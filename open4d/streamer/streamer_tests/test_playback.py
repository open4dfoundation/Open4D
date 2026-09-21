"""Playing over a link, and what the buffer does about it.

Driven on a virtual clock, so every case here is exact rather than timed. The
interesting assertions are about *which* failure happens -- a stall, a freeze,
or neither -- because that is what the model exists to tell apart and what a
score built on it depends on.
"""
from __future__ import annotations

import pytest

from streamer.link import Link, Trace
from streamer.playback import (
    Buffer,
    Playback,
    RateEstimate,
    buffer_budget,
    render_table,
)
from streamer.policy import Rung

pytestmark = pytest.mark.cpu

LOW, MEDIUM, HIGH = 728_000, 2_376_000, 8_040_000
QUALITY = {"low": 41.6, "medium": 45.0, "default": 47.8}


def ladder(clip: str):
    return [
        Rung(clip=clip, variant=name, bits_per_second=rate,
             quality={"psnr": QUALITY[name or "default"]})
        for name, rate in (("low", LOW), ("medium", MEDIUM), (None, HIGH))
    ]


def ladders(count: int):
    return [ladder(f"pane{index}") for index in range(count)]


def a_link(mbit: float, *, latency: float = 0.02):
    # A frozen clock: the model advances time itself, and a real one would let
    # the machine's scheduling leak into the answer.
    return Link(capacity=mbit * 1e6, latency=latency, clock=lambda: 0.0)


# ---------------------------------------------------------------- the buffer ---


def test_filling_buys_playback_time():
    buffer = Buffer(clip="c")
    buffer.fill(1 / 30, 45.0)
    assert buffer.seconds == pytest.approx(1 / 30)
    assert buffer.frames == 1
    assert buffer.mean_quality == pytest.approx(45.0)


def test_draining_consumes_it():
    buffer = Buffer(clip="c", seconds=2.0)
    assert buffer.drain(0.5) == 0.0
    assert buffer.seconds == pytest.approx(1.5)
    assert buffer.stalled == 0.0


def test_an_empty_buffer_stalls_for_the_remainder_only():
    """Charging the whole interval would make the penalty depend on how finely
    time was stepped, which would make the score an artefact of the loop."""
    buffer = Buffer(clip="c", seconds=0.2)
    short = buffer.drain(0.5)
    assert short == pytest.approx(0.3)
    assert buffer.stalled == pytest.approx(0.3)
    assert buffer.stalls == 1


def test_a_continuing_stall_is_one_stall():
    buffer = Buffer(clip="c")
    buffer.drain(1.0)
    buffer.drain(1.0)
    assert buffer.stalls == 1
    assert buffer.stalled == pytest.approx(2.0)


def test_a_refill_ends_the_stall_so_the_next_one_counts_again():
    buffer = Buffer(clip="c")
    buffer.drain(1.0)
    buffer.fill(1.0, 40.0)
    buffer.drain(2.0)
    assert buffer.stalls == 2


def test_a_frozen_buffer_does_not_drain_or_stall():
    """A freeze is a decision, not a failure: the pane holds its last frame
    while the others play, and charging it as a stall would make the two
    indistinguishable in the score."""
    buffer = Buffer(clip="c", seconds=1.0, freezing=True)
    assert buffer.drain(2.0) == 0.0
    assert buffer.seconds == pytest.approx(1.0)
    assert buffer.stalled == 0.0
    assert buffer.frozen == pytest.approx(2.0)
    assert buffer.freezes == 1


def test_quality_is_weighted_by_the_time_it_was_on_screen():
    """An unweighted mean would score a one-frame excursion the same as a
    minute of it."""
    buffer = Buffer(clip="c")
    for _ in range(9):
        buffer.fill(1.0, 40.0)
    buffer.fill(1.0, 50.0)
    assert buffer.mean_quality == pytest.approx(41.0)


def test_an_unplayed_buffer_has_no_quality():
    assert Buffer(clip="c").mean_quality == 0.0


def test_a_rung_change_is_counted_but_the_first_choice_is_not():
    buffer = Buffer(clip="c")
    buffer.use("low")                  # arriving somewhere is not a switch
    assert buffer.switches == 0
    buffer.use("medium")
    buffer.use("medium")               # unchanged
    buffer.use("low")
    assert buffer.switches == 2


# -------------------------------------------------------------- the estimate ---


def test_the_estimate_starts_at_its_first_sample():
    estimate = RateEstimate()
    estimate.record(1_000_000, 1.0)
    assert estimate.bits_per_second == pytest.approx(8_000_000)


def test_the_estimate_follows_a_change():
    estimate = RateEstimate(half_life=2)
    estimate.record(1_000_000, 1.0)
    for _ in range(8):
        estimate.record(1_000_000, 8.0)
    assert 1_000_000 <= estimate.bits_per_second < 2_500_000


def test_a_zero_duration_sample_is_ignored():
    estimate = RateEstimate()
    estimate.record(1_000, 0.0)
    assert estimate.samples == 0


# ---------------------------------------------------------------- the budget ---


def test_a_full_buffer_spends_over_the_estimate():
    assert buffer_budget(10e6, 6.0, 4.0) == pytest.approx(15e6)   # clamped at 1.5


def test_an_empty_buffer_spends_under_it():
    assert buffer_budget(10e6, 0.0, 4.0) == pytest.approx(5e6)    # clamped at 0.5


def test_the_clamp_stops_an_empty_buffer_choosing_the_floor_for_ever():
    """Unclamped, occupancy near zero would scale the budget to nothing and the
    client would never climb back out."""
    assert buffer_budget(10e6, 1e-9, 4.0) == pytest.approx(5e6)


def test_no_target_means_no_scaling():
    assert buffer_budget(10e6, 0.0, 0.0) == pytest.approx(10e6)


# --------------------------------------------------------------- the playback ---


def test_a_fast_link_plays_everything_at_its_best():
    report = Playback(ladders(4), a_link(200), target_buffer=2.0).run(20.0)
    assert report.stalled == 0.0
    assert report.frozen == 0.0
    assert all(buffer.rung == "default" for buffer in report.buffers)
    assert report.mean_quality > 46.0


def test_a_link_below_the_floor_freezes_rather_than_stalling_everything():
    """The trade the model exists to make. Four panes need 2.9 Mbit/s at their
    cheapest; at 1.5 some have to go, and freezing some so the rest play beats
    starving all four into a stall."""
    floor = 4 * LOW
    report = Playback(ladders(4), a_link(floor * 0.5 / 1e6),
                      target_buffer=2.0).run(20.0)
    assert report.frozen > 0
    assert report.stalled == 0.0
    playing = [b for b in report.buffers if b.frozen == 0]
    assert 0 < len(playing) < 4


def test_a_freeze_is_sticky():
    """A chooser reconsidering from scratch each interval would thaw one pane
    and freeze another, and the viewer would see panes flickering."""
    report = Playback(ladders(6), a_link(6 * LOW * 0.6 / 1e6),
                      target_buffer=2.0).run(30.0)
    frozen = [buffer for buffer in report.buffers if buffer.frozen > 0]
    assert frozen
    # One continuous freeze each, not a series of them.
    assert all(buffer.freezes == 1 for buffer in frozen)


def test_a_richer_link_scores_better():
    scores = []
    for mbit in (2, 6, 20, 100):
        scores.append(Playback(ladders(4), a_link(mbit),
                               target_buffer=2.0).run(20.0).score)
    assert scores == sorted(scores)


def test_deciding_less_often_reduces_churn():
    """The reason the interval exists, at the capacity where it shows.

    14 Mbit/s over eight panes is a 1.75 Mbit/s share, which the [0.5, 1.5]
    occupancy scaling stretches across the 2.376 Mbit/s middle rung -- so every
    frame's jitter is a coin toss. Measured: 1224 switches per-frame against 24
    at four seconds.

    The capacity is chosen deliberately. At 19 Mbit/s the share already clears
    that boundary and the interval changes nothing, which is how this test was
    first written and why it passed for the wrong reason.
    """
    often = Playback(ladders(8), a_link(14), target_buffer=3.0,
                     decision_interval=0.033).run(30.0)
    rarely = Playback(ladders(8), a_link(14), target_buffer=3.0,
                      decision_interval=4.0).run(30.0)
    assert often.switches > 20 * rarely.switches


def test_a_share_clear_of_a_boundary_does_not_churn():
    """The other half of the same fact: the interval costs nothing where the
    budget cannot reach a boundary, so it is a default rather than a knob."""
    often = Playback(ladders(8), a_link(19), target_buffer=3.0,
                     decision_interval=0.033).run(30.0)
    rarely = Playback(ladders(8), a_link(19), target_buffer=3.0,
                      decision_interval=4.0).run(30.0)
    assert often.switches == rarely.switches


def test_a_cold_start_bootstraps_instead_of_freezing_everything():
    """The deadlock this guards: with no estimate the rate is zero, so the
    deficit loop freezes every clip, a frozen clip is never fetched, and an
    estimate only comes from a fetch. It produced 30 seconds frozen and mean
    quality 0.0, with no error anywhere."""
    report = Playback(ladders(3), a_link(20), target_buffer=2.0).run(10.0,
                                                                     warm=False)
    assert report.frozen == 0.0
    assert report.mean_quality > 0.0
    assert all(buffer.frames > 0 for buffer in report.buffers)


def test_a_trace_is_replayed():
    trace = Trace.steps((0, 40e6), (10, 2e6), period=30.0)
    link = Link(trace=trace, latency=0.02, clock=lambda: 0.0)
    report = Playback(ladders(4), link, target_buffer=2.0).run(25.0)
    # It starts rich and collapses, so something has to give -- and with a
    # windowed estimate that shows up as a freeze rather than a stall.
    assert report.frozen > 0 or report.stalled > 0
    assert report.switches > 0


def test_the_run_is_deterministic():
    """A result that has to be repeated to be believed is not a measurement."""
    def once():
        return Playback(ladders(5), a_link(9), target_buffer=3.0).run(25.0).as_dict()

    assert once() == once()


def test_warming_avoids_a_startup_stall():
    """Without it every run begins with a stall that says nothing about the
    link, which would swamp a short run's score."""
    warm = Playback(ladders(3), a_link(12), target_buffer=2.0).run(10.0, warm=True)
    cold = Playback(ladders(3), a_link(12), target_buffer=2.0).run(10.0, warm=False)
    assert warm.stalled < cold.stalled


def test_the_report_names_every_clip():
    report = Playback(ladders(3), a_link(20), target_buffer=2.0).run(10.0)
    payload = report.as_dict()
    assert {row["clip"] for row in payload["clips"]} == {"pane0", "pane1", "pane2"}
    assert payload["seconds"] == 10.0


def test_nothing_to_play_is_refused():
    with pytest.raises(ValueError, match="nothing to play"):
        Playback([], a_link(10))


def test_a_nonsense_frame_rate_is_refused():
    with pytest.raises(ValueError, match="fps must be positive"):
        Playback(ladders(1), a_link(10), fps=0)


def test_the_table_renders():
    report = Playback(ladders(2), a_link(20), target_buffer=2.0).run(10.0)
    table = render_table([("20 Mbit/s", report)])
    assert "20 Mbit/s" in table and "score" in table
