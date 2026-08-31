"""The CDF is the artefact the whole first stage is judged on.

Its one number that matters -- the share of voxels below 0.025 -- comes out of
a histogram rather than a sort, so the binning has to be exact at that
threshold and the pooling has to mean what it claims.
"""
from __future__ import annotations

import numpy as np
import pytest

from nevo import cdf as cdf_module


def test_fraction_below_is_exact_at_the_paper_threshold():
    """0.025 lands on a bin edge by construction, so no sample is misbinned."""
    rng = np.random.default_rng(0)
    values = rng.random(200_000)
    accumulator = cdf_module.ImportanceAccumulator()
    accumulator.add(values)
    expected = float((values < cdf_module.PAPER_THRESHOLD).mean())
    assert accumulator.fraction_below(cdf_module.PAPER_THRESHOLD) == pytest.approx(
        expected, abs=1e-9
    )


def test_default_bins_put_an_edge_on_the_threshold():
    edge = cdf_module.PAPER_THRESHOLD * cdf_module.DEFAULT_BINS
    assert edge == int(edge)


def test_accumulation_is_order_independent():
    rng = np.random.default_rng(1)
    chunks = [rng.random((7, 13)) for _ in range(5)]
    first = cdf_module.ImportanceAccumulator()
    second = cdf_module.ImportanceAccumulator()
    for chunk in chunks:
        first.add(chunk)
    for chunk in reversed(chunks):
        second.add(chunk)
    assert np.array_equal(first.counts, second.counts)
    assert first.total == second.total


def test_quantiles_and_mean_track_the_data():
    values = np.linspace(0.0, 1.0, 100_001)
    accumulator = cdf_module.ImportanceAccumulator()
    accumulator.add(values)
    assert accumulator.mean == pytest.approx(0.5, abs=1e-6)
    assert accumulator.quantile(0.5) == pytest.approx(0.5, abs=1e-4)
    assert accumulator.quantile(0.9) == pytest.approx(0.9, abs=1e-4)


def test_non_finite_scores_are_refused():
    accumulator = cdf_module.ImportanceAccumulator()
    with pytest.raises(ValueError):
        accumulator.add(np.asarray([0.1, np.nan]))


def test_curve_is_monotone_and_spans_the_distribution():
    rng = np.random.default_rng(2)
    accumulator = cdf_module.ImportanceAccumulator()
    accumulator.add(rng.random(50_000) ** 3)
    curve = accumulator.curve(points=128)
    importance = np.asarray(curve["importance"])
    probability = np.asarray(curve["cdf"])
    assert np.all(np.diff(importance) >= -1e-12)
    assert np.all(np.diff(probability) >= -1e-12)
    assert probability[-1] == pytest.approx(1.0, abs=1e-6)


def test_empty_accumulator_reports_nan_rather_than_dividing_by_zero():
    accumulator = cdf_module.ImportanceAccumulator()
    assert np.isnan(accumulator.fraction_below(0.025))
    assert np.isnan(accumulator.mean)
    assert accumulator.curve() == {"importance": [], "cdf": []}


def test_excluding_empty_blocks_is_the_callers_job_and_changes_the_answer():
    """Empty blocks are never in the bitstream, so counting them would inflate
    the removable fraction for free. `importance_cdf.py` subsets to the
    occupancy union before feeding the accumulator; this pins what that is
    worth."""
    scores = np.zeros((4, 10))
    scores[:, :5] = 0.9
    occupancy = np.zeros(10, dtype=bool)
    occupancy[:5] = True

    occupied_only = cdf_module.ImportanceAccumulator()
    occupied_only.add(scores[:, occupancy])
    everything = cdf_module.ImportanceAccumulator()
    everything.add(scores)

    assert occupied_only.total == 4 * 5
    assert occupied_only.fraction_below(0.025) == 0.0
    assert everything.fraction_below(0.025) == 0.5


def test_per_frame_pooling_takes_the_best_viewport():
    """Per-viewport asks "can this fetch skip it"; per-frame asks "is it ever
    worth sending". The second is always the smaller fraction."""
    scores = np.asarray([[0.0, 0.5], [0.8, 0.0]])
    per_viewport = cdf_module.ImportanceAccumulator()
    per_viewport.add(scores)
    per_frame = cdf_module.ImportanceAccumulator()
    per_frame.add(scores.max(axis=0))

    assert per_viewport.fraction_below(0.025) == 0.5
    assert per_frame.fraction_below(0.025) == 0.0
    assert per_frame.total == 2


def test_never_hit_fraction_counts_only_the_zero_bin():
    """Voxels no ray touched are the free part of the removable fraction, so
    they have to be separable from voxels that were merely dim."""
    accumulator = cdf_module.ImportanceAccumulator()
    accumulator.add(np.concatenate([np.zeros(300), np.full(100, 0.001), np.full(600, 0.5)]))
    assert accumulator.never_hit_fraction == pytest.approx(0.3)
    assert accumulator.fraction_below(0.025) == pytest.approx(0.4)


def test_never_hit_fraction_is_nan_when_empty():
    assert np.isnan(cdf_module.ImportanceAccumulator().never_hit_fraction)
