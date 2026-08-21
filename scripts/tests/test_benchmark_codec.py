"""Regression tests for benchmark validation and measurement boundaries."""

from __future__ import annotations

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
