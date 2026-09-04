"""A link with a capacity, so a measurement means something.

Every transport in this package has run on loopback, which is not a network: no
bottleneck, no queue, no round trip worth the name. That made three things
untestable at once. `monitor` could say how many bytes moved but never how long
they *should* have taken; `policy` could choose a rung for a budget but nothing
could say what the budget was; and a claim like "a decoded Gaussian frame needs
129 MB/s" described what the content demands rather than what a link delivers.
This is the missing constraint.

**Shaped here rather than with ``tc``.** Kernel shaping on the loopback
interface is more faithful -- it is the real queue, with real congestion
control above it -- and it was rejected for three reasons. It needs root. It is
machine-wide, so it perturbs everything else on a shared box, including the
other demos running on this one. And it cannot be exercised by a test, which
for a measurement instrument is disqualifying: an unreproducible bandwidth
figure is not a result. NeVo makes the same trade for the same reason, modelling
byte arrival from a trace rather than shaping a real interface.

So be clear about what this is. It is a **single-queue bottleneck model**:
bytes leave in the order they were requested, at the capacity in force when
they leave, after a propagation delay. Queueing delay emerges from contention,
which is the behaviour that matters when four panes share one pipe. What it does
*not* reproduce is TCP: no congestion window, no slow start, no retransmission
timers. Loss is charged as the delay a retransmission would cost, not as a
failed request, because that is what an application above TCP actually
experiences. Anything whose answer depends on congestion-control dynamics needs
a real link, and this will mislead.

    link = Link(capacity=20e6, latency=0.020)          # 20 Mbit/s, 20 ms one-way
    server = serve(bundle_dir, link=link, block=False)
    ...
    link.observed()                                    # what it actually carried

**How accurate it is, measured** -- pulling 30 ReRF frames (1.0 MB) off a real
bundle through a real socket:

===============  ==================  ==============
configured       delivered           error
===============  ==================  ==============
5 Mbit/s         4.9 Mbit/s          2.6%
20 Mbit/s        18.5 Mbit/s         7.4%
50 Mbit/s        42.5 Mbit/s         15.0%
5 ms one-way     5.8 ms per request  under 1 ms
20 ms one-way    20.9 ms per request  under 1 ms
===============  ==================  ==============

The error is a fixed cost per write, so it grows with the rate: ``time.sleep``
overshoots by a fraction of a millisecond, and at 50 Mbit/s a 64 kB chunk is
only 10 ms of link time. It always errs the same way -- **under**-delivering,
never letting more through than configured -- which is the safe direction for a
shaper, since a measurement is then pessimistic rather than flattering.

The useful range is where the link is genuinely the constraint. One pane of
this repository's content is 0.7-8 Mbit/s and one scene's whole ladder spans
6-71 Mbit/s, so the interesting decisions all sit in the band where this is
accurate to single digits. Above about 100 Mbit/s it is measuring Python.

A `Link` is shared by every connection a server has open, deliberately: the
contention is the point. Reservations are serialised, so concurrent panes queue
behind one another exactly as they would behind a real bottleneck.
"""
from __future__ import annotations

import random
import threading
import time
from bisect import bisect_right
from dataclasses import dataclass
from pathlib import Path
from typing import Any

#: Ceiling for a link with no capacity limit, in bits per second. Large enough
#: to be unreachable, finite so the arithmetic never has to special-case it.
UNLIMITED = 1e15


@dataclass(frozen=True)
class Trace:
    """Capacity over time, as a step function.

    ``at`` and ``capacity`` are parallel: capacity ``capacity[i]`` holds from
    ``at[i]`` until ``at[i + 1]``, and the last value holds for ever. Written
    this way rather than as one rate per second so a trace can carry a sharp
    drop at a known instant, which is the case an adaptive client is judged on.

    Replayed from the moment the link starts, and looping by default: a
    measurement usually outlasts the trace, and stopping at the end would
    quietly hand the client an unlimited link for the rest of the run.

    ``period`` is where a looping trace wraps, and it must be *past* the last
    point rather than at it. Wrapping at the last point would give that point's
    rate zero width, so a two-point trace would replay only its first rate for
    ever -- silently, and looking like a working trace. Left unset it is one
    more sample-interval past the end, which for the uniformly sampled traces
    real measurements come in is exactly right.
    """

    at: tuple[float, ...]
    capacity: tuple[float, ...]
    loop: bool = True
    period: float | None = None

    def __post_init__(self) -> None:
        if len(self.at) != len(self.capacity):
            raise ValueError("at and capacity must be the same length")
        if not self.at:
            raise ValueError("a trace needs at least one point")
        if self.at[0] != 0.0:
            raise ValueError("a trace must start at t=0")
        if list(self.at) != sorted(self.at):
            raise ValueError("trace times must be non-decreasing")
        if any(rate <= 0 for rate in self.capacity):
            raise ValueError("trace capacities must be positive")
        if self.period is not None and self.period <= self.at[-1]:
            raise ValueError(
                f"period {self.period} must be past the last point {self.at[-1]}; "
                "wrapping at it would give that rate no time at all"
            )

    @property
    def duration(self) -> float:
        """How long one pass takes, including the last point's own stretch."""
        if self.period is not None:
            return self.period
        if len(self.at) < 2:
            return 0.0                      # a constant trace has no period
        return self.at[-1] + (self.at[-1] - self.at[-2])

    def at_time(self, elapsed: float) -> float:
        """Capacity in force ``elapsed`` seconds after the link started."""
        if elapsed < 0:
            elapsed = 0.0
        if self.loop and self.duration > 0:
            elapsed %= self.duration
        index = bisect_right(self.at, elapsed) - 1
        return self.capacity[max(0, index)]

    @classmethod
    def steps(cls, *pairs: tuple[float, float], loop: bool = True,
              period: float | None = None) -> "Trace":
        """``Trace.steps((0, 20e6), (5, 3e6))`` -- 20 Mbit/s, then 3 after 5 s."""
        return cls(at=tuple(p[0] for p in pairs),
                   capacity=tuple(p[1] for p in pairs), loop=loop, period=period)

    @classmethod
    def read(cls, path: Path | str, *, loop: bool = True,
             period: float | None = None) -> "Trace":
        """A two-column text trace: ``<seconds> <bits per second>`` per line.

        Blank lines and ``#`` comments are skipped, so a trace can carry a note
        about where it came from -- which for a bandwidth trace is the most
        important thing about it.
        """
        points = []
        for line in Path(path).read_text().splitlines():
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            when, rate = line.split()
            points.append((float(when), float(rate)))
        if not points:
            raise ValueError(f"{path} holds no trace points")
        return cls.steps(*points, loop=loop, period=period)


@dataclass
class Reservation:
    """When a write may start and when it will have finished."""

    bytes: int
    starts_at: float
    finishes_at: float
    #: Seconds of the total that were queueing behind other traffic rather than
    #: transmission. The signal that a link is saturated.
    queued: float
    #: Seconds charged for retransmission, when loss was configured.
    retransmit: float


class Link:
    """A shared bottleneck: a capacity, a delay, and optionally a trace.

    ``capacity`` is bits per second, ignored when a ``trace`` is given.
    ``latency`` is the one-way propagation delay in seconds -- a response pays
    it once, not per chunk. ``loss`` is a per-chunk probability; a lost chunk is
    charged one round trip, the cost of noticing and resending.

    ``seed`` makes loss reproducible. A bandwidth figure that changes between
    runs is not a measurement, so the default is seeded rather than random.
    """

    def __init__(
        self,
        *,
        capacity: float = UNLIMITED,
        latency: float = 0.0,
        loss: float = 0.0,
        trace: Trace | None = None,
        seed: int = 0,
        clock=time.monotonic,
    ) -> None:
        if capacity <= 0:
            raise ValueError("capacity must be positive")
        if latency < 0:
            raise ValueError("latency must not be negative")
        if not 0.0 <= loss < 1.0:
            raise ValueError("loss must be a probability in [0, 1)")
        self.capacity = float(capacity)
        self.latency = float(latency)
        self.loss = float(loss)
        self.trace = trace
        self._clock = clock
        self._random = random.Random(seed)
        self._lock = threading.Lock()
        self._started = clock()
        # When the link next falls idle. Reservations queue behind this, which
        # is what makes concurrent connections contend rather than each
        # believing it has the whole pipe.
        self._free_at = self._started
        self._bytes = 0
        self._reservations = 0
        self._queued = 0.0
        self._retransmit = 0.0

    # ----------------------------------------------------------- the model ---
    def capacity_at(self, when: float) -> float:
        """Capacity in force at an absolute clock time."""
        if self.trace is None:
            return self.capacity
        return self.trace.at_time(when - self._started)

    def reserve(self, size: int) -> Reservation:
        """Book ``size`` bytes of link time, and say when they land.

        Serialised: the reservation starts when the link is next free, so a
        second caller queues behind the first. That queueing *is* the effect a
        bottleneck has, and modelling each connection as independent would make
        contention -- the whole reason a budget has to be shared -- invisible.
        """
        if size < 0:
            raise ValueError("size must not be negative")
        now = self._clock()
        with self._lock:
            starts_at = max(now, self._free_at)
            queued = starts_at - now
            rate = self.capacity_at(starts_at)
            duration = (size * 8) / rate
            retransmit = 0.0
            if self.loss and size and self._random.random() < self.loss:
                # One round trip: the time to notice nothing arrived and send
                # it again. Not a failure -- TCP hides loss from the
                # application as delay, and pretending a frame vanished would
                # model something the client never sees.
                retransmit = 2 * self.latency
            self._free_at = starts_at + duration
            self._bytes += size
            self._reservations += 1
            self._queued += queued
            self._retransmit += retransmit
            return Reservation(
                bytes=size, starts_at=starts_at,
                finishes_at=self._free_at + retransmit,
                queued=queued, retransmit=retransmit,
            )

    def send(self, size: int, *, propagate: bool = False) -> float:
        """Reserve ``size`` bytes and block until they would have arrived.

        ``propagate`` adds the one-way delay, and is set for the first write of
        a response rather than every chunk: propagation is paid once by a
        stream of bytes already in flight.
        """
        reservation = self.reserve(size)
        wait = reservation.finishes_at - self._clock()
        if propagate:
            wait += self.latency
        if wait > 0:
            time.sleep(wait)
        return max(0.0, wait)

    # ------------------------------------------------------ what it carried ---
    def observed(self) -> dict[str, Any]:
        """What the link actually delivered.

        ``bits_per_second`` here is the number `policy` wants as a budget: not
        the configured capacity, which a client cannot know, but the rate the
        bytes came in at -- which is what a rate estimator would have measured.
        """
        with self._lock:
            elapsed = self._clock() - self._started
            payload = {
                "bytes": self._bytes,
                "reservations": self._reservations,
                "elapsed_seconds": round(elapsed, 4),
                "queued_seconds": round(self._queued, 4),
                "retransmit_seconds": round(self._retransmit, 4),
                "configured_bits_per_second": (
                    None if self.trace else round(self.capacity, 1)
                ),
                "latency_seconds": self.latency,
                "loss": self.loss,
            }
        payload["bits_per_second"] = (
            round(payload["bytes"] * 8 / elapsed, 1) if elapsed > 0 else None
        )
        # Saturation, as the fraction of transfer time spent waiting for the
        # link rather than using it. A client below this is not being limited.
        total = payload["queued_seconds"] + payload["retransmit_seconds"]
        payload["queueing_fraction"] = (
            round(total / elapsed, 4) if elapsed > 0 else None
        )
        return payload

    def reset(self) -> None:
        """Zero the counters and restart the trace, e.g. between two runs."""
        with self._lock:
            self._started = self._clock()
            self._free_at = self._started
            self._bytes = 0
            self._reservations = 0
            self._queued = 0.0
            self._retransmit = 0.0


def described(link: Link | None) -> str:
    """One line naming a link's constraint, for a server to print."""
    if link is None:
        return "unshaped: loopback, so any rate measured through it is the disk's"
    if link.trace is not None:
        rates = link.trace.capacity
        return (f"trace: {min(rates) / 1e6:.1f}-{max(rates) / 1e6:.1f} Mbit/s over "
                f"{link.trace.duration:.0f}s"
                + (", looping" if link.trace.loop else "")
                + f", {link.latency * 1000:.0f} ms one-way")
    return (f"{link.capacity / 1e6:.1f} Mbit/s, {link.latency * 1000:.0f} ms "
            f"one-way" + (f", {link.loss:.1%} loss" if link.loss else ""))
