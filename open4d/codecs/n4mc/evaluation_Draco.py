import os
import zipfile

import numpy as np
import open3d as o3d
from metrics import compute_D1_psnr, compute_D2_psnr, compute_D1_D2_psnr
import pymeshlab
import py7zr

from util import select_viewpoints, evaluate_meshes, compress_folder, print_sizes, compute_bitrate, evaluate_psnr, load_mesh_list, compress_file

# Paths
fps = 30
num_frames = 10
dataset = 'mixed'


Ground_truth_path = "/mnt/datadrive/ChromeDownloads/Mesh_dataset/combined_scaled"
Draco_encode_results_root = f"/mnt/datadrive/ChromeDownloads/Mesh_dataset/encode_Draco/{dataset}"
Draco_decode_results_root = f"/mnt/datadrive/ChromeDownloads/Mesh_dataset/decode_Draco/{dataset}"


# ---------- Draco Evaluation ----------
print("\n=== Evaluating Draco ===\n")


qps_result = []
bitrates = []
d1_psnrs = []
d2_psnrs = []
ssim_depths = []
ssim_colors = []
psnr_depths = []
psnr_colors = []
#3 -15 
qps = range(7, 8)  # QP range for Draco
for qp in qps:
    print(f"\n--- Draco QP {qp} ---")

    # Paths for encoded + decoded meshes
    Draco_encode_results_path = os.path.join(Draco_encode_results_root, f"{dataset}_qp_{qp}")
    Draco_decode_results_path = os.path.join(Draco_decode_results_root, f"{dataset}_qp_{qp}")

    # Compute bitrate
    total_bits_Draco = 0
    drc_files = [f for f in os.listdir(Draco_encode_results_path) if f.endswith(".drc")]
    drc_files.sort()
    for drc_file in drc_files[:num_frames]:
        file_path = os.path.join(Draco_encode_results_path, drc_file)
        file_size = os.path.getsize(file_path)
        total_bits_Draco += file_size * 8
    #print(total_bits_Draco)
    bitrate_Draco = compute_bitrate(total_bits_Draco, num_frames, fps) / 1000

    # ---------- D1/D2 PSNR Evaluation ----------
    (
        avg_d1_Draco, max_d1_Draco, min_d1_Draco,
        avg_d2_Draco, max_d2_Draco, min_d2_Draco
    ) = evaluate_psnr(Ground_truth_path, Draco_decode_results_path, num_frames, mode="default")

    # ---------- SSIM/PSNR Evaluation ----------
    SSIM_depth, SSIM_color, PSNR_depth, PSNR_color = [], [], [], []

    gt_meshes = load_mesh_list(Ground_truth_path, "default")
    Draco_rec_meshes = load_mesh_list(Draco_decode_results_path, "default")

    for i, (gt_file, rec_file) in enumerate(zip(gt_meshes[:num_frames], Draco_rec_meshes[:num_frames])):
        gt_mesh = o3d.io.read_triangle_mesh(gt_file)
        rec_mesh = o3d.io.read_triangle_mesh(rec_file)
        gt_mesh.compute_vertex_normals()
        rec_mesh.compute_vertex_normals()

        out_dir = os.path.join(f"/mnt/datadrive/ChromeDownloads/Mesh_dataset/decode_Draco/{dataset}", "SSIM")
        os.makedirs(out_dir, exist_ok=True)

        num_views = 4
        view_files_exist = all(os.path.exists(f"{out_dir}/view_{j:02d}.json") for j in range(num_views))
        if not view_files_exist:
            viewpoints = select_viewpoints(rec_mesh, gt_mesh, num_views=num_views)
            for j, cam in enumerate(viewpoints):
                o3d.io.write_pinhole_camera_parameters(f"{out_dir}/view_{j:02d}.json", cam)
        else:
            viewpoints = []
            for j in range(num_views):
                cam = o3d.io.read_pinhole_camera_parameters(f"{out_dir}/view_{j:02d}.json")
                viewpoints.append(cam)

        avg_ssim_depth, avg_ssim_color, avg_psnr_depth, avg_psnr_normal = evaluate_meshes(
            gt_mesh, rec_mesh, viewpoints,
            output_dir=os.path.join(out_dir, "renderings")
        )

        SSIM_depth.append(avg_ssim_depth)
        SSIM_color.append(avg_ssim_color)
        PSNR_depth.append(avg_psnr_depth)
        PSNR_color.append(avg_psnr_normal)

    qps_result.append(qp)
    bitrates.append(bitrate_Draco)
    d1_psnrs.append(avg_d1_Draco)
    d2_psnrs.append(avg_d2_Draco)
    ssim_depths.append(np.mean(SSIM_depth))
    ssim_colors.append(np.mean(SSIM_color))
    psnr_depths.append(np.mean(PSNR_depth))
    psnr_colors.append(np.mean(PSNR_color))

# ---------- Final Summary ----------
print("\n=== Draco Final Results ===")
print("Draco_qps =", qps_result)
print("Draco_bitrates =", [round(x, 3) for x in bitrates])
print("Draco_d1_psnrs =", [round(x, 3) for x in d1_psnrs])
print("Draco_d2_psnrs =", [round(x, 3) for x in d2_psnrs])
print("Draco_ssim_depths =", [round(x, 3) for x in ssim_depths])
print("Draco_ssim_colors =", [round(x, 3) for x in ssim_colors])
print("Draco_psnr_depths =", [round(x, 3) for x in psnr_depths])
print("Draco_psnr_colors =", [round(x, 3) for x in psnr_colors])