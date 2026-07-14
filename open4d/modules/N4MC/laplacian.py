import numpy as np
import open3d as o3d
from scipy.sparse import lil_matrix, vstack
from tqdm import tqdm
from scipy.sparse import coo_matrix, save_npz

def compute_mv_weights(mesh):
    """Compute MV weights for Laplacian"""
    mesh.compute_adjacency_list()
    vertices = np.asarray(mesh.vertices)
    adjacency_list = mesh.adjacency_list
    n = len(vertices)
    W = lil_matrix((n, n), dtype=np.float64)

    for i in range(n):
        neighbors = adjacency_list[i]
        vi = vertices[i]
        weights = []

        for j in neighbors:
            vj = vertices[j]
            w_ij = 1.0 / (np.linalg.norm(vi - vj) + 1e-8)  # avoid division by zero
            weights.append(w_ij)
            W[i, j] = w_ij

        W[i, i] = 0  # ensure it's just off-diagonal weights
    print("mv weights: ", W)
    return W

def build_mv_laplacian(mesh, anchor_indices=[]):
    """
    Builds the extended mean value Laplacian L* ∈ ℝ(n+l)×n.
    """
    n = len(mesh.vertices)
    print("n: ", n)
    W = compute_mv_weights(mesh)
    L = lil_matrix((n, n))
    mesh.compute_adjacency_list()
    for i in range(n):
        if(i%100==0):
            print("i: ", i)
        neighbors = mesh.adjacency_list[i]
        w_sum = sum(W[i, j] for j in neighbors)
        L[i, i] = 1
        for j in neighbors:
            L[i, j] = -W[i, j] / w_sum if w_sum != 0 else 0

    # Add anchor rows to extend Laplacian
    l = len(anchor_indices)
    if l > 0:
        L_ext = lil_matrix((n + l, n))
        L_ext[:n, :] = L
        for row_idx, ki in enumerate(anchor_indices):
            L_ext[n + row_idx, ki] = 1
    else:
        L_ext = L

    return L_ext.tocsr()

def build_mv_laplacian_fast(mesh, anchor_indices=[]):
    n = len(mesh.vertices)
    W = compute_mv_weights(mesh)
    mesh.compute_adjacency_list()

    row_idx = []
    col_idx = []
    data = []

    for i in tqdm(range(n), desc="Building Laplacian"):
        neighbors = mesh.adjacency_list[i]
        w_values = [W[i, j] for j in neighbors]
        w_sum = sum(w_values)
        
        # L[i, i] = 1
        row_idx.append(i)
        col_idx.append(i)
        data.append(1.0)

        if w_sum != 0:
            for j, w_ij in zip(neighbors, w_values):
                row_idx.append(i)
                col_idx.append(j)
                data.append(-w_ij / w_sum)

    # Basic Laplacian
    L = coo_matrix((data, (row_idx, col_idx)), shape=(n, n)).tolil()

    # Anchor rows
    l = len(anchor_indices)
    if l > 0:
        L_ext = lil_matrix((n + l, n))
        L_ext[:n, :] = L
        for row_idx, ki in enumerate(anchor_indices):
            L_ext[n + row_idx, ki] = 1
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

# Select anchor points (e.g., 20 evenly spaced)
anchor_indices = np.linspace(0, len(mesh.vertices)-1, 20, dtype=int)

# Build Laplacian
L_star = build_mv_laplacian_fast(mesh, anchor_indices)
print(L_star)
save_npz("/media/frozzzen/DataDrive/VS2022Projects/tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/Answering/reference/L_star.npz", L_star)

# Compute delta trajectories
D = compute_delta_trajectories(L_star, S)

# Save D if needed
np.save("/media/frozzzen/DataDrive/VS2022Projects/tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/Answering/reference/delta_trajectories_slow.npy", D)
