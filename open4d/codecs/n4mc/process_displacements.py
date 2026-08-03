import numpy as np
import os
from copy import deepcopy
import open3d as o3d

# Settings
num_frames = 9
m = 3  # number of eigentrajectories to keep
frame_files = [f"/media/frozzzen/DataDrive/VS2022Projects/tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/Answering/reference/displacements_answering_{i:03d}.txt" for i in range(1, num_frames+1)]

# Step 1: Load displacements and build trajectory vectors
frames = [np.loadtxt(f) for f in frame_files]  # list of [N, 3] arrays
num_vertices = frames[0].shape[0]
assert all(f.shape == (num_vertices, 3) for f in frames), "Shape mismatch!"

# Stack into trajectory vectors: shape [N, 3f]
trajectories = np.hstack(frames)  # [N, 3f] — each row is t_i
print("trajectories shape: ", trajectories.shape)
# Step 2: Compute average trajectory
t_mean = np.mean(trajectories, axis=0, keepdims=True)  # shape [1, 3f]

# Step 3: Center and compute autocorrelation matrix
centered = trajectories - t_mean
R = centered.T @ centered  # [3f, 3f]

# Step 4: Eigendecomposition
eigvals, eigvecs = np.linalg.eigh(R)  # eigvecs: columns are eigentrajectories
sorted_idx = np.argsort(eigvals)[::-1]
eigvecs = eigvecs[:, sorted_idx[:m]]  # [3f, m]

# Step 5: Project to reduced space
B = eigvecs.T  # [m, 3f]
S = centered @ B.T  # [N, m]

# Optional: Save B, S, t_mean
np.savetxt("/media/frozzzen/DataDrive/VS2022Projects/tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/Answering/reference/B_matrix.txt", B, fmt="%.6f")
np.savetxt("/media/frozzzen/DataDrive/VS2022Projects/tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/Answering/reference/S_matrix.txt", S, fmt="%.6f")
np.savetxt("/media/frozzzen/DataDrive/VS2022Projects/tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/Answering/reference/T_matrix.txt", t_mean, fmt="%.6f")
np.savez_compressed("/media/frozzzen/DataDrive/VS2022Projects/tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/Answering/reference/S_matrix.npz",S=S)
print(f"Finished. S shape = {S.shape}, saved to S_matrix.txt")

# Recover full trajectories
T_hat = S @ B + t_mean  # [N, 3f]

# Compare with original
original_T = trajectories  # [N, 3f]
recon_error = np.linalg.norm(original_T - T_hat) / np.linalg.norm(original_T)

np.savetxt("/media/frozzzen/DataDrive/VS2022Projects/tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/Answering/reference/T_hat.txt", T_hat, fmt="%.6f")
print(f"Relative reconstruction error (L2): {recon_error:.6f}")



# Settings
output_dir = "/media/frozzzen/DataDrive/VS2022Projects/tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/Answering/reference/reconstructed_displacements"
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

#o3d.visualization.draw_geometries([reconstruct_mesh])