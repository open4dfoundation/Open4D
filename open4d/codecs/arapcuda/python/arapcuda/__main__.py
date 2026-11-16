"""Track a directory of meshes and write the centers of every frame.

    python -m arapcuda MESH_DIR OUT_DIR [--centers 2000] [--grid-resolution 512] ...

Frames are the .obj files (or .ply, with open3d or trimesh installed) in
MESH_DIR, in numeric filename order. Writes OUT_DIR/centers_XXX.xyz and, from
frame 1 on, OUT_DIR/transforms_XXX.txt.
"""
import argparse
import re
import sys
import time
from pathlib import Path

import numpy as np

from . import Tracker, read_obj, write_transforms, write_xyz


def read_mesh(path):
    if path.suffix.lower() == ".obj":
        return read_obj(path)
    try:
        import open3d as o3d
        mesh = o3d.io.read_triangle_mesh(str(path))
        return np.asarray(mesh.vertices), np.asarray(mesh.triangles)
    except ImportError:
        import trimesh
        mesh = trimesh.load(path, process=False)
        return np.asarray(mesh.vertices), np.asarray(mesh.faces)


def main(argv=None):
    parser = argparse.ArgumentParser(prog="python -m arapcuda", description=__doc__.split("\n")[0])
    parser.add_argument("mesh_dir", type=Path)
    parser.add_argument("out_dir", type=Path)
    parser.add_argument("--centers", type=int, default=2000)
    parser.add_argument("--grid-resolution", type=int, default=512)
    parser.add_argument("--gradient-threshold", type=float, default=1e-4)
    parser.add_argument("--smooth-sigma", type=float, default=0.125)
    parser.add_argument("--smooth-sigma2", type=float, default=0.125)
    parser.add_argument("--device", type=int, default=0)
    args = parser.parse_args(argv)

    meshes = sorted((p for p in args.mesh_dir.iterdir() if p.suffix.lower() in (".obj", ".ply")),
                    key=lambda p: [int(x) for x in re.findall(r"\d+", p.stem)] or [0])
    if not meshes:
        sys.exit(f"no .obj or .ply files in {args.mesh_dir}")
    args.out_dir.mkdir(parents=True, exist_ok=True)

    tracker = Tracker(centers=args.centers, grid_resolution=args.grid_resolution,
                      gradient_threshold=args.gradient_threshold, smooth_sigma=args.smooth_sigma,
                      smooth_sigma2=args.smooth_sigma2, device=args.device)
    start = time.perf_counter()
    for index, path in enumerate(meshes):
        frame = tracker.add_frame(*read_mesh(path))
        write_xyz(args.out_dir / f"centers_{index:03d}.xyz", frame["centers"])
        if frame["transforms"] is not None:
            write_transforms(args.out_dir / f"transforms_{index:03d}.txt", frame["transforms"])
        print(f"frame {index}: {path.name}, {frame['iterations']} iterations, {frame['seconds']:.2f} s", flush=True)
    print(f"tracked {len(meshes)} frames in {time.perf_counter() - start:.2f} s")


if __name__ == "__main__":
    main()
