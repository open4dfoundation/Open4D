import numpy as np
import argparse
import os
from copy import deepcopy
import open3d as o3d
import constriction
import cupy as cp
from matplotlib import pyplot as plt
from tqdm import tqdm
from scipy.sparse import coo_matrix, lil_matrix, save_npz
from util import solve_sparse_least_squares_cg, build_mv_laplacian_gpu_fast, compute_mv_weights_gpu, compute_delta_trajectories
import point_cloud_utils as pcu

parser = argparse.ArgumentParser(description="Compress displacements, KLT + laplacian, and directly using Draco.")
parser.add_argument('--dataset', type=str, required=True, help="Dataset name (e.g., 'basketball_player')")
parser.add_argument('--num_frames', type=int, required=True, help="Number of frames to process")
parser.add_argument('--num_eigenvectors', type=int, required=True, help="Number of eigenvectors to keep")
parser.add_argument('--displacement_path', type=str, required=True, help="Input path for the displacement fields")
parser.add_argument('--output_path', type=str, required=True, help="Output path for the compressed files")
parser.add_argument('--firstIndex', type=int, required=True, help="first index")
parser.add_argument('--lastIndex', type=int, required=True, help="last index")
parser.add_argument('--reference_mesh_path', type=str, required=True, help="Enter path for DECODED reference mesh")

args = parser.parse_args()

dataset = args.dataset
num_frames = args.num_frames
num_eigenvectors = args.num_eigenvectors
displacement_path = args.displacement_path
output_path = args.output_path
firstIndex = args.firstIndex
lastIndex = args.lastIndex

assert num_eigenvectors <= num_frames*3, "Please choose a smaller m for compression, e.g., m=3 for num_frames=10!"
m = num_eigenvectors
displacement_files = [os.path.join(displacement_path, f"displacements_{dataset}_{i:03d}.txt") for i in range(firstIndex, lastIndex+1)]


displacements = [np.loadtxt(f) for f in displacement_files]

num_vertices = displacements[0].shape[0]
assert all(f.shape == (num_vertices, 3) for f in displacements), "Shape mismatch!"


trajectories = np.hstack(displacements)
print("trajectories shape: ", trajectories.shape)

t_mean = np.mean(trajectories, axis=0, keepdims=True)


centered = trajectories - t_mean
R = centered.T @ centered  # [3f, 3f]



eigvals, eigvecs = np.linalg.eigh(R)
sorted_idx = np.argsort(eigvals)[::-1]
eigvals = eigvals[sorted_idx]
eigvecs = eigvecs[:, sorted_idx]


plt.figure()
plt.plot(np.cumsum(eigvals) / np.sum(eigvals))
plt.xlabel("Number of components")
plt.ylabel("Cumulative energy")
plt.title("Cumulative Energy of Eigenvalues")
plt.grid(True)
#plt.show()


plt.figure()
plt.plot(eigvals, label="Eigenvalues")
plt.yscale("log")
plt.xlabel("Component index")
plt.ylabel("Eigenvalue (log scale)")
plt.title("Eigenvalue Spectrum")
plt.legend()
plt.grid(True)
#plt.show()
top = eigvecs[:, :m]
k = 0
bottom = eigvecs[:, -k:] if m + k <= eigvecs.shape[1] else eigvecs[:, m:]
V_hybrid = np.hstack([top, bottom])
print(f"{m}+{k} eigenvectors:", V_hybrid.shape[1])
print(V_hybrid)
eigvecs = V_hybrid
eigvecs = eigvecs[:, :m]


B = eigvecs.T
S = centered @ B.T


np.savetxt(os.path.join(output_path, "B_matrix.txt"), B, fmt="%.6f")
np.savetxt(os.path.join(output_path, "S_matrix.txt"), S, fmt="%.6f")
np.savetxt(os.path.join(output_path, "T_matrix.txt"), t_mean, fmt="%.6f")
np.savez_compressed(os.path.join(output_path, "S_matrix.npz"),S=S)
print(f"Finished. S shape = {S.shape}, saved to S_matrix.txt")


T_hat = S @ B + t_mean




original_T = trajectories
recon_error = np.linalg.norm(original_T - T_hat) / np.linalg.norm(original_T)

np.savetxt(os.path.join(output_path, "T_hat.txt"), T_hat, fmt="%.6f")
print(f"Relative reconstruction error (L2) of T matrix: {recon_error:.6f}")


output_dir = os.path.join(output_path, "reconstructed_displacements")
os.makedirs(output_dir, exist_ok=True)


T_hat_reshaped = T_hat.reshape(num_vertices, num_frames, 3)
print(T_hat_reshaped[0])

for f in range(num_frames):
    frame_data = T_hat_reshaped[:, f, :]
    file_path = os.path.join(output_dir, f"displacements_{dataset}_{f:03d}.txt")
    np.savetxt(file_path, frame_data, fmt="%.6f")

print(f"Saved reconstructed displacements to '{output_dir}/'")



# Get reconsructed meshes. If the distortion is unacceptable, try to increase num_eigenvectors, it's a trade-off between file size and compression ratio.
reference_mesh_path = args.reference_mesh_path
mesh = o3d.io.read_triangle_mesh(reference_mesh_path)
#print(mesh)
subdivided_mesh = o3d.geometry.TriangleMesh.subdivide_midpoint(mesh, number_of_iterations=1)
#print(subdivided_mesh)
subdivided_decoded_mesh_vertices = np.array(subdivided_mesh.vertices)
static_backgrounds_mesh = o3d.io.read_triangle_mesh(os.path.join(f"../data/{dataset}/meshes", "static", "mesh_00.obj"))
for k in range(firstIndex, lastIndex+1):
    vertices = deepcopy(subdivided_decoded_mesh_vertices)
    reconstructed_displacement = np.loadtxt(os.path.join(output_dir, f'displacements_{dataset}_{k:03d}.txt'))
    #reconstructed_displacement = np.loadtxt(os.path.join(displacement_path, f"displacements_{dataset}_{k:03d}.txt"))
    for i in range(0, len(subdivided_decoded_mesh_vertices)):
        vertices[i] += reconstructed_displacement[i]
    reconstruct_mesh = o3d.geometry.TriangleMesh()
    reconstruct_mesh.triangles = subdivided_mesh.triangles
    reconstruct_mesh.vertices = o3d.utility.Vector3dVector(vertices)
    reconstruct_mesh.compute_vertex_normals()
    #o3d.visualization.draw_geometries([reconstruct_mesh])
    test_path = f"../tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/{dataset}_{2000}/reference/test"
    os.makedirs(test_path, exist_ok=True)
    o3d.io.write_triangle_mesh(os.path.join(test_path, f'{dataset}_{k:03d}.obj'), reconstruct_mesh + static_backgrounds_mesh)




load_mesh = o3d.io.read_triangle_mesh(reference_mesh_path)
subdivided_mesh = o3d.geometry.TriangleMesh.subdivide_midpoint(load_mesh, number_of_iterations=1)
#print(subdivided_mesh)
# Load the reference mesh (decoded)
mesh = subdivided_mesh
#print(mesh)
mesh.compute_adjacency_list()


# Reduced trajectory matrix from PCA
S = np.loadtxt(os.path.join(output_path, "S_matrix.txt"))
#print(S.shape, S)
# Select anchor points
anchor_indices = np.linspace(0, len(mesh.vertices)-1, 2000, dtype=int)

# Build Laplacian

L_star = build_mv_laplacian_gpu_fast(mesh, anchor_indices)
#print(L_star)
save_npz(os.path.join(output_path, "L_star.npz"), L_star)

# Compute delta trajectories
D = compute_delta_trajectories(L_star, S)

# Save D 
np.save(os.path.join(output_path, "delta_trajectories.npy"), D)
np.savetxt(os.path.join(output_path, "delta_trajectories.txt"), D)

# Displacement Encoded
D = np.load(os.path.join(output_path, "delta_trajectories.npy"))
print(f"Matrix shape: {D.shape}")
print(f"Matrix sample (first 5 rows):\n{D[:5, :]}")

# Quantize float values to integers, QuantizedGaussian model and AnsCoder can only be applied on integers.
scaling_factor = 10000
D_quantized = np.round(D * scaling_factor).astype(np.int32)
min_val, max_val = np.min(D_quantized), np.max(D_quantized)
print(f"Quantized data range: [{min_val}, {max_val}]")

# Define the QuantizedGaussian model
model_range = (min_val, max_val)
model_family = constriction.stream.model.QuantizedGaussian(*model_range)

# Flatten the matrix to a 1D array for encoding
symbols = D_quantized.flatten().astype(np.int32)  

# Estimate entropy model parameters (mean and std for each column)
means = np.zeros(len(symbols), dtype=np.float64)
stds = np.zeros(len(symbols), dtype=np.float64)
cols = D_quantized.shape[1]
for j in range(cols):
    col_data = D_quantized[:, j]
    mean = np.mean(col_data)
    std = np.std(col_data) if np.std(col_data) > 0 else 1.0  # Avoid zero std
    means[j::cols] = mean
    stds[j::cols] = std

# Encode the symbols
encoder = constriction.stream.stack.AnsCoder()
encoder.encode_reverse(symbols, model_family, means, stds)

# Get the compressed representation
compressed = encoder.get_compressed()
np.save(os.path.join(output_path, "delta_trajectories_encoded.npy"), compressed)
print(f"Compressed representation: {compressed}")
print(f"Compressed size: {encoder.num_bits()} bits")
original_bits = D.nbytes * 8  # Assuming 64-bit floats
print(f"Compression ratio: {original_bits / encoder.num_bits():.2f}")
print(f"{encoder.num_bits():.2f}")

# Decode the symbols
decoder = constriction.stream.stack.AnsCoder(compressed)
reconstructed_quantized = decoder.decode(model_family, means, stds)
shape = D.shape
reconstructed = reconstructed_quantized.reshape(shape) / scaling_factor

# Verify reconstruction (with tolerance due to quantization)
mse = np.mean((D - reconstructed) ** 2)
print(f"Mean squared error: {mse:.2e}")
print(f"Reconstructed matrix sample (first 5 rows):\n{reconstructed[:5, :]}")
print("Matrix successfully encoded and decoded!")

np.save(os.path.join(output_path, "delta_trajectories_decoded.npy"), reconstructed)

