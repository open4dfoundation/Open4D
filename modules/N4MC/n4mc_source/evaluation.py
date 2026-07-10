import os
os.environ["OPEN3D_RENDERING_ENGINE"] = "OSMesa"
import numpy as np
import open3d as o3d
import pymeshlab

from util import select_viewpoints, evaluate_meshes, compress_folder, print_sizes, compute_bitrate, evaluate_psnr, \
    load_mesh_list, compress_file, compress_folder_7z

# Paths
fps = 30
num_frames = 100
group_size = 5

TDMC_results_path = "./checkpoint_1000"
NeCGS_results_path = "./checkpoint_0500"

Ground_truth_path = "/gt"


# ---------- Compression ----------
print("=== Compressing model files ===\n")

files_to_compress = [
    (os.path.join(NeCGS_results_path, "encoder_compressed.pt"), os.path.join(NeCGS_results_path, "encoder.7z"), "NeCGS Encoder"),
    (os.path.join(NeCGS_results_path, "decoder_compressed.pt"), os.path.join(NeCGS_results_path, "decoder.7z"), "NeCGS Decoder"),
    (os.path.join(NeCGS_results_path, "embed_features"), os.path.join(NeCGS_results_path, "embed_features.7z"), "Embedded Features"),
    (os.path.join(TDMC_results_path, "transformer_compressed.pt"), os.path.join(TDMC_results_path, "transformer_compressed.7z"), "TDMC Transformer"),
    (os.path.join(TDMC_results_path, "latent_codes_compressed.pt"), os.path.join(TDMC_results_path, "latent_codes_compressed.7z"), "TDMC Latent Codes"),
]

total_bits_TDMC = 0
total_bits_NeCGS = 0


for file_path, zip_path, name in files_to_compress:
    if "Embedded Features" in name:
        orig_size, comp_size = compress_folder(file_path, zip_path)
        print_sizes(name, orig_size, comp_size)
    else:
        orig_size, comp_size = compress_file(file_path, zip_path)
        print_sizes(name, orig_size, comp_size)

    if "Embedded Features" in name:
        #print("TDMC:", name)
        total_bits_TDMC += comp_size * 8 / (group_size-1)
        #print("NeCGS:", name)
        total_bits_NeCGS += comp_size * 8
    elif ("Decoder" in name) or ("TDMC" in name):
        #print("TDMC:", name)
        total_bits_TDMC += comp_size * 8
    if "NeCGS Decoder" in name:
        #print("NeCGS:", name)
        total_bits_NeCGS += comp_size * 8



# ---------- Bitrates ----------
print("bitrate TDMC", total_bits_TDMC)
print("bitrate NeCGS Decoder", total_bits_NeCGS)
print("=== Bitrates ===\n")
bitrate_TDMC = compute_bitrate(total_bits_TDMC, num_frames, fps)
bitrate_NeCGS = compute_bitrate(total_bits_NeCGS, num_frames, fps)

print(f"TDMC bitrate : {bitrate_TDMC:.3f} Kbps, {bitrate_TDMC/1000:.3f} Mbps")
print(f"NeCGS bitrate: {bitrate_NeCGS:.3f} Kbps, {bitrate_NeCGS/1000:.3f} Mbps")



# ---------- D1 PSNR Evaluation ----------
print("\n=== Evaluating D1 D2 PSNR ===\n")

TDMC_meshes_path = os.path.join(TDMC_results_path, "rec_mesh")
NeCGS_meshes_path = os.path.join(NeCGS_results_path, "rec_mesh")

(
    avg_d1_TDMC, max_d1_TDMC, min_d1_TDMC,
    avg_d2_TDMC, max_d2_TDMC, min_d2_TDMC
) = evaluate_psnr(Ground_truth_path, TDMC_meshes_path, 10, mode="tdmc")

(
    avg_d1_NeCGS, max_d1_NeCGS, min_d1_NeCGS,
    avg_d2_NeCGS, max_d2_NeCGS, min_d2_NeCGS
) = evaluate_psnr(Ground_truth_path, NeCGS_meshes_path, 10, mode="default")

print("TDMC PSNR:")
print(f"  D1 Avg: {avg_d1_TDMC:.3f}, Max: {max_d1_TDMC:.3f}, Min: {min_d1_TDMC:.3f}")
print(f"  D2 Avg: {avg_d2_TDMC:.3f}, Max: {max_d2_TDMC:.3f}, Min: {min_d2_TDMC:.3f}\n")

print("NeCGS PSNR:")
print(f"  D1 Avg: {avg_d1_NeCGS:.3f}, Max: {max_d1_NeCGS:.3f}, Min: {min_d1_NeCGS:.3f}")
print(f"  D2 Avg: {avg_d2_NeCGS:.3f}, Max: {max_d2_NeCGS:.3f}, Min: {min_d2_NeCGS:.3f}\n")


# ---------- SSIM Evaluation ----------
print("\n=== Evaluating 2D SSIM and PSNR ===\n")

# TDMC metrics
SSIM_depth_TDMC = []
SSIM_color_TDMC = []
PSNR_depth_TDMC = []
PSNR_color_TDMC = []

# NeCGS metrics
SSIM_depth_NeCGS = []
SSIM_color_NeCGS = []
PSNR_depth_NeCGS = []
PSNR_color_NeCGS = []

gt_meshes = load_mesh_list(Ground_truth_path, "default")
tdmc_rec_meshes = load_mesh_list(TDMC_meshes_path, "tdmc")
NeCGS_rec_meshes = load_mesh_list(NeCGS_meshes_path, "default")

# ---------- Fix orientation for TDMC meshes (only first time) ----------
marker_file_tdmc = os.path.join(TDMC_results_path, "orientation_fixed.txt")
if not os.path.exists(marker_file_tdmc):
    print("Fixing orientation for TDMC meshes...")
    for rec_file in tdmc_rec_meshes:
        ms = pymeshlab.MeshSet()
        ms.load_new_mesh(rec_file)
        ms.meshing_invert_face_orientation()
        ms.save_current_mesh(rec_file)
    with open(marker_file_tdmc, "w") as f:
        f.write("orientation fixed")

# ---------- Fix orientation for NeCGS meshes (only first time) ----------
marker_file_necgs = os.path.join(NeCGS_results_path, "orientation_fixed.txt")
if not os.path.exists(marker_file_necgs):
    print("Fixing orientation for NeCGS meshes...")
    for rec_file in NeCGS_rec_meshes:
        ms = pymeshlab.MeshSet()
        ms.load_new_mesh(rec_file)
        ms.meshing_invert_face_orientation()
        ms.save_current_mesh(rec_file)
    with open(marker_file_necgs, "w") as f:
        f.write("orientation fixed")

# ---------- TDMC SSIM ----------
for i, (gt_file, rec_file) in enumerate(zip(gt_meshes[0:10], tdmc_rec_meshes[0:10])):
    if i >= num_frames:
        break


    gt_mesh = o3d.io.read_triangle_mesh(gt_file)
    rec_mesh = o3d.io.read_triangle_mesh(rec_file)

    gt_mesh.compute_vertex_normals()
    rec_mesh.compute_vertex_normals()

    out_dir = os.path.join(Ground_truth_path, "SSIM")
    print(out_dir)
    os.makedirs(out_dir, exist_ok=True)
    view_files_exist = all(os.path.exists(f"{out_dir}/view_{i:02d}.json") for i in range(4))
    num_views = 4
    if not (view_files_exist):
        viewpoints = select_viewpoints(rec_mesh, gt_mesh, num_views=num_views)
        for i, cam in enumerate(viewpoints):
            o3d.io.write_pinhole_camera_parameters(f"{out_dir}/view_{i:02d}.json", cam)
    else:
        viewpoints = [o3d.io.read_pinhole_camera_parameters(f"{out_dir}/view_{i:02d}.json") for i in range(num_views)]

    avg_ssim_depth, avg_ssim_color, avg_psnr_depth, avg_psnr_normal = evaluate_meshes(
        gt_mesh, rec_mesh, viewpoints,
        output_dir=os.path.join(TDMC_results_path, "SSIM", f"renderings_{i:03}")
    )
    SSIM_depth_TDMC.append(avg_ssim_depth)
    SSIM_color_TDMC.append(avg_ssim_color)
    PSNR_depth_TDMC.append(avg_psnr_depth)
    PSNR_color_TDMC.append(avg_psnr_normal)

# ---------- NeCGS SSIM ----------
for i, (gt_file, rec_file) in enumerate(zip(gt_meshes[0:10], NeCGS_rec_meshes[0:10])):
    if i >= num_frames:
        break

    gt_mesh = o3d.io.read_triangle_mesh(gt_file)
    rec_mesh = o3d.io.read_triangle_mesh(rec_file)

    gt_mesh.compute_vertex_normals()
    rec_mesh.compute_vertex_normals()

    out_dir = os.path.join(Ground_truth_path, "SSIM") #############################
    os.makedirs(out_dir, exist_ok=True)
    view_files_exist = all(os.path.exists(f"{out_dir}/view_{i:02d}.json") for i in range(4))
    num_views = 4
    if not (view_files_exist):
        viewpoints = select_viewpoints(rec_mesh, gt_mesh, num_views=num_views)
        for i, cam in enumerate(viewpoints):
            o3d.io.write_pinhole_camera_parameters(f"{out_dir}/view_{i:02d}.json", cam)
    else:
        viewpoints = [o3d.io.read_pinhole_camera_parameters(f"{out_dir}/view_{i:02d}.json") for i in range(num_views)]

    avg_ssim_depth, avg_ssim_color, avg_psnr_depth, avg_psnr_normal = evaluate_meshes(
        gt_mesh, rec_mesh, viewpoints,
        output_dir=os.path.join(NeCGS_results_path, "SSIM", "renderings")
    )
    SSIM_depth_NeCGS.append(avg_ssim_depth)
    SSIM_color_NeCGS.append(avg_ssim_color)
    PSNR_depth_NeCGS.append(avg_psnr_depth)
    PSNR_color_NeCGS.append(avg_psnr_normal)

# ---------- Print Results ----------
print("TDMC SSIM/PSNR:")
print(f"  SSIM Depth Avg: {np.mean(SSIM_depth_TDMC):.4f}")
print(f"  SSIM Color Avg: {np.mean(SSIM_color_TDMC):.4f}")
print(f"  PSNR Depth Avg: {np.mean(PSNR_depth_TDMC):.3f}")
print(f"  PSNR Color Avg: {np.mean(PSNR_color_TDMC):.3f}\n")

print("NeCGS SSIM/PSNR:")
print(f"  SSIM Depth Avg: {np.mean(SSIM_depth_NeCGS):.4f}")
print(f"  SSIM Color Avg: {np.mean(SSIM_color_NeCGS):.4f}")
print(f"  PSNR Depth Avg: {np.mean(PSNR_depth_NeCGS):.3f}")
print(f"  PSNR Color Avg: {np.mean(PSNR_color_NeCGS):.3f}\n")
