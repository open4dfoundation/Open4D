import trimesh
import argparse
import numpy as np
from pathlib import Path

def scale_mesh_to_unit_cube(mesh):
    bounds = mesh.bounds
    center = (bounds[0] + bounds[1]) / 2.0
    scale = 2.0 / np.max(bounds[1] - bounds[0])  # scale to fit in [-1, 1]

    mesh.apply_translation(-center)
    mesh.apply_scale(scale)
    return mesh

def process_folder(input_dir, output_dir, valid_exts={".obj", ".ply", ".stl", ".glb", ".gltf"}):
    input_dir = Path(input_dir)
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    mesh_files = [f for f in input_dir.iterdir() if f.suffix.lower() in valid_exts]

    for mesh_path in mesh_files:
        print(f"Processing {mesh_path.name}...")
        try:
            mesh = trimesh.load(mesh_path)
            if not isinstance(mesh, trimesh.Trimesh):
                print(f"  Skipped (not a mesh): {mesh_path.name}")
                continue
            scaled = scale_mesh_to_unit_cube(mesh)
            output_path = output_dir / mesh_path.name
            scaled.export(output_path)
            print(f"  Saved to {output_path}")
        except Exception as e:
            print(f"  Failed to process {mesh_path.name}: {e}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Scale all meshes in folder to fit inside [-1, 1]^3 cube.")
    parser.add_argument("input_folder", help="Folder containing input meshes")
    parser.add_argument("output_folder", help="Folder to save scaled meshes")
    args = parser.parse_args()

    process_folder(args.input_folder, args.output_folder)
