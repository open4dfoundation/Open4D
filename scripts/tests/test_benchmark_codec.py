"""Regression tests for benchmark validation and measurement boundaries."""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import pytest

import benchmark_codec
from open4d import TriangleMesh

pytestmark = pytest.mark.cpu


def test_exactness_detects_custom_attribute_value_shape_and_dtype_changes():
    reference = TriangleMesh(
        [[0.0, 0, 0], [1.0, 0, 0], [0, 1.0, 0]], [[0, 1, 2]],
        attributes={"label": np.array([1, 2, 3], dtype=np.int16)},
    )
    for changed in (
        np.array([1, 9, 3], dtype=np.int16),
        np.array([[1], [2], [3]], dtype=np.int16),
    ):
        candidate = TriangleMesh(
            reference.positions, reference.triangles, attributes={"label": changed}
        )
        assert not benchmark_codec._geometry_exact(reference, candidate)
    assert not benchmark_codec._arrays_exact(
        np.array([1, 2, 3], dtype=np.int16),
        np.array([1, 2, 3], dtype=np.int32),
    )


def test_wall_clock_timing_runs_with_memory_tracing_disabled():
    assert not benchmark_codec.tracemalloc.is_tracing()

    result, _ = benchmark_codec.timed(
        lambda: not benchmark_codec.tracemalloc.is_tracing()
    )

    assert result is True


def test_general_synthetic_fixture_has_no_codec_specific_attributes():
    assert all(not frame.geometry.attributes for frame in benchmark_codec.synthetic(4, 2))


def test_surface_error_is_finite_for_topology_changing_geometry():
    reference = TriangleMesh(
        [[0.0, 0, 0], [1, 0, 0], [0, 1, 0]], [[0, 1, 2]]
    )
    remeshed = TriangleMesh(
        [[0.0, 0, 0], [1, 0, 0], [0, 1, 0], [.5, .5, 0]],
        [[0, 1, 3], [0, 3, 2]],
    )

    rms, maximum = benchmark_codec._surface_errors(reference, remeshed)

    assert np.isfinite(rms) and np.isfinite(maximum)
    json.dumps({"rms": rms, "maximum": maximum}, allow_nan=False)


def test_decode_throughput_includes_eager_open_time(tmp_path, monkeypatch):
    durations = iter((1.0, 2.0, 3.0))

    def fixed_timed(function):
        return function(), next(durations)

    monkeypatch.setattr(benchmark_codec, "timed", fixed_timed)
    monkeypatch.setattr(benchmark_codec, "peak_bytes", lambda function, cleanup=None: 0)
    source = benchmark_codec.synthetic(3, 2)

    result = benchmark_codec.run(source, Path(tmp_path) / "take.o4d")

    assert result["decode_open_ms"] == 2000
    assert result["decode_validate_s"] >= 0
    assert result["decode_all_s"] == 5
    assert result["decode_frames_per_s"] == pytest.approx(2 / 5)


def test_decode_timing_excludes_surface_validation(tmp_path, monkeypatch):
    measuring = False
    real_timed = benchmark_codec.timed
    real_surface = benchmark_codec._surface_errors

    def marked_timed(function):
        nonlocal measuring
        measuring = True
        try:
            return real_timed(function)
        finally:
            measuring = False

    # Validation has its own explicit timer, outside timed decode consumption.
    def check(left, right):
        assert not measuring
        return real_surface(left, right)

    monkeypatch.setattr(benchmark_codec, "timed", marked_timed)
    monkeypatch.setattr(benchmark_codec, "_surface_errors", check)
    benchmark_codec.run(benchmark_codec.synthetic(3, 2), tmp_path / "test.o4d")


def test_decode_peak_memory_excludes_surface_validation(tmp_path, monkeypatch):
    inside_peak_measurement = False
    real_peak_bytes = benchmark_codec.peak_bytes
    real_surface_errors = benchmark_codec._surface_errors

    def marked_peak(function, cleanup=None):
        nonlocal inside_peak_measurement
        inside_peak_measurement = True
        try:
            return real_peak_bytes(function, cleanup)
        finally:
            inside_peak_measurement = False

    def checked_surface_errors(left, right):
        assert not inside_peak_measurement
        return real_surface_errors(left, right)

    monkeypatch.setattr(benchmark_codec, "peak_bytes", marked_peak)
    monkeypatch.setattr(benchmark_codec, "_surface_errors", checked_surface_errors)

    result = benchmark_codec.run(
        benchmark_codec.synthetic(3, 2), Path(tmp_path) / "take.o4d"
    )

    assert result["decode_all_peak_bytes"] > 0


def test_benchmark_does_not_compare_unrelated_vertex_indices(tmp_path, monkeypatch):
    from open4d import Frame, MemoryFrameProvider, Sequence
    from open4d.codec._npz import NumPyZipCodec

    source = benchmark_codec.synthetic(3, 2)
    codec = NumPyZipCodec()
    codec.lossless = False
    monkeypatch.setitem(benchmark_codec._BASELINES, "reordered", codec)

    def decode(*args, **kwargs):
        frames = []
        for frame in source:
            mesh = frame.geometry
            frames.append(Frame(frame.frame_index, frame.timestamp, TriangleMesh(
                mesh.positions[::-1], len(mesh.positions) - 1 - mesh.triangles,
            ), metadata=frame.metadata))
        return Sequence(MemoryFrameProvider(frames, metadata=source.metadata,
                                            has_vertex_correspondence=False))

    monkeypatch.setattr(benchmark_codec, "decode_sequence", decode)
    result = benchmark_codec.run(source, tmp_path / "test.o4d", codec="reordered")
    assert result["position_rms_error"] is None
    assert result["position_max_error"] is None
    assert result["surface_rms_error"] == 0


def test_benchmark_closes_decoders_after_validation_failure(tmp_path, monkeypatch):
    from open4d import MemoryFrameProvider, Sequence

    opened = []
    source = benchmark_codec.synthetic(3, 2)

    def decode(*args, **kwargs):
        value = Sequence(MemoryFrameProvider(tuple(source), metadata={"bad": True}))
        opened.append(value)
        return value

    monkeypatch.setattr(benchmark_codec, "decode_sequence", decode)
    with pytest.raises(AssertionError):
        benchmark_codec.run(source, tmp_path / "test.o4d")
    assert all(value.closed for value in opened)
    assert not source.closed


def test_benchmark_cli_closes_the_imported_source(tmp_path, monkeypatch, capsys):
    import sys

    source = benchmark_codec.synthetic(3, 2)
    monkeypatch.setattr(benchmark_codec, "open_sequence", lambda *a, **kw: source)
    monkeypatch.setattr(sys, "argv", ["benchmark_codec.py", "--source", str(tmp_path),
                                      "--frames", "1", "--json"])
    benchmark_codec.main()
    assert source.closed
    assert json.loads(capsys.readouterr().out)["results"][0]["frames"] == 1
