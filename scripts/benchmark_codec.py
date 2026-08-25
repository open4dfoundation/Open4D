#!/usr/bin/env python3
"""Benchmark a validated Open4D sequence encode/decode round trip."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import tempfile
import time
import tracemalloc

import numpy as np

from open4d import Frame, MemoryFrameProvider, Sequence, TriangleMesh
from open4d.codec import available_codecs, decode_sequence, encode_sequence
from open4d.io import open_sequence

_FIELDS = ("positions", "triangles", "colors", "normals", "texture_coordinates")


def synthetic(side: int, frames: int) -> Sequence:
    axis = np.linspace(-1, 1, side, dtype=np.float32)
    x, y = np.meshgrid(axis, axis)
    cells = np.arange((side - 1) ** 2, dtype=np.uint32)
    row, column = np.divmod(cells, side - 1)
    corner = row * side + column
    triangles = np.column_stack(
        (corner, corner + 1, corner + side, corner + 1,
         corner + side + 1, corner + side)
    ).reshape(-1, 3)
    values = []
    for index in range(frames):
        z = np.sin(3 * x + index / 10).ravel() / 20
        positions = np.column_stack((x.ravel(), y.ravel(), z)).astype(np.float32)
        values.append(Frame(
            index, index / 30, TriangleMesh(positions, triangles)
        ))
    return Sequence(MemoryFrameProvider(values))


def timed(function):
    start = time.perf_counter()
    result = function()
    return result, time.perf_counter() - start


def peak_bytes(function, cleanup=None):
    tracemalloc.start()
    try:
        result = function()
        peak = tracemalloc.get_traced_memory()[1]
        if cleanup is not None:
            cleanup(result)
        return peak
    finally:
        tracemalloc.stop()


def _arrays_exact(left, right) -> bool:
    if left is None or right is None:
        return left is None and right is None
    return (
        left.shape == right.shape
        and left.dtype == right.dtype
        and np.array_equal(left, right)
    )


def _geometry_exact(left: TriangleMesh, right: TriangleMesh) -> bool:
    return (
        all(_arrays_exact(getattr(left, name), getattr(right, name)) for name in _FIELDS)
        and left.attributes.keys() == right.attributes.keys()
        and all(
            _arrays_exact(left.attributes[name], right.attributes[name])
            for name in left.attributes
        )
    )


def _surface_points(mesh: TriangleMesh, count: int = 1024) -> np.ndarray:
    vertices = np.asarray(mesh.positions, dtype=np.float64)
    faces = np.asarray(mesh.triangles, dtype=np.int64)
    if not len(vertices) or not len(faces):
        raise AssertionError("decoded frames must contain vertices and triangles")
    corners = vertices[faces]
    areas = np.linalg.norm(
        np.cross(corners[:, 1] - corners[:, 0], corners[:, 2] - corners[:, 0]),
        axis=1,
    )
    if not np.any(areas):
        raise AssertionError("decoded frames must contain nondegenerate triangles")
    choices = np.searchsorted(
        np.cumsum(areas) / areas.sum(), (np.arange(count) + .5) / count
    ).clip(max=len(faces) - 1)
    sample = np.arange(count, dtype=np.float64)
    first = np.mod(sample * 0.7548776662466927, 1.0)
    second = np.mod(sample * 0.5698402909980532, 1.0)
    reflected = first + second > 1
    first[reflected], second[reflected] = (
        1 - first[reflected], 1 - second[reflected]
    )
    selected = corners[choices]
    return (
        selected[:, 0]
        + first[:, None] * (selected[:, 1] - selected[:, 0])
        + second[:, None] * (selected[:, 2] - selected[:, 0])
    )


def _nearest(left: np.ndarray, right: np.ndarray) -> np.ndarray:
    distances = []
    for start in range(0, len(left), 256):
        difference = left[start:start + 256, None] - right[None]
        distances.append(np.sqrt(np.square(difference).sum(2).min(1)))
    return np.concatenate(distances)


def _surface_errors(left: TriangleMesh, right: TriangleMesh) -> tuple[float, float]:
    if _arrays_exact(left.positions, right.positions) and _arrays_exact(
        left.triangles, right.triangles
    ):
        return 0.0, 0.0
    left_points, right_points = _surface_points(left), _surface_points(right)
    distances = np.concatenate((
        _nearest(left_points, right_points), _nearest(right_points, left_points)
    ))
    return float(np.sqrt(np.mean(np.square(distances)))), float(distances.max())


def run(
    source: Sequence, output: Path, codec: str = "npz", *,
    encode_options: dict | None = None, decode_options: dict | None = None,
) -> dict[str, str | float | int | bool | None]:
    # Keep reference parsing outside every measurement. Encode still consumes
    # the supplied Sequence normally; decode timing reads only the artifact.
    expected_frames = tuple(source)
    info = next(item for item in available_codecs() if item.id == codec)
    encode = lambda: encode_sequence(
        source, output, codec=codec, overwrite=True, **(encode_options or {})
    )
    artifact, encode_s = timed(encode)
    encode_peak = peak_bytes(encode)
    decoded, open_s = timed(
        lambda: decode_sequence(artifact, **(decode_options or {}))
    )
    open_peak = peak_bytes(
        lambda: decode_sequence(artifact, **(decode_options or {})),
        cleanup=lambda sequence: sequence.close(),
    )
    assert decoded.metadata == source.metadata
    if info.lossless:
        assert decoded.topology is source.topology
        assert decoded.has_constant_vertex_count == source.has_constant_vertex_count
        assert decoded.has_vertex_correspondence == source.has_vertex_correspondence

    def decode_and_validate(sequence):
        vertices = triangles = 0
        squared_error = maximum_error = 0.0
        compared_values = 0
        surface_squared = surface_maximum = 0.0
        exact = True
        for expected, actual in zip(expected_frames, sequence, strict=True):
            assert actual.frame_index == expected.frame_index
            assert actual.timestamp == expected.timestamp
            assert actual.metadata == expected.metadata
            assert len(actual.geometry.positions) and len(actual.geometry.triangles)
            exact &= _geometry_exact(actual.geometry, expected.geometry)
            if actual.geometry.positions.shape == expected.geometry.positions.shape:
                error = actual.geometry.positions - expected.geometry.positions
                squared_error += float(np.square(error).sum())
                compared_values += error.size
                maximum_error = max(maximum_error, float(np.abs(error).max()))
            surface_rms, surface_max = _surface_errors(
                expected.geometry, actual.geometry
            )
            surface_squared += surface_rms ** 2
            surface_maximum = max(surface_maximum, surface_max)
            vertices += len(actual.geometry.positions)
            triangles += len(actual.geometry.triangles)
        if info.lossless and not exact:
            raise AssertionError(f"lossless codec {codec} changed decoded arrays")
        rms = (squared_error / compared_values) ** 0.5 if compared_values else None
        surface_rms = (surface_squared / len(expected_frames)) ** .5
        return (
            vertices, triangles, exact, rms,
            maximum_error if compared_values else None,
            surface_rms, surface_maximum,
        )

    result, validate_s = timed(lambda: decode_and_validate(decoded))
    (
        vertices, triangles, exact, rms_error, maximum_error,
        surface_rms_error, surface_max_error,
    ) = result
    decoded.close()
    decode_s = open_s + validate_s

    def decode_consume_close():
        measured_sequence = decode_sequence(artifact, **(decode_options or {}))
        try:
            return sum(
                len(frame.geometry.positions) + len(frame.geometry.triangles)
                for frame in measured_sequence
            )
        finally:
            measured_sequence.close()

    decode_peak = peak_bytes(decode_consume_close)
    return {
        "codec": codec,
        "frames": len(source),
        "vertices": vertices,
        "triangles": triangles,
        "artifact_bytes": artifact.stat().st_size,
        "encode_s": encode_s,
        "decode_open_ms": open_s * 1000,
        "decode_validate_s": validate_s,
        "decode_all_s": decode_s,
        "encode_frames_per_s": len(source) / encode_s,
        "decode_frames_per_s": len(source) / decode_s,
        "encode_peak_bytes": encode_peak,
        "decode_open_peak_bytes": open_peak,
        "decode_all_peak_bytes": decode_peak,
        "exact": exact,
        "position_rms_error": rms_error,
        "position_max_error": maximum_error,
        "surface_rms_error": surface_rms_error,
        "surface_max_error": surface_max_error,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, help="real frame directory")
    parser.add_argument("--frames", type=int, default=30)
    parser.add_argument("--side", type=int, default=64)
    parser.add_argument(
        "--codecs", default="npz",
        help="comma-separated codec ids (for example raw,deflate,bzip2,lzma,rle)",
    )
    parser.add_argument("--json", action="store_true")
    parser.add_argument(
        "--options", type=Path,
        help="JSON mapping codec ids to encode/decode option objects",
    )
    args = parser.parse_args()
    if args.frames < 1 or args.side < 2:
        parser.error("frames must be positive and side must be at least 2")
    lazy = (
        open_sequence(args.source, fps=30)[: args.frames]
        if args.source else synthetic(args.side, args.frames)
    )
    frames, load_s = timed(lambda: tuple(lazy))
    load_peak = peak_bytes(lambda: tuple(lazy))
    sequence = Sequence(MemoryFrameProvider(
        frames,
        metadata=lazy.metadata,
        topology=lazy.topology,
        has_constant_vertex_count=lazy.has_constant_vertex_count,
        has_vertex_correspondence=lazy.has_vertex_correspondence,
        allow_nonmonotonic_timestamps=lazy.allow_nonmonotonic_timestamps,
    ))
    codecs = tuple(dict.fromkeys(
        item.strip() for item in args.codecs.split(",") if item.strip()
    ))
    if not codecs:
        parser.error("--codecs must name at least one codec")
    suffixes = {info.id: info.suffixes[0] for info in available_codecs()}
    profiles = json.loads(args.options.read_text()) if args.options else {}
    with tempfile.TemporaryDirectory(prefix="open4d-codec-bench-") as directory:
        results = [
            run(
                sequence, Path(directory) / f"sequence-{codec}{suffixes[codec]}", codec,
                encode_options=profiles.get(codec, {}).get("encode"),
                decode_options=profiles.get(codec, {}).get("decode"),
            )
            for codec in codecs
        ]
    report = {"source_load_s": load_s, "source_load_peak_bytes": load_peak,
              "results": results}
    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True, allow_nan=False))
    else:
        print(f"source_load_s                  {load_s:.3f}")
        print("codec      bytes       encode_s   decode_s   exact  surface_rms")
        for result in results:
            print(
                f"{result['codec']:<10} {result['artifact_bytes']:<11} "
                f"{result['encode_s']:<10.3f} {result['decode_all_s']:<10.3f} "
                f"{str(result['exact']):<6} {result['surface_rms_error']:.3g}"
            )


if __name__ == "__main__":
    main()
