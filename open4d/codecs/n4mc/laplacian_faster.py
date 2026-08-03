import open3d as o3d
import numpy as np
import cupy as cp
from tqdm import tqdm
from scipy.sparse import coo_matrix, lil_matrix, save_npz

def compute_mv_weights_gpu(vertices, adjacency_list):
    n = len(vertices)
    row, col, data = [], [], []

    vertices_gpu = cp.asarray(vertices)

    for i in range(n):
        vi = vertices_gpu[i]
        neighbors = np.array(list(adjacency_list[i]), dtype=np.int32)
        vj_gpu = vertices_gpu[neighbors]
        diff = vj_gpu - vi
        dist = cp.linalg.norm(diff, axis=1) + 1e-8
        weights = 1.0 / dist

        row.extend([i] * len(neighbors))
        col.extend(neighbors)
        data.extend(cp.asnumpy(weights))
    return coo_matrix((data, (row, col)), shape=(n, n)).tolil()

def build_mv_laplacian_gpu(mesh, anchor_indices=[]):
    vertices = np.asarray(mesh.vertices)
    adjacency_list = mesh.adjacency_list
    n = len(vertices)

    W = compute_mv_weights_gpu(vertices, adjacency_list)

    row_idx = []
    col_idx = []
    data = []

    for i in tqdm(range(n), desc="Building Laplacian"):
        neighbors = adjacency_list[i]
        w_values = [W[i, j] for j in neighbors]
        w_sum = sum(w_values)

        row_idx.append(i)
        col_idx.append(i)
        data.append(1.0)

        if w_sum != 0:
            for j, w_ij in zip(neighbors, w_values):
                row_idx.append(i)
                col_idx.append(j)
                data.append(-w_ij / w_sum)

    L = coo_matrix((data, (row_idx, col_idx)), shape=(n, n)).tolil()

    l = len(anchor_indices)
    if l > 0:
        L_ext = lil_matrix((n + l, n))
        L_ext[:n, :] = L
        for row_offset, ki in enumerate(anchor_indices):
            L_ext[n + row_offset, ki] = 1
    else:
        L_ext = L

    return L_ext.tocsr()



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

L_star = build_mv_laplacian_gpu(mesh, anchor_indices)
print(L_star)
save_npz("/media/frozzzen/DataDrive/VS2022Projects/tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/Answering/reference/L_star.npz", L_star)

# Compute delta trajectories
D = compute_delta_trajectories(L_star, S)

# Save D if needed
np.save("/media/frozzzen/DataDrive/VS2022Projects/tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/Answering/reference/delta_trajectories.npy", D)
np.savetxt("/media/frozzzen/DataDrive/VS2022Projects/tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/Answering/reference/delta_trajectories.txt", D)