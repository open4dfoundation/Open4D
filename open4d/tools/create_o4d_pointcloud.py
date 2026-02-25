import os
import re
from typing import Callable, Optional
import numpy as np
import trimesh

from o4d_pointcloud_io import O4DPointCloudWriter


def _default_sort_key(name: str):
    nums = re.findall(r"\d+", name)
    return (int(nums[-1]) if nums else float("inf"), name)


def _load_xyz(path: str) -> np.ndarray:
    # expects whitespace-separated x y z
    return np.loadtxt(path, dtype=np.float32)[:, :3]


def _load_pts(path: str) -> np.ndarray:
    # Some .pts files start with an integer count on first line
    with open(path, "r") as f:
        first = f.readline().strip()
    try:
        n = int(first)
        data = np.loadtxt(path, dtype=np.float32, skiprows=1)
        return data[:, :3]
    except ValueError:
        data = np.loadtxt(path, dtype=np.float32)
        return data[:, :3]


def import_pointcloud_folder_to_o4d(
    input_dir: str,
    output_o4d_path: str,
    fps: Optional[float] = None,
    sort_key: Callable[[str], object] = _default_sort_key,
    meta: Optional[dict] = None,
) -> int:
    exts = (".ply", ".xyz", ".pts")
    files = [f for f in os.listdir(input_dir) if f.lower().endswith(exts)]
    files.sort(key=sort_key)

    if not files:
        raise FileNotFoundError(f"No point cloud files found in {input_dir} ({exts})")

    meta_out = dict(meta or {})
    meta_out.update({
        "source": "import_pointcloud_folder_to_o4d",
        "input_dir": os.path.abspath(input_dir),
        "fps": fps,
        "num_source_files": len(files),
    })

    frames_written = 0
    with O4DPointCloudWriter(output_o4d_path, meta=meta_out) as w:
        for i, fname in enumerate(files):
            path = os.path.join(input_dir, fname)
            ext = os.path.splitext(fname)[1].lower()

            colors = None
            if ext == ".ply":
                # trimesh loads PLY point clouds as PointCloud or Trimesh; handle both
                obj = trimesh.load(path, process=False)

                if isinstance(obj, trimesh.points.PointCloud):
                    points = np.asarray(obj.vertices, dtype=np.float32)
                    if hasattr(obj, "colors") and obj.colors is not None and len(obj.colors) == len(points):
                        c = np.asarray(obj.colors)
                        # sometimes RGBA
                        colors = c[:, :3].astype(np.uint8, copy=False)
                elif isinstance(obj, trimesh.Trimesh):
                    # if it accidentally has faces, treat vertices as points
                    points = np.asarray(obj.vertices, dtype=np.float32)
                else:
                    raise ValueError(f"Unsupported PLY payload type from {path}: {type(obj)}")

            elif ext == ".xyz":
                points = _load_xyz(path)
            elif ext == ".pts":
                points = _load_pts(path)
            else:
                raise ValueError(f"Unexpected extension: {ext}")

            ts = (i / fps) if (fps is not None and fps > 0) else float(i)
            w.write_keyframe(points, colors_rgb=colors, timestamp=ts, frame_index=i)
            frames_written += 1
            if frames_written > 10:
                return frames_written

    return frames_written

from o4d_pointcloud_io import O4DPointCloudReader

n = import_pointcloud_folder_to_o4d("pc_frames/", "pc_file.o4d", fps=30.0)
print("wrote", n)

with O4DPointCloudReader("pc_file.o4d") as r:
    print("meta:", r.meta)
    p, c, ts = r.get_frame(0)
    print(p.shape, c.shape if c is not None else None, ts)
