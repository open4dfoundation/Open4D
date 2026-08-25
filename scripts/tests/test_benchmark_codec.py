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
    assert result["decode_validate_s"] == 3
    assert result["decode_all_s"] == 5
    assert result["decode_frames_per_s"] == pytest.approx(2 / 5)


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
