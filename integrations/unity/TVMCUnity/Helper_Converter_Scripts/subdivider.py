import os
import open3d as o3d
from pathlib import Path


def subdivide_files_recursive(master_directory, subdivision_iterations=1):
    """
    Recursively find 3D files and subdivide them using Open3D

    Args:
        master_directory: Path to the master directory
        subdivision_iterations: Number of subdivision iterations (default: 1)
    """

    # Supported file extensions
    supported_extensions = {'.ply', '.obj', '.stl', '.off'}

    # Walk through all directories
    for root, dirs, files in os.walk(master_directory):
        for file in files:
            file_path = Path(root) / file

            # Check if it's a supported 3D file
            if file_path.suffix.lower() in supported_extensions:
                print(f"Processing: {file_path}")

                try:
                    # Load the mesh
                    mesh = o3d.io.read_triangle_mesh(str(file_path))

                    if len(mesh.vertices) > 0:
                        print(f"  Original vertices: {len(mesh.vertices)}, triangles: {len(mesh.triangles)}")

                        # Subdivide the mesh
                        subdivided_mesh = mesh.subdivide_midpoint(number_of_iterations=subdivision_iterations)

                        print(
                            f"  After subdivision: {len(subdivided_mesh.vertices)} vertices, {len(subdivided_mesh.triangles)} triangles")

                        # Save the subdivided mesh (add _subdivided to filename)
                        output_path = file_path.parent / f"{file_path.stem}_subdivided{file_path.suffix}"
                        o3d.io.write_triangle_mesh(str(output_path), subdivided_mesh)
                        print(f"  Saved to: {output_path}")

                    else:
                        print(f"  No mesh data found in {file_path}")

                except Exception as e:
                    print(f"  Error processing {file_path}: {e}")

                print()  # Empty line for readability


# Set your directory path here and run
if __name__ == "__main__":
    master_dir = "/Users/joshua/Downloads/Mitch_Sequence"  # Change this path
    subdivision_iterations = 1  # Change this if you want more subdivision

    print(f"Starting subdivision of 3D files in: {master_dir}")
    subdivide_files_recursive(master_dir, subdivision_iterations)
    print("Done!")