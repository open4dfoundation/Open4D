"""View, animate, or export geometry from an Open4D file with Open3D."""

from __future__ import annotations

import argparse
import time
from pathlib import Path
from typing import Any

from open4d_open3d import iter_frames, load_frame, sequence_info


def _replace_geometry(target: Any, source: Any, o3d: Any) -> None:
    if isinstance(target, o3d.geometry.TriangleMesh) and isinstance(
        source, o3d.geometry.TriangleMesh
    ):
        target.vertices = source.vertices
        target.triangles = source.triangles
        target.vertex_colors = source.vertex_colors
        target.vertex_normals = source.vertex_normals
        return
    if isinstance(target, o3d.geometry.PointCloud) and isinstance(
        source, o3d.geometry.PointCloud
    ):
        target.points = source.points
        target.colors = source.colors
        target.normals = source.normals
        return
    raise TypeError("Geometry type changed within the Open4D sequence")


def _animate(path: Path, start_frame_id: int, fps: float) -> None:
    import open3d as o3d

    frame_ids = sequence_info(path)["frame_ids"]
    if start_frame_id not in frame_ids:
        raise IndexError(
            f"Frame {start_frame_id} is not present; available frame IDs: {frame_ids}"
        )
    frames = iter_frames(path, start=frame_ids.index(start_frame_id))
    try:
        geometry = next(frames)
    except StopIteration as exc:
        raise ValueError("Open4D sequence contains no frames") from exc

    visualizer = o3d.visualization.Visualizer()
    visualizer.create_window(window_name=f"Open4D: {path.name}")
    visualizer.add_geometry(geometry)
    seconds_per_frame = 1.0 / fps
    try:
        for decoded in frames:
            started = time.monotonic()
            _replace_geometry(geometry, decoded, o3d)
            visualizer.update_geometry(geometry)
            if not visualizer.poll_events():
                break
            visualizer.update_renderer()
            remaining = seconds_per_frame - (time.monotonic() - started)
            if remaining > 0:
                time.sleep(remaining)
    finally:
        visualizer.destroy_window()


def _export(path: Path, geometry: Any) -> None:
    import open3d as o3d

    if isinstance(geometry, o3d.geometry.TriangleMesh):
        written = o3d.io.write_triangle_mesh(str(path), geometry)
    else:
        written = o3d.io.write_point_cloud(str(path), geometry)
    if not written:
        raise OSError(f"Open3D could not write {path}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path, help="Open4D .o4d file")
    parser.add_argument("--frame", type=int, default=0, help="stored frame ID")
    parser.add_argument("--animate", action="store_true", help="play from --frame")
    parser.add_argument("--fps", type=float, default=30.0, help="maximum animation FPS")
    parser.add_argument("--export-ply", type=Path, help="export the selected frame")
    args = parser.parse_args()
    if args.fps <= 0:
        parser.error("--fps must be greater than zero")

    if args.animate:
        _animate(args.path, args.frame, args.fps)
        return

    geometry = load_frame(args.path, args.frame)
    if args.export_ply:
        _export(args.export_ply, geometry)
    else:
        import open3d as o3d

        o3d.visualization.draw_geometries(
            [geometry], window_name=f"Open4D frame {args.frame}"
        )


if __name__ == "__main__":
    main()
