import open3d as o3d
import numpy as np
import cupy as cp
from tqdm import tqdm
from scipy.sparse import coo_matrix, lil_matrix, save_npz, vstack, diags, identity
import cupyx
import cupyx.scipy.sparse
from cupyx.scipy.sparse.linalg import cg
import os
from copy import deepcopy
import time
from collections import defaultdict

decoding_time = 0

def compute_mv_weights_gpu(vertices, adjacency_list):
    n = len(vertices)
    row, col = [], []

    for i in range(n):
        neighbors = adjacency_list[i]
        row.extend([i] * len(neighbors))
        col.extend(neighbors)

    row = np.array(row, dtype=np.int32)
    col = np.array(col, dtype=np.int32)

    vertices_gpu = cp.asarray(vertices)
    vi_gpu = vertices_gpu[row]
    vj_gpu = vertices_gpu[col]

    diff_gpu = vj_gpu - vi_gpu
    dist_gpu = cp.linalg.norm(diff_gpu, axis=1) + 1e-8
    weights_gpu = 1.0 / dist_gpu

    # Transfer to CPU just once
    data = cp.asnumpy(weights_gpu)

    W = coo_matrix((data, (row, col)), shape=(n, n)).tocsr()
    return W

def build_mv_laplacian_gpu(mesh, anchor_indices=[]):
    vertices = np.asarray(mesh.vertices)
    adjacency_list = mesh.adjacency_list
    n = len(vertices)

    start = time.time()
    W = compute_mv_weights_gpu(vertices, adjacency_list)
    print("Computing mv weights:", time.time() - start)

    # Now build Laplacian efficiently
    row_idx = []
    col_idx = []
    data = []

    W = W.tocsr()  # Fast row slicing

    start_time = time.time()
    for i in range(n):
        neighbors = np.array(list(adjacency_list[i]), dtype=np.int32)
        if len(neighbors) == 0:
            row_idx.append(i)
            col_idx.append(i)
            data.append(1.0)
            continue

        w_values = W[i, neighbors].toarray()[0]
        w_sum = np.sum(w_values)

        row_idx.append(i)
        col_idx.append(i)
        data.append(1.0)

        if w_sum > 1e-8:
            data.extend([-w / w_sum for w in w_values])
            row_idx.extend([i] * len(neighbors))
            col_idx.extend(neighbors)
    print("loop time: ", time.time() - start_time)

    start_time = time.time()
    L = coo_matrix((data, (row_idx, col_idx)), shape=(n, n)).tocsr()
    print("time: ", time.time() - start_time)

    start = time.time()
    if len(anchor_indices) > 0:
        anchor_rows = lil_matrix((len(anchor_indices), n))
        for row_offset, ki in enumerate(anchor_indices):
            anchor_rows[row_offset, ki] = 1
        L_ext = vstack([L, anchor_rows]).tocsr()
    else:
        L_ext = L
    print("vstack method:", time.time() - start)
    return L_ext

def build_mv_laplacian_gpu_fast(mesh, anchor_indices=[]):
    vertices = np.asarray(mesh.vertices)
    adjacency_list = mesh.adjacency_list
    n = len(vertices)

    start = time.time()
    W = compute_mv_weights_gpu(vertices, adjacency_list)
    print("Computing mv weights:", time.time() - start)

    start_time = time.time()

    # Normalize W by row sums (L = I - D⁻¹W)
    W = W.tocsr()
    row_sums = np.array(W.sum(axis=1)).flatten()
    row_inv = np.reciprocal(row_sums, where=row_sums > 1e-8)

    D_inv = diags(row_inv)
    L = identity(n, format='csr') - D_inv @ W
    print("Laplacian build time (vectorized):", time.time() - start_time)

    start = time.time()
    if len(anchor_indices) > 0:
        anchor_rows = lil_matrix((len(anchor_indices), n))
        for row_offset, ki in enumerate(anchor_indices):
            anchor_rows[row_offset, ki] = 1
        L_ext = vstack([L, anchor_rows]).tocsr()
    else:
        L_ext = L
    print("vstack method:", time.time() - start)
    return L_ext


def compute_delta_trajectories(L_ext, S):
    """
    Compute delta trajectory matrix D = L* @ S
    L_ext: (n+l) x n sparse matrix
    S: n x m matrix
    """
    return L_ext @ S  # Result is (n+l) x m

load_mesh = o3d.io.read_triangle_mesh("/media/frozzzen/DataDrive/VS2022Projects/tvm-editing/TVMEditor.Test/bin/Release/net5.0/Data/answering_2000/reference_mesh/decimated_reference_mesh.obj")
subdivided_mesh = o3d.geometry.TriangleMesh.subdivide_midpoint(load_mesh, number_of_iterations=1)
print(subdivided_mesh)
# Load the average mesh (decoded)
mesh = subdivided_mesh
print(mesh)
mesh.compute_adjacency_list()


# Reduced trajectory matrix from PCA
S = np.loadtxt("/media/frozzzen/DataDrive/VS2022Projects/tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/Answering/reference/S_matrix.txt")  # or any (n, m) numpy array

# Select anchor points
anchor_indices = np.linspace(0, len(mesh.vertices)-1, 20, dtype=int)

# Build Laplacian
calculate_L_start = time.time()
L_star = build_mv_laplacian_gpu_fast(mesh, anchor_indices)
calculate_L_end = time.time()
print(f"L_star time: {(calculate_L_end - calculate_L_start)*1000} ms")
decoding_time += calculate_L_start - calculate_L_end

D_hat = np.load("/media/frozzzen/DataDrive/VS2022Projects/tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/Answering/reference/delta_trajectories_decoded.npy")

print(L_star.dtype, L_star.shape)
print(D_hat.dtype, D_hat.shape)

def solve_sparse_least_squares_cg(L_star_gpu, D_hat_gpu, maxiter=500, tol=1e-6):
    A_T = L_star_gpu.transpose()
    AtA = A_T @ L_star_gpu
    AtB = A_T @ D_hat_gpu

    num_cols = AtB.shape[1]
    n = AtA.shape[0]
    S_recon_gpu = cp.zeros((n, num_cols), dtype=cp.float32)

    for i in range(num_cols):
        b = AtB[:, i]
        x, info = cg(AtA, b, tol=tol, maxiter=maxiter)
        if info != 0:
            print(f"CG did not converge on column {i}, info: {info}")
        S_recon_gpu[:, i] = x

    return S_recon_gpu

L_star_gpu = cupyx.scipy.sparse.csr_matrix(L_star)
D_recon_gpu = cp.asarray(D_hat)

decoding_time = 0

solve_start = time.time()
S_recon_gpu = solve_sparse_least_squares_cg(L_star_gpu, D_recon_gpu)
solve_end = time.time()
print(f"Sparse CG batched time: {(solve_end - solve_start)*1000:.2f} ms")
decoding_time += solve_end - solve_start

print(S_recon_gpu.shape, S_recon_gpu)


num_frames = 9
frame_files = [f"/media/frozzzen/DataDrive/VS2022Projects/tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/Answering/reference/displacements_answering_{i:03d}.txt" for i in range(1, num_frames+1)]

# Step 1: Load displacements and build trajectory vectors
frames = [np.loadtxt(f) for f in frame_files]  # list of [N, 3] arrays
num_vertices = frames[0].shape[0]
assert all(f.shape == (num_vertices, 3) for f in frames), "Shape mismatch!"

# Stack into trajectory vectors: shape [N, 3f]
trajectories = np.hstack(frames)  # [N, 3f] — each row is t_i
print("trajectories shape: ", trajectories.shape)

B_hat = np.loadtxt("/media/frozzzen/DataDrive/VS2022Projects/tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/Answering/reference/B_matrix.txt")
S_hat = S_recon_gpu
t_mean = np.loadtxt("/media/frozzzen/DataDrive/VS2022Projects/tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/Answering/reference/T_matrix.txt")

# Recover full trajectories
T_hat = cp.asnumpy(S_hat) @ B_hat + t_mean  # [N, 3f]

# Compare with original
original_T = trajectories  # [N, 3f]
recon_error = np.linalg.norm(original_T - T_hat) / np.linalg.norm(original_T)

np.savetxt("/media/frozzzen/DataDrive/VS2022Projects/tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/Answering/reference/T_hat.txt", T_hat, fmt="%.6f")
print(f"Relative reconstruction error (L2): {recon_error:.6f}")



# Settings
output_dir = "/media/frozzzen/DataDrive/VS2022Projects/tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/Answering/reference/decoded_reconstructed_displacements"
os.makedirs(output_dir, exist_ok=True)

# Reshape T_hat: [N, 3f] → [N, F, 3]
T_hat_reshaped = T_hat.reshape(num_vertices, num_frames, 3)

# Save each frame
for f in range(num_frames):
    frame_data = T_hat_reshaped[:, f, :]  # [N, 3]
    file_path = os.path.join(output_dir, f"displacements_answering_{f:03d}.txt")
    np.savetxt(file_path, frame_data, fmt="%.6f")

print(f"Saved reconstructed displacements to '{output_dir}/'")


import open3d as o3d

mesh = o3d.io.read_triangle_mesh("/media/frozzzen/DataDrive/VS2022Projects/tvm-editing/TVMEditor.Test/bin/Release/net5.0/Data/answering_2000/reference_mesh/decimated_reference_mesh.obj")
print(mesh)
subdivided_mesh = o3d.geometry.TriangleMesh.subdivide_midpoint(mesh, number_of_iterations=1)
print(subdivided_mesh)
subdivided_decoded_mesh_vertices = np.array(subdivided_mesh.vertices)
reordered_vertices = deepcopy(subdivided_decoded_mesh_vertices)

original_displacement = np.loadtxt(fr'/media/frozzzen/DataDrive/VS2022Projects/tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/Answering/reference/reconstructed_displacements/displacements_answering_000.txt')
for i in range(0, len(subdivided_decoded_mesh_vertices)):
    reordered_vertices[i] += original_displacement[i]
reconstruct_mesh = o3d.geometry.TriangleMesh()
reconstruct_mesh.triangles = subdivided_mesh.triangles
reconstruct_mesh.vertices = o3d.utility.Vector3dVector(reordered_vertices)
reconstruct_mesh.compute_vertex_normals()

o3d.visualization.draw_geometries([mesh, reconstruct_mesh])