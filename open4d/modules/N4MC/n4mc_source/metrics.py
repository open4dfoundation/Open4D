from copy import deepcopy
import subprocess
import numpy as np
import open3d as o3d
import trimesh
import os
import time
import re
from scipy.spatial import cKDTree




def compute_D1_psnr(original_mesh, decoded_mesh):
    original_vertices = np.array(original_mesh.vertices)
    #original_vertices = normalize_vertices(original_vertices)
    decoded_vertices = np.array(decoded_mesh.vertices)
    #decoded_vertices = normalize_vertices(decoded_vertices)

    pcd_original = o3d.geometry.PointCloud()
    pcd_original.points = o3d.utility.Vector3dVector(original_vertices)

    pcd_decoded = o3d.geometry.PointCloud()
    pcd_decoded.points = o3d.utility.Vector3dVector(decoded_vertices)
    pcd_tree = o3d.geometry.KDTreeFlann(pcd_decoded)

    MSE = 0
    for i in range(0, len(original_vertices)):
        [k, index, _] = pcd_tree.search_knn_vector_3d(original_vertices[i], 1)
        MSE += np.square(np.linalg.norm(original_vertices[i] - decoded_vertices[index]))
    MSE = MSE / len(original_vertices)
    #print("D1 mse:",MSE)
    aabb = pcd_original.get_axis_aligned_bounding_box()
    min_bound = aabb.get_min_bound()

    max_bound = aabb.get_max_bound()

    signal_peak = np.linalg.norm(max_bound - min_bound)
    #signal_peak = 12
    #print(signal_peak)
    psnr = 20 * np.log10(signal_peak) - 10 * np.log10(MSE)
    #print(psnr)
    return psnr


def compute_D2_psnr(original_mesh, decoded_mesh):
    decoded_mesh.compute_vertex_normals()

    original_vertices = np.asarray(original_mesh.vertices)
    decoded_vertices = np.asarray(decoded_mesh.vertices)

    # Build KD-tree on decoded mesh
    pcd_decoded = o3d.geometry.PointCloud()
    pcd_decoded.points = o3d.utility.Vector3dVector(decoded_vertices)
    pcd_decoded.normals = o3d.utility.Vector3dVector(decoded_mesh.vertex_normals)
    pcd_tree = o3d.geometry.KDTreeFlann(pcd_decoded)

    MSE = 0.0
    for i in range(len(original_vertices)):
        _, idx, _ = pcd_tree.search_knn_vector_3d(original_vertices[i], 1)
        nearest_v = decoded_vertices[idx[0]]
        nearest_n = np.asarray(pcd_decoded.normals)[idx[0]]

        diff = original_vertices[i] - nearest_v
        dist = np.dot(diff, nearest_n)   # point-to-plane distance
        MSE += dist**2

    MSE /= len(original_vertices)

    # Signal peak: diagonal of original bounding box
    aabb = o3d.geometry.AxisAlignedBoundingBox.create_from_points(o3d.utility.Vector3dVector(original_vertices))
    signal_peak = np.linalg.norm(aabb.get_max_bound() - aabb.get_min_bound())
    #signal_peak = 12
    #print(signal_peak)
    psnr = 20 * np.log10(signal_peak) - 10 * np.log10(MSE)
    return psnr

def compute_D1_D2_psnr(original_mesh, decoded_mesh):
    """
    Compute both D1 (point-to-point) and D2 (point-to-plane) PSNR
    between original and decoded meshes in one pass.
    Returns: (psnr_d1, psnr_d2)
    """
    original_vertices = np.asarray(original_mesh.vertices)
    decoded_vertices = np.asarray(decoded_mesh.vertices)

    # Prepare decoded point cloud
    pcd_decoded = o3d.geometry.PointCloud()
    pcd_decoded.points = o3d.utility.Vector3dVector(decoded_vertices)

    decoded_mesh.compute_vertex_normals()
    pcd_decoded.normals = o3d.utility.Vector3dVector(decoded_mesh.vertex_normals)

    pcd_tree = o3d.geometry.KDTreeFlann(pcd_decoded)

    MSE_D1 = 0.0
    MSE_D2 = 0.0

    decoded_normals = np.asarray(pcd_decoded.normals)

    for v in original_vertices:
        _, idx, _ = pcd_tree.search_knn_vector_3d(v, 1)
        nearest_v = decoded_vertices[idx[0]]
        nearest_n = decoded_normals[idx[0]]

        # D1: point-to-point
        dist_p2p = np.linalg.norm(v - nearest_v)
        MSE_D1 += dist_p2p**2

        # D2: point-to-plane
        diff = v - nearest_v
        dist_p2pl = np.dot(diff, nearest_n)
        MSE_D2 += dist_p2pl**2

    n = len(original_vertices)
    MSE_D1 /= n
    MSE_D2 /= n

    # Signal peak: diagonal of original bounding box
    aabb = o3d.geometry.AxisAlignedBoundingBox.create_from_points(
        o3d.utility.Vector3dVector(original_vertices)
    )
    signal_peak = np.linalg.norm(aabb.get_max_bound() - aabb.get_min_bound())
    signal_peak = np.sqrt(12)
    psnr_d1 = 20 * np.log10(signal_peak) - 10 * np.log10(MSE_D1)
    psnr_d2 = 20 * np.log10(signal_peak) - 10 * np.log10(MSE_D2)

    return psnr_d1, psnr_d2


def compute_MSE_RMSE(original_mesh, decoded_mesh):
    original_vertices = np.array(original_mesh.vertices)
    #original_vertices = normalize_vertices(original_vertices)
    decoded_vertices = np.array(decoded_mesh.vertices)
    #decoded_vertices = normalize_vertices(decoded_vertices)

    pcd_original = o3d.geometry.PointCloud()
    pcd_original.points = o3d.utility.Vector3dVector(original_vertices)

    pcd_decoded = o3d.geometry.PointCloud()
    pcd_decoded.points = o3d.utility.Vector3dVector(decoded_vertices)
    pcd_tree = o3d.geometry.KDTreeFlann(pcd_decoded)

    MSE = 0
    for i in range(0, len(original_vertices)):
        [k, index, _] = pcd_tree.search_knn_vector_3d(original_vertices[i], 1)
        MSE += np.square(np.linalg.norm(original_vertices[i] - decoded_vertices[index]))
    MSE = MSE / len(original_vertices)
    #print("MSE:", MSE)
    RMSE = np.sqrt(MSE)

    return np.log10(MSE), np.log10(RMSE)


def chamfer_distance(mesh1, mesh2):
    # Extract vertices
    v1 = np.asarray(mesh1.vertices)
    v2 = np.asarray(mesh2.vertices)

    # KD-trees
    tree1 = cKDTree(v1)
    tree2 = cKDTree(v2)

    # Nearest neighbor distances
    dist1, _ = tree2.query(v1, k=1)  # each v1 to nearest in v2
    dist2, _ = tree1.query(v2, k=1)  # each v2 to nearest in v1

    # Symmetric Chamfer Distance (squared distances)
    cd = np.mean(dist1**2) + np.mean(dist2**2)
    return cd