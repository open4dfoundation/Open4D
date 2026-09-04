"""A link with a capacity, and the shaping it imposes.

Most of these drive the model with an injected clock rather than sleeping. That
is not only for speed: a test that sleeps measures the machine's scheduler as
much as the model, and this is a measurement instrument, so its own tests
should not be the least reproducible thing in the repository.
"""
from __future__ import annotations

import io
import contextlib
import threading
import time
import urllib.request

import pytest

from streamer import bundle
from streamer.link import UNLIMITED, Link, Trace, described
from streamer.server import serve

pytestmark = pytest.mark.cpu


class Ticks:
    """A clock that only moves when told."""

    def __init__(self) -> None:
        self.now = 1000.0

    def __call__(self) -> float:
        return self.now

    def advance(self, seconds: float) -> None:
        self.now += seconds


# ------------------------------------------------------------- the capacity ---


def test_a_reservation_takes_the_time_the_bytes_need():
    clock = Ticks()
    link = Link(capacity=8_000_000, latency=0.0, clock=clock)   # 1 MB/s
    booked = link.reserve(1_000_000)
    assert booked.finishes_at - booked.starts_at == pytest.approx(1.0)
    assert booked.queued == 0.0


def test_a_second_reservation_queues_behind_the_first():
    """The contention that makes a shared budget mean something. Modelling each
    connection as independent would let four panes each believe they had the
    whole pipe, which is exactly the error a chooser exists to prevent."""
    clock = Ticks()
    link = Link(capacity=8_000_000, latency=0.0, clock=clock)
    first = link.reserve(1_000_000)
    second = link.reserve(1_000_000)
    assert second.starts_at == pytest.approx(first.finishes_at)
    assert second.queued == pytest.approx(1.0)
    # Two seconds of link time from the moment the first one started.
    assert second.finishes_at - first.starts_at == pytest.approx(2.0)


def test_a_link_left_idle_does_not_bank_capacity():
    """A token bucket would let an idle link burst afterwards. This is a queue,
    not a bucket: idle time is gone, which is what a bottleneck does."""
    clock = Ticks()
    link = Link(capacity=8_000_000, latency=0.0, clock=clock)
    link.reserve(1_000_000)
    clock.advance(10.0)
    booked = link.reserve(1_000_000)
    assert booked.queued == 0.0
    assert booked.starts_at == pytest.approx(clock.now)


def test_zero_bytes_costs_nothing():
    link = Link(capacity=8_000_000, clock=Ticks())
    assert link.reserve(0).finishes_at == pytest.approx(link.reserve(0).starts_at)


def test_a_negative_size_is_refused():
    with pytest.raises(ValueError, match="must not be negative"):
        Link(clock=Ticks()).reserve(-1)


@pytest.mark.parametrize("capacity,latency,loss", [
    (0, 0.0, 0.0), (-1, 0.0, 0.0), (1e6, -0.1, 0.0), (1e6, 0.0, 1.0), (1e6, 0.0, -0.1),
])
def test_impossible_links_are_refused(capacity, latency, loss):
    with pytest.raises(ValueError):
        Link(capacity=capacity, latency=latency, loss=loss)


def test_an_unlimited_link_is_effectively_free():
    link = Link(capacity=UNLIMITED, clock=Ticks())
    booked = link.reserve(10_000_000)
    assert booked.finishes_at - booked.starts_at < 1e-6


# ---------------------------------------------------------------- the trace ---


def test_a_trace_holds_each_rate_until_the_next_point():
    trace = Trace.steps((0, 20e6), (5, 3e6), (10, 12e6), loop=False)
    assert trace.at_time(0) == 20e6
    assert trace.at_time(4.999) == 20e6
    assert trace.at_time(5) == 3e6
    assert trace.at_time(9.9) == 3e6
    assert trace.at_time(10) == 12e6
    assert trace.at_time(1000) == 12e6       # the last rate holds for ever


def test_a_trace_loops_past_its_last_point_not_at_it():
    """Wrapping *at* the last point would give that rate zero width, so a
    two-point trace would replay only its first rate for ever -- silently, and
    looking like a working trace. The period runs one sample past the end."""
    trace = Trace.steps((0, 20e6), (5, 3e6))
    assert trace.duration == 10               # 5, plus the last point's own 5
    assert trace.at_time(5.0) == 3e6          # the second rate does get its turn
    assert trace.at_time(9.9) == 3e6
    assert trace.at_time(10.0) == 20e6        # wrapped
    assert trace.at_time(12.0) == 20e6


def test_an_explicit_period_overrides_the_guess():
    trace = Trace.steps((0, 20e6), (5, 3e6), period=8.0)
    assert trace.duration == 8.0
    assert trace.at_time(7.9) == 3e6
    assert trace.at_time(8.0) == 20e6


def test_a_period_at_or_before_the_last_point_is_refused():
    with pytest.raises(ValueError, match="must be past the last point"):
        Trace.steps((0, 20e6), (5, 3e6), period=5.0)


def test_a_constant_trace_needs_no_period():
    trace = Trace.steps((0, 7e6))
    assert trace.at_time(0) == 7e6
    assert trace.at_time(10_000) == 7e6


def test_a_trace_drives_the_reservation_rate():
    clock = Ticks()
    link = Link(trace=Trace.steps((0, 8e6), (1, 800_000), loop=False),
                latency=0.0, clock=clock)
    fast = link.reserve(1_000_000)           # at 1 MB/s
    assert fast.finishes_at - fast.starts_at == pytest.approx(1.0)
    clock.advance(5.0)
    slow = link.reserve(1_000_000)           # now at 100 kB/s
    assert slow.finishes_at - slow.starts_at == pytest.approx(10.0)


@pytest.mark.parametrize("pairs,message", [
    ([(1.0, 5e6)], "must start at t=0"),
    ([(0.0, 0.0)], "must be positive"),
    ([(0.0, 5e6), (2.0, 5e6), (1.0, 5e6)], "non-decreasing"),
])
def test_impossible_traces_are_refused(pairs, message):
    with pytest.raises(ValueError, match=message):
        Trace.steps(*pairs)


def test_an_empty_trace_is_refused():
    with pytest.raises(ValueError, match="at least one point"):
        Trace(at=(), capacity=())


def test_mismatched_columns_are_refused():
    with pytest.raises(ValueError, match="same length"):
        Trace(at=(0.0, 1.0), capacity=(5e6,))


def test_a_trace_reads_from_a_file(tmp_path):
    path = tmp_path / "walk.trace"
    path.write_text(
        "# a 4G walk, from somewhere worth naming\n"
        "0    20000000\n"
        "\n"
        "5     3000000   # tunnel\n"
        "12   18000000\n"
    )
    trace = Trace.read(path)
    assert trace.at == (0.0, 5.0, 12.0)
    assert trace.capacity == (20e6, 3e6, 18e6)


def test_a_file_with_no_points_is_refused(tmp_path):
    path = tmp_path / "empty.trace"
    path.write_text("# nothing but a comment\n")
    with pytest.raises(ValueError, match="no trace points"):
        Trace.read(path)


# ----------------------------------------------------------------- the loss ---


def test_loss_is_charged_as_delay_not_as_failure():
    """What an application above TCP experiences. Failing the request would
    model something a client never sees."""
    clock = Ticks()
    # Just under certain: loss=1.0 is refused, because every chunk lost is not
    # "one retransmission each", it is a link that never delivers.
    link = Link(capacity=UNLIMITED, latency=0.010, loss=0.999, clock=clock)
    booked = link.reserve(1_000)
    assert booked.retransmit == pytest.approx(0.020)      # one round trip


def test_no_loss_means_no_retransmission():
    link = Link(capacity=UNLIMITED, latency=0.010, loss=0.0, clock=Ticks())
    assert link.reserve(1_000).retransmit == 0.0


def test_loss_is_reproducible():
    """A bandwidth figure that moves between runs is not a measurement."""
    def run():
        link = Link(capacity=8e6, latency=0.01, loss=0.5, seed=7, clock=Ticks())
        return [link.reserve(1000).retransmit for _ in range(20)]

    assert run() == run()


def test_a_different_seed_gives_a_different_pattern():
    def run(seed):
        link = Link(capacity=8e6, latency=0.01, loss=0.5, seed=seed, clock=Ticks())
        return [link.reserve(1000).retransmit for _ in range(20)]

    assert run(1) != run(2)


# ------------------------------------------------------------ what it saw ---


def test_the_observed_rate_is_what_arrived_not_what_was_configured():
    """The number a rate estimator would read, and the one `policy` wants as a
    budget: a client cannot know the configured capacity."""
    clock = Ticks()
    link = Link(capacity=8_000_000, latency=0.0, clock=clock)
    link.reserve(1_000_000)
    clock.advance(2.0)                       # half the time was idle
    seen = link.observed()
    assert seen["bytes"] == 1_000_000
    assert seen["bits_per_second"] == pytest.approx(4_000_000, rel=0.01)
    assert seen["configured_bits_per_second"] == 8_000_000


def test_queueing_fraction_shows_saturation():
    clock = Ticks()
    link = Link(capacity=8_000_000, latency=0.0, clock=clock)
    for _ in range(4):
        link.reserve(1_000_000)              # 4 s of work booked instantly
    clock.advance(4.0)
    assert link.observed()["queueing_fraction"] > 0.5


def test_an_unused_link_reports_nothing_rather_than_dividing_by_zero():
    link = Link(clock=Ticks())
    seen = link.observed()
    assert seen["bytes"] == 0
    assert seen["bits_per_second"] is None


def test_a_traced_link_reports_no_single_capacity():
    link = Link(trace=Trace.steps((0, 5e6)), clock=Ticks())
    assert link.observed()["configured_bits_per_second"] is None


def test_reset_zeroes_the_counters_and_restarts_the_trace():
    clock = Ticks()
    link = Link(trace=Trace.steps((0, 8e6), (1, 800_000), loop=False),
                latency=0.0, clock=clock)
    link.reserve(1_000_000)
    clock.advance(5.0)
    link.reset()
    booked = link.reserve(1_000_000)
    assert link.observed()["bytes"] == 1_000_000
    # Back at the trace's first rate, not its last.
    assert booked.finishes_at - booked.starts_at == pytest.approx(1.0)


# ------------------------------------------------------------- described ---


def test_described_names_the_constraint():
    assert "loopback" in described(None)
    assert "20.0 Mbit/s" in described(Link(capacity=20e6, latency=0.02))
    assert "2.0% loss" in described(Link(capacity=20e6, loss=0.02))
    assert "trace" in described(Link(trace=Trace.steps((0, 5e6), (2, 20e6))))


# --------------------------------------------------- through a real server ---


def a_bundle(tmp_path, *, frames=6, size=40_000):
    (tmp_path / "c").mkdir(parents=True, exist_ok=True)
    paths = []
    for index in range(frames):
        relative = f"c/frame_{index:04d}.bin"
        (tmp_path / relative).write_bytes(b"x" * size)
        paths.append(relative)
    bundle.write(tmp_path, title="t", source="s",
                 clips=[bundle.Clip(name="c", representation="pixels",
                                    frames=paths)])
    return tmp_path, paths


def quietly(bundle_dir, **kwargs):
    with contextlib.redirect_stdout(io.StringIO()):
        return serve(bundle_dir, port=0, block=False, **kwargs)


def test_the_server_paces_a_real_response(tmp_path):
    """The end of the chain: a link passed to `serve` slows a real socket."""
    root, paths = a_bundle(tmp_path, frames=4, size=50_000)
    link = Link(capacity=2_000_000, latency=0.0)      # 250 kB/s
    server = quietly(root, link=link)
    try:
        base = f"http://127.0.0.1:{server.server_address[1]}"
        opener = urllib.request.build_opener()
        started = time.monotonic()
        for path in paths:
            opener.open(f"{base}/{path}", timeout=60).read()
        elapsed = time.monotonic() - started
        # 200 kB at 250 kB/s is 0.8 s. Generous bounds: this asserts the
        # shaping happened at all, not its accuracy, which the module
        # docstring records from a measurement rather than a test.
        assert 0.4 < elapsed < 3.0
        assert link.observed()["bytes"] >= 200_000
    finally:
        server.shutdown()
        server.server_close()


def test_an_unshaped_server_is_unchanged(tmp_path):
    root, paths = a_bundle(tmp_path, frames=3, size=10_000)
    server = quietly(root)
    try:
        base = f"http://127.0.0.1:{server.server_address[1]}"
        started = time.monotonic()
        for path in paths:
            urllib.request.urlopen(f"{base}/{path}", timeout=30).read()
        assert time.monotonic() - started < 1.0
    finally:
        server.shutdown()
        server.server_close()


def test_the_counters_route_reports_the_link(tmp_path):
    """So a client can read the rate it is actually getting, which is the
    budget a chooser needs and cannot otherwise know."""
    import json

    root, paths = a_bundle(tmp_path, frames=2, size=20_000)
    link = Link(capacity=4_000_000, latency=0.001)
    server = quietly(root, link=link)
    try:
        base = f"http://127.0.0.1:{server.server_address[1]}"
        urllib.request.urlopen(f"{base}/{paths[0]}", timeout=30).read()
        stats = json.loads(
            urllib.request.urlopen(f"{base}/stats.json", timeout=30).read())
        assert stats["link"]["configured_bits_per_second"] == 4_000_000
        assert stats["link"]["bytes"] > 0
    finally:
        server.shutdown()
        server.server_close()


def test_concurrent_readers_share_the_link(tmp_path):
    """Two panes on one pipe take about twice as long as one, because they
    queue. If each got the whole capacity, a shared budget would be a fiction."""
    root, paths = a_bundle(tmp_path, frames=1, size=120_000)
    link = Link(capacity=4_000_000, latency=0.0)      # 500 kB/s
    server = quietly(root, link=link)
    try:
        base = f"http://127.0.0.1:{server.server_address[1]}"
        url = f"{base}/{paths[0]}"

        def pull():
            urllib.request.urlopen(url, timeout=60).read()

        started = time.monotonic()
        threads = [threading.Thread(target=pull) for _ in range(2)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        elapsed = time.monotonic() - started
        # 240 kB total at 500 kB/s is ~0.48 s; one alone would be ~0.24 s.
        assert elapsed > 0.30
        assert link.observed()["queued_seconds"] > 0
    finally:
        server.shutdown()
        server.server_close()
