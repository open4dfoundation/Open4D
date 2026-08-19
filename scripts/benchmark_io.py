#!/usr/bin/env python3
"""Reproducible in-process benchmarks for the public Open4D I/O API."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import statistics
import tempfile
import time
import tracemalloc

import numpy as np

from open4d.io import inspect_sequence, open_sequence


def grid(side: int) -> tuple[np.ndarray, np.ndarray]:
    axis = np.linspace(-1, 1, side, dtype=np.float32)
    x, y = np.meshgrid(axis, axis)
    points = np.column_stack((x.ravel(), y.ravel(), np.sin(3 * x).ravel() / 20))
    cell = np.arange((side - 1) ** 2, dtype=np.uint32)
    row, column = np.divmod(cell, side - 1)
    corner = row * side + column
    faces = np.column_stack(
        (corner, corner + 1, corner + side, corner + 1,
         corner + side + 1, corner + side)
    ).reshape(-1, 3)
    return points, faces


def write_ply(path: Path, points: np.ndarray, faces: np.ndarray) -> None:
    vertices = np.asarray(points, dtype="<f4")
    records = np.empty(len(faces), dtype=[("count", "u1"), ("indices", "<i4", 3)])
    records["count"], records["indices"] = 3, faces
    header = (
        "ply\nformat binary_little_endian 1.0\n"
        f"element vertex {len(points)}\nproperty float x\nproperty float y\n"
        "property float z\n"
        f"element face {len(faces)}\nproperty list uchar int vertex_indices\n"
        "end_header\n"
    ).encode("ascii")
    path.write_bytes(header + vertices.tobytes() + records.tobytes())


def write_obj(path: Path, points: np.ndarray, faces: np.ndarray) -> None:
    lines = [*(f"v {x} {y} {z}" for x, y, z in points),
             *("f " + " ".join(str(int(i) + 1) for i in face) for face in faces)]
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def timed(function, repeats: int) -> float:
    samples = []
    for _ in range(repeats):
        start = time.perf_counter()
        function()
        samples.append(time.perf_counter() - start)
    return statistics.median(samples)


def peak_bytes(function) -> int:
    tracemalloc.start()
    try:
        tracemalloc.reset_peak()
        function()
        return tracemalloc.get_traced_memory()[1]
    finally:
        tracemalloc.stop()


def validate(frame, vertices: int, triangles: int) -> None:
    assert frame.geometry.positions.shape == (vertices, 3)
    assert frame.geometry.triangles.shape == (triangles, 3)


def run(side: int, frame_count: int, repeats: int) -> dict[str, float | int]:
    points, faces = grid(side)
    with tempfile.TemporaryDirectory(prefix="open4d-io-bench-") as directory:
        root = Path(directory)
        ply, obj, frames = root / "frame.ply", root / "frame.obj", root / "frames"
        write_ply(ply, points, faces)
        write_obj(obj, points, faces)
        frames.mkdir()
        for index in range(frame_count):
            os.link(ply, frames / f"frame_{index:06d}.ply")

        def discover():
            sequence = open_sequence(frames)
            assert len(sequence) == frame_count

        def inspect():
            info = inspect_sequence(frames)
            assert info.frame_count == frame_count and info.format == "ply"

        sequence = open_sequence(frames)
        selected = np.linspace(0, frame_count - 1, min(frame_count, 16), dtype=int)

        def metadata():
            assert len(sequence.timestamps) == frame_count

        def decode(path: Path):
            validate(open_sequence(path)[0], len(points), len(faces))

        def decode_optional(path: Path):
            geometry = open_sequence(path)[0].geometry
            assert geometry.positions.ndim == 2 and geometry.positions.shape[1] == 3
            assert np.isfinite(geometry.positions).all()
            assert geometry.triangles.shape == (len(faces), 3)
            assert geometry.triangles.max() < len(geometry.positions)
            if path.suffix != ".stl":
                assert len(geometry.positions) == len(points)

        def random_access():
            for index in selected:
                validate(sequence[int(index)], len(points), len(faces))

        def iterate():
            count = 0
            for frame in sequence:
                validate(frame, len(points), len(faces))
                count += 1
            assert count == frame_count

        decode(ply)  # Warm parser/import caches; fixture generation is never timed.
        results: dict[str, float | int] = {
            "vertices_per_frame": len(points),
            "triangles_per_frame": len(faces),
            "frames": frame_count,
            "directory_discovery_ms": timed(discover, repeats) * 1000,
            "inspect_ms": timed(inspect, repeats) * 1000,
            "timestamps_ms": timed(metadata, repeats) * 1000,
            "obj_decode_ms": timed(lambda: decode(obj), repeats) * 1000,
            "ply_decode_ms": timed(lambda: decode(ply), repeats) * 1000,
            "random_16_frames_ms": timed(random_access, repeats) * 1000,
            "full_iteration_s": timed(iterate, repeats),
            "discovery_peak_bytes": peak_bytes(discover),
            "one_ply_peak_bytes": peak_bytes(lambda: decode(ply)),
        }
        results["full_iteration_frames_per_s"] = frame_count / results["full_iteration_s"]
        results["full_iteration_mib_per_s"] = (
            ply.stat().st_size * frame_count / results["full_iteration_s"] / 2**20
        )
        try:
            import trimesh
        except ImportError:
            results["trimesh_formats_measured"] = 0
        else:
            mesh = trimesh.Trimesh(vertices=points, faces=faces, process=False)
            optional_paths = {}
            for suffix in ("off", "stl"):
                path = root / f"frame.{suffix}"
                mesh.export(path)
                optional_paths[suffix] = path
            scene = trimesh.Scene(mesh)
            glb = root / "frame.glb"
            glb.write_bytes(scene.export(file_type="glb"))
            optional_paths["glb"] = glb
            gltf_files = scene.export(file_type="gltf")
            for name, content in gltf_files.items():
                (root / name).write_bytes(content)
            optional_paths["gltf"] = next(root.glob("*.gltf"))
            for suffix, path in optional_paths.items():
                results[f"{suffix}_decode_ms"] = timed(
                    lambda path=path: decode_optional(path), repeats
                ) * 1000
            results["trimesh_formats_measured"] = len(optional_paths)
        return results


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--side", type=int, default=64)
    parser.add_argument("--frames", type=int, default=120)
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    if args.side < 2 or min(args.frames, args.repeats) < 1:
        parser.error("side must be at least 2; frames and repeats must be positive")
    results = run(args.side, args.frames, args.repeats)
    if args.json:
        print(json.dumps(results, indent=2, sort_keys=True))
    else:
        for name, value in results.items():
            print(f"{name:32} {value:.3f}" if isinstance(value, float)
                  else f"{name:32} {value}")


if __name__ == "__main__":
    main()
