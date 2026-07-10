import os
import open3d as o3d
import numpy as np
from natsort import natsorted
import math

def normalize_mesh_to_unit_cube(mesh):
    vertices = np.asarray(mesh.vertices)
    min_bounds = vertices.min(axis=0)
    max_bounds = vertices.max(axis=0)
    center = (min_bounds + max_bounds) / 2
    scale = (max_bounds - min_bounds).max() / 2
    normalized_vertices = (vertices - center) / scale
    mesh.vertices = o3d.utility.Vector3dVector(normalized_vertices)
    return mesh

def offset_mesh(mesh, offset):
    vertices = np.asarray(mesh.vertices)
    mesh.vertices = o3d.utility.Vector3dVector(vertices + offset)
    return mesh

def normalize_whole_scene(meshes):
    """Normalize a list of meshes together so they all fit inside [-1,1]^3"""
    all_vertices = np.vstack([np.asarray(m.vertices) for m in meshes if m.has_vertices()])
    min_bounds = all_vertices.min(axis=0)
    max_bounds = all_vertices.max(axis=0)
    center = (min_bounds + max_bounds) / 2
    scale = (max_bounds - min_bounds).max() / 2

    normalized = []
    for mesh in meshes:
        vertices = np.asarray(mesh.vertices)
        vertices = (vertices - center) / scale
        mesh.vertices = o3d.utility.Vector3dVector(vertices)
        normalized.append(mesh)
    return normalized

def generate_grid_offsets(n_seq, spacing=1):
    """Generate grid offsets for n_seq items."""
    grid_size = math.ceil(math.sqrt(n_seq))
    coords = []
    for idx in range(n_seq):
        row = idx // grid_size
        col = idx % grid_size
        coords.append((col, row))

    coords = np.array(coords, dtype=float)
    coords -= coords.mean(axis=0, keepdims=True)
    return [np.array([x * spacing, 0, z * spacing]) for x, z in coords]

def combine_sequences_framewise(input_folders, output_folder, max_frames=None, mesh_exts=('.ply', '.obj', '.stl')):
    os.makedirs(output_folder, exist_ok=True)

    offsets = generate_grid_offsets(len(input_folders))

    # Get sorted file lists for each sequence
    seq_files = []
    for folder in input_folders:
        files = [f for f in os.listdir(folder) if f.lower().endswith(mesh_exts)]
        seq_files.append(natsorted(files))

    min_len = min(len(files) for files in seq_files)
    if max_frames is not None:
        min_len = min(min_len, max_frames)

    for frame_idx in range(min_len):
        meshes = []
        for seq_idx, folder in enumerate(input_folders):
            filename = seq_files[seq_idx][frame_idx]
            input_path = os.path.join(folder, filename)

            mesh = o3d.io.read_triangle_mesh(input_path, enable_post_processing=False)
            if not mesh.has_vertices():
                print(f"Skipping empty mesh: {filename}")
                continue

            mesh = normalize_mesh_to_unit_cube(mesh)
            mesh = offset_mesh(mesh, offsets[seq_idx])
            meshes.append(mesh)

        combined_mesh = o3d.geometry.TriangleMesh()
        for m in meshes:
            combined_mesh += m

        output_path = os.path.join(output_folder, f"frame_{frame_idx:04d}.obj")
        o3d.io.write_triangle_mesh(
            output_path,
            combined_mesh,
            write_vertex_normals=False,
            write_vertex_colors=False,
            write_triangle_uvs=False
        )
        print(f"Processed combined frame {frame_idx+1}/{min_len}")
# Example usage: combine 3 sequences into one cube
input_folders = [
    "./meshes",
]
output_folder = "./output"
combine_sequences_framewise(input_folders, output_folder, max_frames=100)