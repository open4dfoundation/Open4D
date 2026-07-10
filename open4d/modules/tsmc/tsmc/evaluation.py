import json
import shutil
import open3d as o3d
import argparse
import numpy as np
import cupy as cp
import cupyx.scipy.sparse
import os
from copy import deepcopy
import time
from util import solve_sparse_least_squares_cg, build_mv_laplacian_gpu_fast, calculate_bitrate, select_viewpoints, evaluate_meshes
import subprocess
import re

decoding_time = 0

parser = argparse.ArgumentParser(description="Compress displacements, KLT + laplacian, and directly using Draco.")
parser.add_argument('--dataset', type=str, required=True, help="Dataset name (e.g., 'basketball_player')")
parser.add_argument('--num_frames', type=int, required=True, help="Number of frames to process")
parser.add_argument('--num_centers', type=int, required=True, help="Number of volume centers (pointCount)")
parser.add_argument('--input_path', type=str, required=True, help="Input path for original files and encoded files (T, S, B, D matrices)")
parser.add_argument('--dynamic_static_path', type=str, required=True, help="Input path for reconstructed meshes")
parser.add_argument('--firstIndex', type=int, required=True, help="first index")
parser.add_argument('--lastIndex', type=int, required=True, help="last index")
parser.add_argument('--reference_mesh_path', type=str, required=True, help="Enter path for DECODED reference mesh")
parser.add_argument('--group_idx', type=int, default=1, help="Group index (e.g., group=1 frame[0:num_frames])")

args = parser.parse_args()

dataset = args.dataset
num_frames = args.num_frames
num_centers = args.num_centers
input_path = args.input_path
dynamic_static_path = args.dynamic_static_path
firstIndex = args.firstIndex
lastIndex = args.lastIndex
reference_mesh_path = args.reference_mesh_path
group_idx = args.group_idx


output_mesh_path = os.path.join(dynamic_static_path, "recon_meshes")
os.makedirs(output_mesh_path, exist_ok=True)

load_mesh = o3d.io.read_triangle_mesh(reference_mesh_path)
mesh = o3d.geometry.TriangleMesh.subdivide_midpoint(load_mesh, number_of_iterations=1)
print(mesh)
mesh.compute_adjacency_list()

# Select anchor points
anchor_indices = np.linspace(0, len(mesh.vertices)-1, 2000, dtype=int)

# Build Laplacian using the same decoded mesh, so there is no need to stream L_star
calculate_L_start = time.time()
L_star = build_mv_laplacian_gpu_fast(mesh, anchor_indices)
calculate_L_end = time.time()
print(f"L_star time: {(calculate_L_end - calculate_L_start)*1000} ms")
decoding_time += (calculate_L_end - calculate_L_start)*1000

# Decode D
D = np.load(os.path.join(input_path, "delta_trajectories.npy"))
print(f"Matrix shape: {D.shape}")
print(f"Matrix sample (first 5 rows):\n{D[:5, :]}")

D_hat = np.load(os.path.join(input_path, "delta_trajectories_decoded.npy"))


# Verify reconstruction (with tolerance due to quantization)
mse = np.mean((D - D_hat) ** 2)
print(f"Mean squared error: {mse:.2e}")
print(f"Reconstructed matrix sample (first 5 rows):\n{D_hat[:5, :]}")



print(L_star.dtype, L_star.shape)
print(D_hat.dtype, D_hat.shape)

L_star_gpu = cupyx.scipy.sparse.csr_matrix(L_star)
D_recon_gpu = cp.asarray(D_hat)


solve_start = time.time()
S_recon_gpu = solve_sparse_least_squares_cg(L_star_gpu, D_recon_gpu, maxiter=500, tol=1e-6)
solve_end = time.time()
print(f"Solving time: {(solve_end - solve_start)*1000} ms")
decoding_time += (solve_end - solve_start)*1000
#print(S_recon_gpu.shape, S_recon_gpu)


num_frames = num_frames
frame_files = [os.path.join(input_path, f"displacements_{dataset}_{i:03d}.txt") for i in range(0, num_frames)]

# Step 1: Load displacements and build trajectory vectors
frames = [np.loadtxt(f) for f in frame_files]  # list of [N, 3] arrays
num_vertices = frames[0].shape[0]
assert all(f.shape == (num_vertices, 3) for f in frames), "Shape mismatch!"

# Stack into trajectory vectors: shape [N, 3f]
trajectories = np.hstack(frames)  # [N, 3f] — each row is t_i
print("trajectories shape: ", trajectories.shape)

B_hat = np.loadtxt(os.path.join(input_path, "B_matrix.txt"))
S_hat = S_recon_gpu
t_mean = np.loadtxt(os.path.join(input_path, "T_matrix.txt"))

# Recover full trajectories
T_hat = cp.asnumpy(S_hat) @ B_hat + t_mean  # [N, 3f]
#print(T_hat.shape, T_hat)
# Compare with original
original_T = trajectories  # [N, 3f]
recon_error = np.linalg.norm(original_T - T_hat) / np.linalg.norm(original_T)

np.savetxt(os.path.join(input_path, "T_hat.txt"), T_hat, fmt="%.6f")
print(f"Relative reconstruction error (L2): {recon_error:.6f}")



# Settings
output_dir = os.path.join(input_path, "decoded_reconstructed_displacements")
os.makedirs(output_dir, exist_ok=True)

# Reshape T_hat: [N, 3f] → [N, F, 3]
T_hat_reshaped = T_hat.reshape(num_vertices, num_frames, 3)

# Save each frame
for f in range(num_frames):
    frame_data = T_hat_reshaped[:, f, :]  # [N, 3]
    file_path = os.path.join(output_dir, f"displacements_{dataset}_{f:03d}.txt")
    np.savetxt(file_path, frame_data, fmt="%.6f")

print(f"Saved reconstructed displacements to '{output_dir}/'")


result = subprocess.run([
    '../draco/build/draco_encoder',
    '-i', os.path.join(dynamic_static_path, "static", f'mesh_00.obj'),
    '-o', os.path.join(dynamic_static_path, "static", f'static_backgrounds.drc'),
    '-qp', str('12'),
    '-cl', '7'
], capture_output=True, text=True)
print(result.stdout)
print(result.stderr)

result = subprocess.run([
    '../draco/build/draco_decoder',
    '-i', os.path.join(dynamic_static_path, "static", f'static_backgrounds.drc'),
    '-o', os.path.join(dynamic_static_path, "static", f'static_backgrounds.obj')
], capture_output=True, text=True)
print(result.stdout)
print(result.stderr)


static_backgrounds_path = os.path.join(dynamic_static_path, "static", "static_backgrounds.drc")
result = subprocess.run([
    '../draco/build/draco_decoder',
    '-i', static_backgrounds_path,
    '-o', os.path.join(dynamic_static_path, "static", "static_backgrounds.obj")
    ], capture_output=True, text=True)
print(result.stdout)
print(result.stderr)
times = []
time_pattern = re.compile(r"(\d+) ms to decode")
match = time_pattern.search(result.stdout)
if match:
    times.append(int(match.group(1)))

if times:
    mean_time = sum(times) / len(times)
    print(f"Decoding time for static backgrounds: {mean_time:.6f} ms")
    decoding_time += mean_time

static_backgrounds_mesh = o3d.io.read_triangle_mesh(os.path.join(dynamic_static_path, "static", "static_backgrounds.obj"))

output_mesh_dir = os.path.join(input_path, "decoded_reconstructed_meshes")
os.makedirs(output_mesh_dir, exist_ok=True)

reference_mesh_path = args.reference_mesh_path
mesh = o3d.io.read_triangle_mesh(reference_mesh_path)
#print(mesh)
subdivided_mesh = o3d.geometry.TriangleMesh.subdivide_midpoint(mesh, number_of_iterations=1)
#print(subdivided_mesh)
subdivided_decoded_mesh_vertices = np.array(subdivided_mesh.vertices)
displacement_time = 0
subdivided_mesh.compute_vertex_normals()
#o3d.visualization.draw_geometries([subdivided_mesh])
for k in range(firstIndex, lastIndex+1):
    vertices = deepcopy(subdivided_decoded_mesh_vertices)
    reconstructed_displacement = np.loadtxt(os.path.join(output_dir, f'displacements_{dataset}_{k:03d}.txt'))

    apply_displacement_start = time.time()
    for i in range(0, len(subdivided_decoded_mesh_vertices)):
        vertices[i] += reconstructed_displacement[i]
    apply_displacement_end = time.time()
    #print(f"Applying displacements time: {(apply_displacement_end - apply_displacement_start)*1000} ms")
    displacement_time += (apply_displacement_end - apply_displacement_start)*1000
    reconstruct_mesh = o3d.geometry.TriangleMesh()
    reconstruct_mesh.triangles = subdivided_mesh.triangles
    reconstruct_mesh.vertices = o3d.utility.Vector3dVector(vertices)
    reconstruct_mesh.compute_vertex_normals()
    #o3d.visualization.draw_geometries([reconstruct_mesh])
    reconstruct_mesh = reconstruct_mesh + static_backgrounds_mesh
    #reconstruct_mesh = reconstruct_mesh
    o3d.io.write_triangle_mesh(os.path.join(output_mesh_dir, f'{dataset}_{k:03d}.obj'), reconstruct_mesh)

decoding_time += displacement_time
#o3d.visualization.draw_geometries([reconstruct_mesh])
print(f"Applying displacements time: {displacement_time} ms")

number_frames = num_frames
frame_rate = 30
total_size = 0
total_duration = number_frames / frame_rate

# Things we need to stream: B matrix, t_mean, and encoded D matrix, one static background, one reference mesh
B_size = os.path.getsize(os.path.join(input_path, "B_matrix.txt"))
t_mean_size = os.path.getsize(os.path.join(input_path, "T_matrix.txt"))
D_encoded_size = os.path.getsize(os.path.join(input_path, "delta_trajectories_encoded.npy"))
print(f"B matrix size: {B_size} bytes, t_mean size: {t_mean_size} bytes, D_encoded size: {D_encoded_size} bytes.")

total_size += B_size + t_mean_size + D_encoded_size
displacements_bitrate = calculate_bitrate(B_size + t_mean_size + D_encoded_size, total_duration) / 1000000

static_backgrounds_size = os.path.getsize(os.path.join(dynamic_static_path, "static", "static_backgrounds.drc"))

static_backgrounds_bitrate = calculate_bitrate(static_backgrounds_size, total_duration) / 1000000
total_size += static_backgrounds_size


reference_mesh_file_path = fr'../tvm-editing/TVMEditor.Test/bin/Release/net5.0/Data/{dataset}_{num_centers}/reference_mesh/decimated_reference_mesh.drc'
result = subprocess.run([
    '../draco/build/draco_decoder',
    '-i', reference_mesh_file_path,
    '-o', reference_mesh_path
    ], capture_output=True, text=True)
print(result.stdout)
print(result.stderr)
times = []
time_pattern = re.compile(r"(\d+) ms to decode")
match = time_pattern.search(result.stdout)
if match:
    times.append(int(match.group(1)))

if times:
    mean_time = sum(times) / len(times)
    print(f"Decoding time for reference mesh: {mean_time:.6f} ms")
    decoding_time += mean_time

print(f"Decoding time in total for {num_frames} frames: {decoding_time} ms")
print(f"Decoding time per frame: {decoding_time/num_frames} ms")


reference_mesh_file_size = os.path.getsize(reference_mesh_file_path)
reference_bitrate = calculate_bitrate(reference_mesh_file_size, total_duration) / 1000000
total_size += reference_mesh_file_size

overall_bitrate = calculate_bitrate(total_size, total_duration)


print(f"Total Compressed Size: {total_size} bytes")
print(f"Overall Bitrate: {overall_bitrate} bits per second")

bitrate_kbps = overall_bitrate / 1000
bitrate_mbps = overall_bitrate / 1000000

print(f"Reference Bitrate: {reference_bitrate:.2f} Mbps")
print(f"Displacements Bitrate: {displacements_bitrate:.2f} Mbps")
print(f"Static backgrounds Bitrate: {static_backgrounds_bitrate:.2f} Mbps")
print(f"Overall Bitrate: {bitrate_mbps:.2f} Mbps")
print(f"{reference_bitrate:.2f}, {displacements_bitrate:.2f}, {static_backgrounds_bitrate:.2f}, {bitrate_mbps:.2f}")


# Error metrics
SSIM_depth = []
SSIM_color = []
if_povs = False
for t in range(firstIndex, lastIndex):
    original_mesh = o3d.io.read_triangle_mesh(os.path.join(f"../data/{dataset}/meshes/gt", f'mesh_0{t:01}.obj'))
    reconstruct_mesh = o3d.io.read_triangle_mesh(os.path.join(input_path, 'decoded_reconstructed_meshes', f'{dataset}_{t:03d}.obj'))
    #reconstruct_mesh = o3d.io.read_triangle_mesh(os.path.join(input_path, 'test', f'{dataset}_{t:03d}.obj'))

    original_mesh.compute_vertex_normals()
    reconstruct_mesh.compute_vertex_normals()
    #o3d.visualization.draw_geometries([original_mesh, reconstruct_mesh,subdivided_mesh])

    out_dir = f"./output/{dataset}"
    os.makedirs(out_dir, exist_ok=True)
    view_files_exist = all(os.path.exists(f"{out_dir}/view_{i:02d}.json") for i in range(4))
    num_views = 4
    if not (view_files_exist):
        viewpoints = select_viewpoints(reconstruct_mesh, original_mesh, num_views=num_views)
        for i, cam in enumerate(viewpoints):
            o3d.io.write_pinhole_camera_parameters(
                f"{out_dir}/view_{i:02d}.json", cam)
    else:
        viewpoints = []
        for i in range(num_views):
            cam = o3d.io.read_pinhole_camera_parameters(f"{out_dir}/view_{i:02d}.json")
            viewpoints.append(cam)
    avg_ssim_depth, avg_ssim_color, avg_psnr_depth, avg_psnr_normal = evaluate_meshes(original_mesh, reconstruct_mesh, viewpoints, output_dir=f"./output/{dataset}/TSMC/renderings")
    SSIM_depth.append(avg_ssim_depth)
    SSIM_color.append(avg_ssim_color)


#print("average D1:", np.mean(d1s))
print("average SSIM depth:", np.mean(SSIM_depth))
print("average SSIM color:", np.mean(SSIM_color))
print(json.dumps({"bitrate_mbps": bitrate_mbps, "SSIM_depth_mean": np.mean(SSIM_depth), "SSIM_color_mean": np.mean(SSIM_color), "decoding_time":(decoding_time/num_frames), "reference_bitrate": reference_bitrate, "displacements_bitrate": displacements_bitrate, "static_backgrounds_bitrate": static_backgrounds_bitrate}))



# save compressed files

# === PATHS TO COPY ===
base_output = "../tvm-editing/TVMEditor.Test/bin/Release/net5.0"
folders_to_copy = [
    os.path.join(base_output, f"output/{dataset}_{num_centers}/reference/decoded_reconstructed_displacements"),
    os.path.join(base_output, f"output/{dataset}_{num_centers}/reference/decoded_reconstructed_meshes"),
    os.path.join(base_output, f"output/{dataset}_{num_centers}/reference/reconstructed_displacements"),
]

files_to_copy = [
    os.path.join(base_output, f"output/{dataset}_{num_centers}/reference/B_matrix.txt"),
    os.path.join(base_output, f"output/{dataset}_{num_centers}/reference/delta_trajectories.npy"),
    os.path.join(base_output, f"output/{dataset}_{num_centers}/reference/delta_trajectories_decoded.npy"),
    os.path.join(base_output, f"output/{dataset}_{num_centers}/reference/delta_trajectories_encoded.npy"),
    os.path.join(base_output, f"output/{dataset}_{num_centers}/reference/T_matrix.txt"),
    os.path.join(base_output, f"Data/{dataset}_{num_centers}/reference_mesh/others/undecimated_reference_mesh.obj"),
    os.path.join(base_output, f"Data/{dataset}_{num_centers}/reference_mesh/decimated_reference_mesh.drc"),
    os.path.join(base_output, f"Data/{dataset}_{num_centers}/reference_mesh/decimated_reference_mesh.obj"),
    os.path.join(base_output, f"Data/{dataset}_{num_centers}/reference_mesh/others/decoded_decimated_reference_mesh.obj"),
]

# === DESTINATION ===
dest_folder = os.path.join(f"./output/{dataset}", f"{dataset}_group_{group_idx:03d}")

# Create destination folder
if os.path.exists(dest_folder):
    shutil.rmtree(dest_folder)
os.makedirs(dest_folder, exist_ok=True)

# === COPY FOLDERS ===
for folder in folders_to_copy:
    if os.path.exists(folder):
        target = os.path.join(dest_folder, os.path.basename(folder))
        shutil.copytree(folder, target)
        print(f"Copied folder: {folder} -> {target}")
    else:
        print(f"Warning: folder not found {folder}")

# === COPY FILES ===
for file in files_to_copy:
    if os.path.exists(file):
        target = os.path.join(dest_folder, os.path.basename(file))
        shutil.copy(file, target)
        print(f"Copied file: {file} -> {target}")
    else:
        print(f"Warning: file not found {file}")

print(f"\nAll files/folders copied into {dest_folder}")