import os

import numpy as np
import open3d as o3d
import pymeshlab
from util import select_viewpoints, evaluate_meshes, compress_folder, print_sizes, compute_bitrate, evaluate_psnr, load_mesh_list, compress_file, compress_folder_7z


vdmc_meshes_path = '/home/frozzzen/Documents/Github/mpeg-vdmc-tm/decoded/s6c2r5_thom'
Ground_truth_path = '/media/frozzzen/DataDrive/ChromeDownloads/C6/C6/C6_shifted'

out_dir = os.path.join(Ground_truth_path, "SSIM")
print(out_dir)
num_views = 4
num_frames = 100

#viewpoints = [o3d.io.read_pinhole_camera_parameters(f"{out_dir}/view_{i:02d}.json") for i in range(num_views)]

(
    avg_d1_vdmc, max_d1_vdmc, min_d1_vdmc,
    avg_d2_vdmc, max_d2_vdmc, min_d2_vdmc
) = evaluate_psnr(Ground_truth_path, vdmc_meshes_path, 10, mode="default")


print("vdmc PSNR:")
print(f"  D1 Avg: {avg_d1_vdmc:.3f}, Max: {max_d1_vdmc:.3f}, Min: {min_d1_vdmc:.3f}")
print(f"  D2 Avg: {avg_d2_vdmc:.3f}, Max: {max_d2_vdmc:.3f}, Min: {min_d2_vdmc:.3f}\n")


# vdmc metrics
SSIM_depth_vdmc = []
SSIM_color_vdmc = []
PSNR_depth_vdmc = []
PSNR_color_vdmc = []


gt_meshes = load_mesh_list(Ground_truth_path, "default")
vdmc_rec_meshes = load_mesh_list(vdmc_meshes_path, "default")



# ---------- vdmc SSIM ----------
for i, (gt_file, rec_file) in enumerate(zip(gt_meshes[0:10], vdmc_rec_meshes[0:10])):
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
        output_dir=os.path.join(vdmc_meshes_path, "SSIM", "renderings")
    )
    SSIM_depth_vdmc.append(avg_ssim_depth)
    SSIM_color_vdmc.append(avg_ssim_color)
    PSNR_depth_vdmc.append(avg_psnr_depth)
    PSNR_color_vdmc.append(avg_psnr_normal)

print("vdmc SSIM/PSNR:")
print(f"  SSIM Depth Avg: {np.mean(SSIM_depth_vdmc):.4f}")
print(f"  SSIM Color Avg: {np.mean(SSIM_color_vdmc):.4f}")
print(f"  PSNR Depth Avg: {np.mean(PSNR_depth_vdmc):.3f}")
print(f"  PSNR Color Avg: {np.mean(PSNR_color_vdmc):.3f}\n")