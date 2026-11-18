"""Track a folder of OBJ meshes and save the centers.

    python track_obj_folder.py MESH_DIR centers.npz
"""
import sys
from pathlib import Path

import numpy as np

import arapcuda

mesh_dir, out_file = Path(sys.argv[1]), sys.argv[2]
paths = sorted(mesh_dir.glob("*.obj"))
print(f"{len(paths)} meshes, {arapcuda.device_count()} GPU(s)")

tracker = arapcuda.Tracker(centers=2000, grid_resolution=512)
centers = []
for path in paths:
    vertices, triangles = arapcuda.read_obj(path)
    frame = tracker.add_frame(vertices, triangles)
    centers.append(frame["centers"])
    print(f"{path.name}: {frame['iterations']} iterations, {frame['seconds']:.2f} s")

centers = np.stack(centers)                       # (frames, 2000, 3)
moved = np.linalg.norm(centers[-1] - centers[0], axis=1)
print(f"median center moved {np.median(moved):.4f} from first to last frame")

np.savez(out_file, centers=centers)
print(f"saved {centers.shape} to {out_file}")
