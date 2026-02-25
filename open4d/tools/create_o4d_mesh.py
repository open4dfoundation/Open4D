import os
import re
from typing import Callable, Optional, Tuple, List

import numpy as np
import trimesh

from o4d_mesh_io import O4DMeshWriter


def _default_sort_key(name: str):
    """
    Natural-ish sort: extracts the last integer in the filename if present.
    mesh_00012.ply -> 12
    otherwise falls back to string.
    """
    nums = re.findall(r"\d+", name)
    return (int(nums[-1]) if nums else float("inf"), name)


def _timestamp_from_filename(name: str) -> Optional[float]:
    """
    Optional helper: tries to extract a float timestamp from filename patterns.
    Examples:
      frame_0.033.obj -> 0.033
      t=1.25.ply -> 1.25
    Returns None if not found.
    """
    m = re.search(r"([-+]?\d*\.\d+|\d+)", name)
    return float(m.group(0)) if m else None


def import_folder_to_o4d(
    input_dir: str,
    output_o4d_path: str,
    fps: Optional[float] = None,
    sort_key: Callable[[str], object] = _default_sort_key,
    timestamp_fn: Optional[Callable[[str], Optional[float]]] = _timestamp_from_filename,
    meta: Optional[dict] = None,
    allow_nontriangles: bool = False,
) -> int:
    """
    Reads a directory of .obj/.ply meshes and writes a single .o4d file.

    Parameters
    ----------
    input_dir : str
        Folder containing mesh frames (.obj/.ply).
    output_o4d_path : str
        Destination .o4d file.
    fps : Optional[float]
        If provided, timestamps are frame_index / fps (unless timestamp_fn returns a value).
    sort_key : Callable
        How to order the frames.
    timestamp_fn : Optional[Callable]
        Extract timestamp from filename. If returns None, use fps/frame_index.
        Pass None to always use fps/index timing.
    meta : Optional[dict]
        Stored in HEAD chunk.
    allow_nontriangles : bool
        If False, will force triangulation (recommended).

    Returns
    -------
    int
        Number of frames written.
    """
    exts = (".obj", ".ply")
    files = [f for f in os.listdir(input_dir) if f.lower().endswith(exts)]
    files.sort(key=sort_key)

    if not files:
        raise FileNotFoundError(f"No .obj/.ply files found in {input_dir}")

    meta_out = dict(meta or {})
    meta_out.update({
        "source": "import_folder_to_o4d",
        "input_dir": os.path.abspath(input_dir),
        "fps": fps,
        "num_source_files": len(files),
    })

    frames_written = 0
    with O4DMeshWriter(output_o4d_path, meta=meta_out) as w:
        for i, fname in enumerate(files):
            path = os.path.join(input_dir, fname)

            mesh = trimesh.load(path, process=False)

            # trimesh may return a Scene if file has multiple geometries
            if isinstance(mesh, trimesh.Scene):
                # Merge all geometry into one mesh
                geoms = []
                for g in mesh.geometry.values():
                    if isinstance(g, trimesh.Trimesh):
                        geoms.append(g)
                if not geoms:
                    raise ValueError(f"No mesh geometry in scene: {path}")
                mesh = trimesh.util.concatenate(geoms)

            if not isinstance(mesh, trimesh.Trimesh):
                raise ValueError(f"Unsupported mesh type from {path}: {type(mesh)}")

            if not allow_nontriangles:
                # Ensure triangles
                if mesh.faces is None or mesh.faces.shape[1] != 3:
                    mesh = mesh.triangulate()

            V = np.asarray(mesh.vertices, dtype=np.float32)
            F = np.asarray(mesh.faces, dtype=np.uint32)

            # Timestamp logic
            ts = None
            if timestamp_fn is not None:
                ts = timestamp_fn(fname)
            if ts is None:
                ts = (i / fps) if (fps is not None and fps > 0) else float(i)

            w.write_keyframe(V, F, timestamp=ts, frame_index=i)
            frames_written += 1

    return frames_written

n = import_folder_to_o4d(
    input_dir="meshes/",
    output_o4d_path="sequence.o4d",
    fps=30.0,                 # timestamps = i/30 unless filename has a number
    timestamp_fn=None,        # force fps-based timestamps (recommended)
    meta={"dataset": "my_capture"}
)
print("frames written:", n)
