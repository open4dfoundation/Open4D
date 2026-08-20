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
        values.append(Frame(index, index / 30, TriangleMesh(positions, triangles)))
    return Sequence(MemoryFrameProvider(values))


def measured(function):
    tracemalloc.start()
    start = time.perf_counter()
    try:
        result = function()
        return result, time.perf_counter() - start, tracemalloc.get_traced_memory()[1]
    finally:
        tracemalloc.stop()


def run(
    source: Sequence, output: Path, codec: str = "npz", *,
    encode_options: dict | None = None, decode_options: dict | None = None,
) -> dict[str, str | float | int]:
    # Keep reference parsing outside every measurement. Encode still consumes
    # the supplied Sequence normally; decode timing reads only the artifact.
    expected_frames = tuple(source)
    info = next(item for item in available_codecs() if item.id == codec)
    artifact, encode_s, encode_peak = measured(
        lambda: encode_sequence(
            source, output, codec=codec, overwrite=True, **(encode_options or {})
        )
    )
    decoded, open_s, open_peak = measured(
        lambda: decode_sequence(artifact, **(decode_options or {}))
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
        exact = True
        for expected, actual in zip(expected_frames, sequence, strict=True):
            assert actual.frame_index == expected.frame_index
            assert actual.timestamp == expected.timestamp
            assert actual.metadata == expected.metadata
            for name in _FIELDS:
                left = getattr(actual.geometry, name)
                right = getattr(expected.geometry, name)
                exact &= (left is None and right is None) or (
                    left is not None and right is not None and np.array_equal(left, right)
                )
            exact &= actual.geometry.attributes.keys() == expected.geometry.attributes.keys()
            if actual.geometry.positions.shape == expected.geometry.positions.shape:
                error = actual.geometry.positions - expected.geometry.positions
                squared_error += float(np.square(error).sum())
                compared_values += error.size
                maximum_error = max(maximum_error, float(np.abs(error).max()))
            vertices += len(actual.geometry.positions)
            triangles += len(actual.geometry.triangles)
        if info.lossless and not exact:
            raise AssertionError(f"lossless codec {codec} changed decoded arrays")
        rms = (squared_error / compared_values) ** 0.5 if compared_values else float("nan")
        return vertices, triangles, exact, rms, maximum_error

    result, decode_s, decode_peak = measured(lambda: decode_and_validate(decoded))
    vertices, triangles, exact, rms_error, maximum_error = result
    decoded.close()
    return {
        "codec": codec,
        "frames": len(source),
        "vertices": vertices,
        "triangles": triangles,
        "artifact_bytes": artifact.stat().st_size,
        "encode_s": encode_s,
        "decode_open_ms": open_s * 1000,
        "decode_all_s": decode_s,
        "encode_frames_per_s": len(source) / encode_s,
        "decode_frames_per_s": len(source) / decode_s,
        "encode_peak_bytes": encode_peak,
        "decode_open_peak_bytes": open_peak,
        "decode_all_peak_bytes": decode_peak,
        "exact": exact,
        "position_rms_error": rms_error,
        "position_max_error": maximum_error,
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
    frames, load_s, load_peak = measured(lambda: tuple(lazy))
    sequence = Sequence(MemoryFrameProvider(
        frames,
        metadata=lazy.metadata,
        topology=lazy.topology,
        has_constant_vertex_count=lazy.has_constant_vertex_count,
        has_vertex_correspondence=lazy.has_vertex_correspondence,
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
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print(f"source_load_s                  {load_s:.3f}")
        print("codec      bytes       encode_s   decode_s   exact  position_rms")
        for result in results:
            print(
                f"{result['codec']:<10} {result['artifact_bytes']:<11} "
                f"{result['encode_s']:<10.3f} {result['decode_all_s']:<10.3f} "
                f"{str(result['exact']):<6} {result['position_rms_error']:.3g}"
            )


if __name__ == "__main__":
    main()
