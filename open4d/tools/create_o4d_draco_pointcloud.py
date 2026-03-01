"""
Import a folder of point cloud frames, compress each frame with Google Draco,
and store the result in a single .o4d file.

Supported input formats: .ply, .xyz, .pts

Usage example
-------------
    from open4d.tools.create_o4d_draco_pointcloud import import_pointcloud_folder_to_o4d_draco

    n = import_pointcloud_folder_to_o4d_draco(
        input_dir="pc_frames/",
        output_o4d_path="pc_draco.o4d",
        fps=30.0,
        quantization_bits=14,
        compression_level=7,
    )
    print(f"Wrote {n} Draco-compressed frames")

Command-line quick-start
------------------------
    python create_o4d_draco_pointcloud.py pc_frames/ pc_draco.o4d --fps 30
"""

import argparse
import os
import re
import sys
from typing import Callable, Optional

import numpy as np
import trimesh

from open4d.io.o4d_draco_pointcloud_io import O4DDracoPointCloudWriter


# ----------------------------
# Helpers
# ----------------------------

def _default_sort_key(name: str):
    """Natural sort by the last integer found in the filename."""
    nums = re.findall(r"\d+", name)
    return (int(nums[-1]) if nums else float("inf"), name)


def _load_xyz(path: str) -> np.ndarray:
    """Load whitespace-separated X Y Z (and optionally more columns)."""
    return np.loadtxt(path, dtype=np.float32)[:, :3]


def _load_pts(path: str) -> np.ndarray:
    """Load .pts files (optional leading count line)."""
    with open(path) as fh:
        first = fh.readline().strip()
    try:
        int(first)
        return np.loadtxt(path, dtype=np.float32, skiprows=1)[:, :3]
    except ValueError:
        return np.loadtxt(path, dtype=np.float32)[:, :3]


# ----------------------------
# Main import function
# ----------------------------

def import_pointcloud_folder_to_o4d_draco(
    input_dir: str,
    output_o4d_path: str,
    fps: Optional[float] = None,
    quantization_bits: int = 14,
    compression_level: int = 7,
    sort_key: Callable[[str], object] = _default_sort_key,
    meta: Optional[dict] = None,
) -> int:
    """
    Read all point cloud files from *input_dir*, compress each frame with
    Google Draco, and write a single Draco-encoded .o4d file.

    Parameters
    ----------
    input_dir : str
        Directory containing .ply / .xyz / .pts files (one file = one frame).
    output_o4d_path : str
        Destination .o4d file.
    fps : float, optional
        If given, timestamps are assigned as frame_index / fps.
        Otherwise timestamps equal the frame index.
    quantization_bits : int
        Draco position quantisation (1-30). Default 14 gives sub-mm precision
        for scenes up to ~16 m.
    compression_level : int
        Draco compression effort (0=fastest … 10=smallest). Default 7.
    sort_key : callable
        Ordering function applied to filenames.
    meta : dict, optional
        Extra key/value pairs stored in the file's HEAD metadata.

    Returns
    -------
    int
        Number of frames written.
    """
    exts = (".ply", ".xyz", ".pts")
    files = [f for f in os.listdir(input_dir) if f.lower().endswith(exts)]
    files.sort(key=sort_key)

    if not files:
        raise FileNotFoundError(
            f"No point cloud files ({exts}) found in: {input_dir}"
        )

    meta_out = dict(meta or {})
    meta_out.update({
        "source": "import_pointcloud_folder_to_o4d_draco",
        "input_dir": os.path.abspath(input_dir),
        "fps": fps,
        "num_source_files": len(files),
    })

    frames_written = 0
    with O4DDracoPointCloudWriter(
        output_o4d_path,
        quantization_bits=quantization_bits,
        compression_level=compression_level,
        meta=meta_out,
    ) as writer:
        for i, fname in enumerate(files):
            path = os.path.join(input_dir, fname)
            ext = os.path.splitext(fname)[1].lower()

            colors: Optional[np.ndarray] = None

            if ext == ".ply":
                obj = trimesh.load(path, process=False)
                if isinstance(obj, trimesh.points.PointCloud):
                    points = np.asarray(obj.vertices, dtype=np.float32)
                    if (
                        hasattr(obj, "colors")
                        and obj.colors is not None
                        and len(obj.colors) == len(points)
                    ):
                        colors = np.asarray(obj.colors)[:, :3].astype(np.uint8, copy=False)
                elif isinstance(obj, trimesh.Trimesh):
                    points = np.asarray(obj.vertices, dtype=np.float32)
                else:
                    raise ValueError(
                        f"Unsupported PLY payload type from {path}: {type(obj)}"
                    )

            elif ext == ".xyz":
                points = _load_xyz(path)

            elif ext == ".pts":
                points = _load_pts(path)

            else:
                raise ValueError(f"Unexpected extension: {ext}")

            ts = (i / fps) if (fps is not None and fps > 0) else float(i)
            writer.write_keyframe(points, colors_rgb=colors, timestamp=ts, frame_index=i)
            frames_written += 1

    return frames_written


# ----------------------------
# CLI entry point
# ----------------------------

def _cli():
    parser = argparse.ArgumentParser(
        description="Compress a folder of point cloud frames into a Draco-encoded .o4d file."
    )
    parser.add_argument("input_dir", help="Directory with .ply / .xyz / .pts files")
    parser.add_argument("output", help="Output .o4d file path")
    parser.add_argument("--fps", type=float, default=None, help="Frames per second for timestamps")
    parser.add_argument("--quantization-bits", type=int, default=14, help="Draco quantization bits (default: 14)")
    parser.add_argument("--compression-level", type=int, default=7, help="Draco compression level 0-10 (default: 7)")
    args = parser.parse_args()

    n = import_pointcloud_folder_to_o4d_draco(
        input_dir=args.input_dir,
        output_o4d_path=args.output,
        fps=args.fps,
        quantization_bits=args.quantization_bits,
        compression_level=args.compression_level,
    )
    print(f"Done — wrote {n} Draco-compressed frames to '{args.output}'")


if __name__ == "__main__":
    _cli()
