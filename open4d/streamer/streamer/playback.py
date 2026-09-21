"""Playing a bundle over a link, and scoring what the viewer got.

`policy` chooses a rung for a budget at an instant. That is one decision, and a
playback is thousands of them, so the questions a comparison actually asks
cannot be answered by it alone: did this method stall, how often did its
quality jump about, and which of two methods survives a bad link better. Those
need the thing between a decision and a picture -- a **buffer**.

A buffer is why streaming is not just downloading. Frames arrive at whatever
rate the link gives and are consumed at a fixed one, so the occupancy is the
integral of the difference. When it reaches zero playback stops, and *that* --
not a low bitrate -- is what a viewer reports as the stream being bad. Every
adaptation decision is really about keeping this number off the floor.

Three things are told apart here, because collapsing them hides the behaviour
worth measuring:

**Stall.** The buffer emptied and playback halted. Unintentional, and the most
expensive thing that can happen.

**Freeze.** A clip was *deliberately* not fetched, so it holds its last frame
while the others keep playing. A policy decision under deficit, not a failure,
and cheaper than a stall -- but it grows more expensive the longer it lasts,
or a chooser would freeze one pane for ever to protect the rest.

**Switch.** The rung changed. Visible, so it is charged; the point of charging
it is that a marginal quality gain should not buy a visible jump.

Simulated on a virtual clock rather than by sleeping. `Link.reserve` is pure
bookkeeping given a clock, which is what makes this exact and fast: a
sixty-second playback of nine panes runs in milliseconds and gives the same
answer every time. A run that has to be repeated to be believed is not a
measurement.

Two things this model found about its own subject, both of which contradicted
an assumption made while writing it:

* **The rate estimator decides whether stalls happen at all.** Estimating from
  `Link.observed`, which is cumulative, a 30 -> 2.5 Mbit/s trace produced 30.4
  seconds of stall and not one freeze -- the average of both phases never
  looked like a deficit, so nothing ever told the chooser to act. Swapping in
  the same windowed estimate the browser uses turned that into 0 seconds of
  stall and 56 of freeze: the same shortfall, taken as a decision instead of a
  failure.
* **Scaling the budget by occupancy is nearly free of benefit here**, worth
  0.7 dB and no stalls, because the deficit handling already prevents them --
  while costing up to 1224 rung changes in thirty seconds if decisions are not
  spaced out. See `buffer_budget` and ``decision_interval``.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Callable, Mapping, Sequence

from .link import Link
from .policy import Rung

#: What a second of stalled playback costs, in the same units as quality (dB).
#: Large: the ABR literature puts a stall at several dB-seconds and viewers
#: agree -- a stall is reported as the stream being broken, a low rung as it
#: being soft.
STALL_PENALTY = 40.0

#: What a second of deliberate freeze costs. Cheaper than a stall, because the
#: rest of the view keeps playing, but not free.
FREEZE_PENALTY = 12.0

#: What one rung change costs. Charged per switch rather than per second: the
#: jump is the visible event.
SWITCH_PENALTY = 1.0

#: Samples the rate estimate halves over. The same rule and horizon as
#: ``RateMeter`` in the client, deliberately: this model exists to predict what
#: that client will do, and giving it a better estimator than the thing it
#: models would flatter every result.
RATE_HALF_LIFE = 8


class RateEstimate:
    """An exponentially weighted mean of recent transfer rates.

    Not `Link.observed`, which is cumulative and therefore cannot see a change:
    on a trace that starts fast and collapses, a cumulative estimate keeps
    reporting the average of both phases, the chooser never registers a
    deficit, and the buffers drain while it holds a rung it cannot afford.
    Measured on a 30 -> 2.5 Mbit/s trace: 30.4 seconds of stall and not one
    freeze, because nothing ever told the chooser the link had gone.
    """

    def __init__(self, half_life: int = RATE_HALF_LIFE) -> None:
        self.half_life = half_life
        self.bits_per_second = 0.0
        self.samples = 0

    def record(self, size: int, seconds: float) -> None:
        if size <= 0 or seconds <= 0:
            return
        rate = size * 8 / seconds
        self.samples += 1
        if self.samples == 1:
            self.bits_per_second = rate
            return
        alpha = 1 - 0.5 ** (1 / self.half_life)
        self.bits_per_second += alpha * (rate - self.bits_per_second)


@dataclass
class Buffer:
    """Frames decoded and waiting to be shown, for one clip.

    Held in **seconds** rather than frames so a clip at a different frame rate
    is comparable, and because the quantity a viewer experiences is time.
    """

    clip: str
    #: Seconds of playback currently buffered.
    seconds: float = 0.0
    #: Seconds this clip has spent stalled, and how many separate stalls.
    stalled: float = 0.0
    stalls: int = 0
    #: Seconds spent frozen by decision, and how many separate freezes.
    frozen: float = 0.0
    freezes: int = 0
    #: Rung changes, and the rung in force.
    switches: int = 0
    rung: str | None = None
    #: Frames fetched, the playback seconds they bought, and the sum of
    #: quality x seconds. Mean quality is the ratio, so a rung that was on
    #: screen briefly counts briefly -- an unweighted mean over rungs would
    #: score a one-frame excursion the same as a minute of it.
    frames: int = 0
    filled_seconds: float = 0.0
    quality_seconds: float = 0.0
    #: True while deliberately not being fetched.
    freezing: bool = False
    _stalling: bool = False
    _froze: bool = False

    def fill(self, seconds: float, quality: float) -> None:
        """A frame arrived: it buys ``seconds`` of playback at ``quality``."""
        self.seconds += seconds
        self.frames += 1
        self.filled_seconds += seconds
        self.quality_seconds += seconds * quality
        self._stalling = False

    def drain(self, seconds: float) -> float:
        """Consume ``seconds`` of playback; return the seconds spent stalled.

        A buffer that empties mid-interval stalls for the remainder, which is
        why this returns a duration rather than a flag: charging a whole
        interval would make the penalty depend on how finely time was stepped.
        """
        if self.freezing:
            self.frozen += seconds
            if not self._froze:
                self.freezes += 1
                self._froze = True
            return 0.0
        self._froze = False
        played = min(self.seconds, seconds)
        self.seconds -= played
        short = seconds - played
        if short > 0:
            self.stalled += short
            if not self._stalling:
                self.stalls += 1
                self._stalling = True
        return short

    def use(self, rung: str | None) -> None:
        if self.rung is not None and rung != self.rung:
            self.switches += 1
        self.rung = rung

    @property
    def mean_quality(self) -> float:
        """Quality delivered, weighted by the playback time it bought."""
        if self.filled_seconds <= 0:
            return 0.0
        return self.quality_seconds / self.filled_seconds


@dataclass
class Report:
    """What a playback delivered."""

    seconds: float
    buffers: tuple[Buffer, ...]
    #: What the link carried, from `Link.observed`.
    link: Mapping[str, Any]
    #: Seconds of playback the client asked for but could not show.
    stalled: float
    frozen: float
    switches: int
    mean_quality: float
    score: float

    def as_dict(self) -> dict[str, Any]:
        return {
            "seconds": round(self.seconds, 3),
            "stalled_seconds": round(self.stalled, 3),
            "frozen_seconds": round(self.frozen, 3),
            "switches": self.switches,
            "mean_quality": round(self.mean_quality, 3),
            "score": round(self.score, 3),
            "link": dict(self.link),
            "clips": [
                {
                    "clip": buffer.clip, "frames": buffer.frames,
                    "stalled": round(buffer.stalled, 3), "stalls": buffer.stalls,
                    "frozen": round(buffer.frozen, 3), "freezes": buffer.freezes,
                    "switches": buffer.switches, "rung": buffer.rung,
                }
                for buffer in self.buffers
            ],
        }


def buffer_budget(rate: float, occupancy: float, target: float) -> float:
    """The rate to spend, given what the link gives and how full the buffer is.

    Rate alone is a poor guide and known to be: an estimate is a lagging
    average, so a client that trusts it keeps buying the rung the link *used*
    to afford and drains its buffer doing so. Occupancy is the state that
    actually says whether the last few decisions were affordable, which is why
    every buffer-based algorithm since BBA leans on it.

    The rule here is the simplest one with the right shape: scale the budget by
    how full the buffer is against its target, clamped to [0.5, 1.5]. Below
    target it spends under the estimate and refills; above, it spends over and
    banks quality. Clamped both ways -- unclamped, an empty buffer would pick
    the floor and stay there, and a full one would overshoot the link and empty
    itself.

    **Measured, it earns much less here than the literature would suggest.**
    On a 30 -> 6 -> 2.5 -> 14 Mbit/s trace it bought 0.7 dB of mean quality and
    removed no stalls at all, against a flat rate-only budget. The reason is
    that stall prevention is already being done by the deficit handling in
    `Playback._even_split`, which refuses to commit to more clips than the rate
    can carry -- so this is a second-order correction on top of a first-order
    fix, and it costs churn to get.

    It is also what makes the budget noisy enough to need
    ``decision_interval``: occupancy jitters every frame, and where the scaled
    budget can reach across a rung boundary that jitter becomes 1224 rung
    changes in thirty seconds.

    Kept anyway, because it is the standard technique and a result measured
    against it should be comparable to published ones. But a reader deciding
    whether their own client needs it should know it was worth 0.7 dB on this
    content, at that cost, and not assume otherwise.
    """
    if target <= 0:
        return rate
    return rate * max(0.5, min(1.5, occupancy / target))


class Playback:
    """Play clips over a link on a virtual clock, and score the result.

    ``ladders`` is one sequence of `policy.Rung` per clip, as
    `policy.measured_rungs` returns. ``chooser`` decides a rung per clip from a
    budget; the default is the even split the viewer uses, since what is being
    compared is usually methods rather than choosers.

    One fetch at a time, matching the single queue `Link` models. A real client
    opens several connections, which changes the queueing and not the
    arithmetic -- and a serial fetcher is the honest simple case rather than an
    optimistic one.
    """

    def __init__(
        self,
        ladders: Sequence[Sequence[Rung]],
        link: Link,
        *,
        fps: int = 30,
        target_buffer: float = 4.0,
        #: How often to reconsider, in seconds of playback.
        #:
        #: Hysteresis for a noisy input, not a segment boundary. The budget is
        #: scaled by occupancy, which jitters as frames arrive and drain, so a
        #: per-frame decision chases the jitter -- but only when the scaled
        #: budget can reach across a rung boundary. Measured on an eight-pane
        #: ladder over thirty seconds:
        #:
        #: ===========  ==========  ==========
        #: capacity     per frame   every 4 s
        #: ===========  ==========  ==========
        #: 14 Mbit/s    1224        24
        #: 18 Mbit/s    548         8
        #: 19 Mbit/s    8           8
        #: ===========  ==========  ==========
        #:
        #: At 19 the share already sits above the middle rung and nothing can
        #: flip; below it the [0.5, 1.5] scaling straddles the boundary and
        #: every frame is a coin toss. So the interval costs nothing where it
        #: is not needed and is worth fifty-fold where it is, which is why it
        #: defaults to on rather than being offered as a tuning knob.
        decision_interval: float = 2.0,
        metric: str = "psnr",
        clock: Callable[[], float] | None = None,
        chooser: Callable[..., Mapping[str, Rung]] | None = None,
        stall_penalty: float = STALL_PENALTY,
        freeze_penalty: float = FREEZE_PENALTY,
        switch_penalty: float = SWITCH_PENALTY,
    ) -> None:
        if fps <= 0:
            raise ValueError("fps must be positive")
        if not ladders:
            raise ValueError("nothing to play")
        self.ladders = {rungs[0].clip: tuple(rungs) for rungs in ladders if rungs}
        self.link = link
        self.fps = fps
        self.frame_seconds = 1.0 / fps
        self.target_buffer = target_buffer
        self.decision_interval = decision_interval
        self.metric = metric
        self.chooser = chooser or self._even_split
        self.stall_penalty = stall_penalty
        self.freeze_penalty = freeze_penalty
        self.switch_penalty = switch_penalty
        self.buffers = {clip: Buffer(clip=clip) for clip in self.ladders}
        self.rate = RateEstimate()
        self._now = 0.0
        self._decided_at = float("-inf")
        self._picked: dict = {}
        self._external_clock = clock

    # ------------------------------------------------------------ the clock ---
    def _tick(self) -> float:
        return self._now if self._external_clock is None else self._external_clock()

    def _advance(self, to: float) -> None:
        """Move the clock forward, draining every buffer by the interval."""
        step = max(0.0, to - self._now)
        self._now = to
        if step <= 0:
            return
        for buffer in self.buffers.values():
            buffer.drain(step)

    # ---------------------------------------------------------- the decision ---
    def _even_split(self, rate: float, buffers: Mapping[str, Buffer]) -> dict:
        """The viewer's rule: equal shares, best rung each share affords.

        Budget per clip is scaled by that clip's own occupancy, so a pane that
        has fallen behind buys cheaper frames until it catches up rather than
        being punished for the average.
        """
        # Nothing measured yet: fetch everything at the floor to find out.
        #
        # Without this a cold start deadlocks, and silently. With no estimate
        # the rate is zero, so the deficit loop below freezes every clip; a
        # frozen clip is never fetched; and an estimate only ever comes from a
        # fetch. Measured before the guard existed: 30 seconds of playback, 30
        # seconds frozen, mean quality 0.0, and no error anywhere. A player
        # starts at its lowest rung for exactly this reason.
        if not getattr(self.rate, "samples", 0):
            chosen = {}
            for clip, rungs in self.ladders.items():
                buffers[clip].freezing = False
                chosen[clip] = rungs[0]
            return chosen

        # Deficit first: when the link cannot carry every clip's cheapest rung,
        # something has to give, and freezing some so the rest play is better
        # than starving all of them equally into a stall.
        #
        # Two criteria, in order. **Already frozen** comes first, so a freeze is
        # sticky: a chooser that reconsidered from scratch each interval would
        # thaw one pane and freeze another, and a viewer would see panes
        # flickering in and out rather than a stable subset playing. Then
        # **fullest buffer**, because the pane with least ahead of it is the one
        # about to stall and so the one worth protecting.
        #
        # A frozen pane stays frozen for the rest of the run unless the rate
        # recovers. That is deliberate, and it is why `FREEZE_PENALTY` is
        # charged per second rather than per event: showing seven panes well
        # beats showing eight badly, but not for ever, and the score has to say
        # so rather than treating a permanent freeze as free.
        floors = {clip: rungs[0].bits_per_second
                  for clip, rungs in self.ladders.items()}
        playing = sorted(
            self.ladders,
            key=lambda clip: (buffers[clip].freezing, buffers[clip].seconds),
        )
        while playing and sum(floors[clip] for clip in playing) > rate:
            playing.pop()                    # the stickiest, best-buffered one
        frozen = set(self.ladders) - set(playing)

        chosen: dict = {}
        share = rate / max(1, len(playing)) if playing else 0.0
        for clip, rungs in self.ladders.items():
            buffers[clip].freezing = clip in frozen
            if clip in frozen:
                chosen[clip] = rungs[0]      # nothing is fetched for it
                continue
            budget = buffer_budget(share, buffers[clip].seconds, self.target_buffer)
            pick = rungs[0]
            for rung in rungs:
                if rung.bits_per_second <= budget:
                    pick = rung
            chosen[clip] = pick
        return chosen

    # ------------------------------------------------------------- the run ---
    def run(self, seconds: float, *, warm: bool = True) -> Report:
        """Play for ``seconds`` of wall time and report what was delivered.

        ``warm`` fills each buffer to target before the clock starts, which is
        what a player's startup does. Without it every run begins with a stall
        that says nothing about the link.
        """
        if warm:
            self._prefill()
        horizon = self._now + seconds

        while self._now < horizon:
            # The rate a client would have estimated: what the link has
            # delivered so far. Cumulative here rather than windowed, which is
            # a deliberate simplification -- a windowed estimate is what the
            # browser does, and modelling its lag belongs with modelling the
            # browser.
            if self._now - self._decided_at >= self.decision_interval:
                self._picked = self.chooser(self.rate.bits_per_second, self.buffers)
                for clip, rung in self._picked.items():
                    if not self.buffers[clip].freezing:
                        self.buffers[clip].use(rung.name)
                self._decided_at = self._now
            picked = self._picked

            # Fetch for whichever clip is closest to running dry, ignoring the
            # frozen ones. That is the scheduling decision a buffer model
            # exists to make: with one connection, who gets it next decides who
            # stalls.
            live = [clip for clip in self.buffers if not self.buffers[clip].freezing]
            if not live:
                self._advance(min(self._now + self.decision_interval, horizon))
                continue
            clip = min(live, key=lambda name: self.buffers[name].seconds)
            buffer = self.buffers[clip]
            rung = picked[clip]
            size = int(rung.bits_per_second / (8 * self.fps))

            booked = self.link.reserve(size)
            arrives = booked.finishes_at + self.link.latency
            # Timed as a client would: from asking to having it, so queueing
            # and propagation are in the estimate. A model that timed only
            # transmission would estimate the link's capacity rather than the
            # rate this client can actually achieve, and then overshoot it.
            self.rate.record(size, max(1e-9, arrives - self._now))
            self._advance(min(arrives, horizon))
            if arrives <= horizon:
                buffer.fill(self.frame_seconds, rung.utility(self.metric))
            if arrives >= horizon:
                break

        self._advance(horizon)
        return self._report(seconds)

    def _prefill(self) -> None:
        """Buy each clip its target buffer before the clock matters."""
        picked = {clip: rungs[0] for clip, rungs in self.ladders.items()}
        for clip, rung in picked.items():
            buffer = self.buffers[clip]
            buffer.use(rung.name)
            while buffer.seconds < self.target_buffer:
                size = int(rung.bits_per_second / (8 * self.fps))
                booked = self.link.reserve(size)
                arrives = booked.finishes_at + self.link.latency
                self.rate.record(size, max(1e-9, arrives - self._now))
                self._now = max(self._now, arrives)
                buffer.fill(self.frame_seconds, rung.utility(self.metric))

    def _report(self, seconds: float) -> Report:
        buffers = tuple(self.buffers.values())
        stalled = sum(buffer.stalled for buffer in buffers)
        frozen = sum(buffer.frozen for buffer in buffers)
        switches = sum(buffer.switches for buffer in buffers)
        filled = sum(buffer.filled_seconds for buffer in buffers)
        quality = (sum(buffer.quality_seconds for buffer in buffers) / filled
                   if filled else 0.0)
        score = (
            quality
            - self.stall_penalty * (stalled / max(1, len(buffers)))
            - self.freeze_penalty * (frozen / max(1, len(buffers)))
            - self.switch_penalty * switches / max(1, len(buffers))
        )
        return Report(
            seconds=seconds, buffers=buffers, link=self.link.observed(),
            stalled=stalled, frozen=frozen, switches=switches,
            mean_quality=quality, score=score,
        )


def render_table(rows: Sequence[tuple[str, Report]]) -> str:
    """One row per condition: what the viewer got, and what it scored."""
    lines = [f"{'condition':<18}{'stalled':>9}{'frozen':>9}{'switches':>10}"
             f"{'quality':>9}{'score':>9}"]
    lines.append("-" * len(lines[0]))
    for label, report in rows:
        lines.append(
            f"{label:<18}{report.stalled:>8.1f}s{report.frozen:>8.1f}s"
            f"{report.switches:>10}{report.mean_quality:>9.2f}"
            f"{report.score:>9.2f}"
        )
    return "\n".join(lines)


def main(argv=None) -> int:
    import argparse
    import json

    from .link import Link, Trace
    from .policy import measured_rungs

    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("bundle")
    parser.add_argument("--scene", help="only this scene's clips")
    parser.add_argument("--seconds", type=float, default=30.0)
    parser.add_argument("--capacity", type=float, action="append", default=None,
                        help="Mbit/s to play at; repeat to sweep")
    parser.add_argument("--trace", help="a two-column bandwidth trace to replay")
    parser.add_argument("--latency", type=float, default=0.02,
                        help="one-way delay in seconds")
    parser.add_argument("--target-buffer", type=float, default=3.0)
    parser.add_argument("--decision-interval", type=float, default=2.0)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)

    ladders = measured_rungs(args.bundle, scene=args.scene)
    if not ladders:
        print("no clip here has more than one rendition; nothing to adapt")
        return 0

    def play(link):
        return Playback(
            ladders, link, target_buffer=args.target_buffer,
            decision_interval=args.decision_interval,
            # A frozen virtual clock: the model advances time itself, and a real
            # one would let the machine's scheduling leak into the result.
            clock=None,
        ).run(args.seconds)

    rows = []
    if args.trace:
        trace = Trace.read(args.trace)
        rows.append((f"trace {args.trace.split('/')[-1]}",
                     play(Link(trace=trace, latency=args.latency,
                               clock=lambda: 0.0))))
    else:
        floor = sum(rungs[0].bits_per_second for rungs in ladders) / 1e6
        ceiling = sum(rungs[-1].bits_per_second for rungs in ladders) / 1e6
        rates = args.capacity or [ceiling * 1.2, ceiling * 0.5, floor, floor * 0.5]
        for mbit in rates:
            rows.append((f"{mbit:.1f} Mbit/s",
                         play(Link(capacity=mbit * 1e6, latency=args.latency,
                                   clock=lambda: 0.0))))

    if args.json:
        print(json.dumps([{"condition": label, **report.as_dict()}
                          for label, report in rows], indent=2))
        return 0
    print(f"{len(ladders)} panes, {args.seconds:.0f}s each, "
          f"target buffer {args.target_buffer:.0f}s, "
          f"deciding every {args.decision_interval:.0f}s\n")
    print(render_table(rows))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
