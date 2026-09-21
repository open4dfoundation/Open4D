"""Choosing a rung: what to send when you cannot send everything.

The last piece of the loop. `bundle` lets a clip carry quality rungs, `metrics`
measures what each one costs and buys, and this decides. Without it a ladder is
data nobody reads: playback takes the default rendition every time, however
little bandwidth there is.

The problem is a **multiple-choice knapsack**. Several clips play at once -- a
comparison view is four panes, a wall is nine -- each offering one rung from
its own ladder, and the total has to fit a budget. Maximise the weighted
quality of what is chosen.

Solved exactly, by dynamic programming over a quantised budget, rather than
greedily. Greedy "best quality per byte" is the standard approximation and it
is wrong in a way that matters here: with rungs this coarse (3.2x and 10.6x
apart) a greedy pass will spend everything on the first clip it considers and
leave the rest at their floor, which is exactly the lopsided allocation a
comparison view must not have. The problem size makes exactness free -- nine
clips times three rungs is nothing.

What this deliberately does **not** model is buffering. A real client also
tracks per-clip buffer occupancy, distinguishes a deliberate freeze from a
stall, and penalises churn across segments. Those need a link that can starve
one, and this repository has no constrained transport yet: everything runs on
loopback. Building stall accounting now would mean writing policy against a
situation that cannot be produced or measured, so what is here is the
allocation, and `switch_penalty` is the one piece of dynamics that can be
tested without a network.

Utility is quality in dB by default, which is a choice worth naming. Summing
decibels is not physically meaningful -- they are a log scale -- but it is what
the ABR literature optimises and it has the right shape: the gain from a bad
rung to a mediocre one exceeds the gain from a good one to a slightly better
one. Pass ``metric="ssim"`` for a bounded alternative, or a callable for
anything else.
"""
from __future__ import annotations

import math
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Iterable, Mapping, Sequence

from . import bundle

#: Budget resolution for the dynamic program, in bits per second. Finer costs
#: proportionally more time for a decision no one could act on: 10 kbit/s is
#: well below the difference between any two rungs here.
QUANTUM = 10_000

#: Utility of a rendition with no measured quality. Chosen so an unmeasured
#: rung is never preferred to a measured one, rather than defaulting to zero
#: and being silently avoided -- which would look like a policy decision.
UNMEASURED = float("-inf")


@dataclass(frozen=True)
class Rung:
    """One rendition of one clip, as a chooser sees it."""

    clip: str
    #: ``None`` for the clip's default rendition.
    variant: str | None
    bits_per_second: float
    #: Measured, from `metrics`. Empty when nothing has scored this rendition.
    quality: Mapping[str, float]

    @property
    def name(self) -> str:
        return self.variant or "default"

    def utility(self, metric: str | Callable[[Mapping[str, float]], float]) -> float:
        if callable(metric):
            return metric(self.quality)
        value = self.quality.get(metric)
        return UNMEASURED if value is None else float(value)


@dataclass(frozen=True)
class Choice:
    """The rung picked for one clip."""

    clip: str
    variant: str | None
    bits_per_second: float
    utility: float
    weight: float

    @property
    def name(self) -> str:
        return self.variant or "default"


@dataclass(frozen=True)
class Selection:
    """What a chooser decided, and what it could not fit."""

    choices: tuple[Choice, ...]
    #: Clips left out because even their cheapest rung did not fit. Named
    #: rather than silently omitted: a viewer showing three of four panes has
    #: to be able to say why the fourth is missing.
    dropped: tuple[str, ...]
    budget: float
    bits_per_second: float
    utility: float

    @property
    def headroom(self) -> float:
        return self.budget - self.bits_per_second

    def of(self, clip: str) -> Choice:
        for choice in self.choices:
            if choice.clip == clip:
                return choice
        raise KeyError(f"{clip!r} was not chosen for; dropped: {self.dropped}")

    def as_dict(self) -> dict[str, Any]:
        return {
            "budget_mbit_s": round(self.budget / 1e6, 4),
            "spent_mbit_s": round(self.bits_per_second / 1e6, 4),
            "utility": round(self.utility, 4),
            "dropped": list(self.dropped),
            "choices": [
                {
                    "clip": choice.clip, "rung": choice.name,
                    "mbit_s": round(choice.bits_per_second / 1e6, 4),
                    "utility": round(choice.utility, 3),
                    "weight": choice.weight,
                }
                for choice in self.choices
            ],
        }


def rungs_of(clip: bundle.Clip | Mapping[str, Any], fps: int) -> tuple[Rung, ...]:
    """Every rendition of a clip, cheapest first, as `Rung` objects.

    The default rendition is one of them. It has no variant entry, so its rate
    comes from the clip's own frames and its quality from
    ``detail["quality"]`` -- where `metrics.write_back` puts it.
    """
    name = clip.name if isinstance(clip, bundle.Clip) else clip["name"]
    frames = clip.frames if isinstance(clip, bundle.Clip) else clip.get("frames") or []
    detail = clip.detail if isinstance(clip, bundle.Clip) else clip.get("detail") or {}
    found = []
    default_bytes = detail.get("bytes_per_frame")
    if frames and default_bytes:
        found.append(Rung(
            clip=name, variant=None,
            bits_per_second=float(default_bytes) * 8 * fps,
            quality=dict(detail.get("quality") or {}),
        ))
    for variant in bundle.variants_of(clip):
        found.append(Rung(
            clip=name, variant=variant.name,
            bits_per_second=variant.bitrate(fps),
            quality=dict(variant.quality),
        ))
    return tuple(sorted(found, key=lambda rung: rung.bits_per_second))


def measured_rungs(
    bundle_dir: Path | str, *, scene: str | None = None, method: str | None = None
) -> tuple[tuple[Rung, ...], ...]:
    """Every laddered clip's rungs, read off a bundle.

    A clip's default rendition needs a byte count that the manifest does not
    carry, so it is measured off disk here -- once, and only for clips that
    have a ladder at all. A clip with one rendition is not a choice and is left
    out: including it would let the budget be spent on something a chooser
    cannot change.
    """
    root = Path(bundle_dir).expanduser().resolve()
    index = bundle.read(root)
    if not index:
        raise FileNotFoundError(f"{root} has no {bundle.INDEX_NAME}")
    fps = index.get("fps", 30)

    ladders = []
    for entry in index.get("clips", []):
        if not entry.get("variants"):
            continue
        if scene is not None and entry.get("scene") != scene:
            continue
        if method is not None and entry.get("method") != method:
            continue
        frames = entry.get("frames") or []
        detail = dict(entry.get("detail") or {})
        if frames and "bytes_per_frame" not in detail:
            total = sum((root / path).stat().st_size for path in frames)
            detail["bytes_per_frame"] = total / len(frames)
        ladders.append(rungs_of(dict(entry, detail=detail), fps))
    return tuple(ladder for ladder in ladders if len(ladder) > 1)


def choose(
    ladders: Iterable[Sequence[Rung]],
    *,
    budget: float,
    weights: Mapping[str, float] | None = None,
    metric: str | Callable[[Mapping[str, float]], float] = "psnr",
    previous: Mapping[str, str | None] | None = None,
    switch_penalty: float = 0.0,
    quantum: int = QUANTUM,
) -> Selection:
    """Pick one rung per ladder, maximising weighted utility under ``budget``.

    ``budget`` is bits per second. ``weights`` scales a clip's utility by how
    much it matters -- which pane is being looked at, in a viewer. ``previous``
    and ``switch_penalty`` discourage changing a clip's rung from one decision
    to the next: churn is visible, so a marginal gain should not buy it.

    Returns the selection. When even the cheapest rung of everything exceeds
    the budget, the lowest-weight clips are dropped until it fits, and they are
    named in ``Selection.dropped`` -- there is no rung cheap enough, and
    pretending otherwise would overrun the budget silently.
    """
    ladders = [tuple(ladder) for ladder in ladders if ladder]
    if budget < 0:
        raise ValueError("budget must not be negative")
    if quantum <= 0:
        raise ValueError("quantum must be positive")
    weights = dict(weights or {})
    previous = dict(previous or {})

    def weight(clip: str) -> float:
        return float(weights.get(clip, 1.0))

    def score(rung: Rung) -> float:
        value = rung.utility(metric)
        if value == UNMEASURED:
            return UNMEASURED
        value *= weight(rung.clip)
        if switch_penalty and rung.clip in previous:
            if previous[rung.clip] != rung.variant:
                value -= switch_penalty * weight(rung.clip)
        return value

    def quanta(rate: float) -> int:
        """A rate as whole budget quanta, rounded up.

        Up, not nearest: a rung must never be costed below what it spends, or a
        selection can overrun the budget by a quantum per clip.
        """
        return int(math.ceil(rate / quantum))

    # Deficit: not even the floor fits. Drop by lowest weight, then by the most
    # expensive floor, so what goes is what matters least and costs most.
    #
    # Costed in *quanta*, matching the dynamic program below. Comparing exact
    # rates here and rounded costs there lets the two disagree at the margin:
    # the floor would look affordable, the program would find it infeasible,
    # and every clip would come back dropped with no reason given.
    dropped: list[str] = []
    every_ladder = list(ladders)
    slots = int(budget // quantum)
    while ladders and sum(quanta(ladder[0].bits_per_second) for ladder in ladders) > slots:
        victim = min(
            ladders,
            key=lambda ladder: (weight(ladder[0].clip),
                                -ladder[0].bits_per_second,
                                ladder[0].clip),
        )
        dropped.append(victim[0].clip)
        ladders = [ladder for ladder in ladders if ladder is not victim]

    # No early return when everything was dropped: the repair pass below is
    # what puts back a clip that the rounded-up floor made look unaffordable,
    # and with one clip in the ladder that is *every* clip. Skipping to a
    # result here reported an empty selection for a budget that fits.

    # Exact DP over the quantised budget. cell[b] is the best utility using at
    # most b quanta, with a back-pointer per clip so the choice can be recovered.
    best = [0.0] * (slots + 1)
    taken: list[list[Rung | None]] = [[None] * (slots + 1)]
    for ladder in ladders:
        nxt = [-math.inf] * (slots + 1)
        picks: list[Rung | None] = [None] * (slots + 1)
        for rung in ladder:
            value = score(rung)
            if value == UNMEASURED:
                continue
            cost = quanta(rung.bits_per_second)
            for spend in range(cost, slots + 1):
                candidate = best[spend - cost] + value
                if candidate > nxt[spend]:
                    nxt[spend] = candidate
                    picks[spend] = rung
        # Monotone fill: a larger budget can always do at least as well, and
        # carrying the better cell forward is what makes the back-pointers
        # recoverable without storing the whole table per clip.
        for spend in range(1, slots + 1):
            if nxt[spend - 1] > nxt[spend]:
                nxt[spend] = nxt[spend - 1]
                picks[spend] = picks[spend - 1]
        best = nxt
        taken.append(picks)

    # Walk the back-pointers from the fullest cell.
    choices: list[Choice] = []
    spend = slots
    for index in range(len(ladders), 0, -1):
        rung = taken[index][spend]
        if rung is None:
            # No affordable measured rung for this clip; it is dropped rather
            # than shown at an unknown quality.
            dropped.append(ladders[index - 1][0].clip)
            continue
        choices.append(Choice(
            clip=rung.clip, variant=rung.variant,
            bits_per_second=rung.bits_per_second,
            utility=rung.utility(metric), weight=weight(rung.clip),
        ))
        spend -= quanta(rung.bits_per_second)

    choices.reverse()
    choices, dropped = _repair(
        choices, every_ladder, budget, score, metric, dropped, weight)
    return Selection(
        choices=tuple(choices),
        dropped=tuple(sorted(set(dropped))),
        budget=budget,
        bits_per_second=sum(choice.bits_per_second for choice in choices),
        utility=sum(choice.utility * choice.weight for choice in choices),
    )


def _repair(
    choices: list[Choice],
    ladders: Sequence[Sequence[Rung]],
    budget: float,
    score: Callable[[Rung], float],
    metric,
    dropped: Sequence[str],
    weight: Callable[[str], float],
) -> tuple[tuple[Choice, ...], list[str]]:
    """Spend headroom the quantisation hid, then stop.

    Costs are rounded **up** to whole quanta so a selection can never overrun
    the budget. The price is up to one quantum of phantom cost per clip, and
    across eight clips it accumulates into a decision that is visibly wrong at
    the boundaries. Both were measured on `g_thomas`:

    * at exactly the all-default budget, the program left one clip a rung down,
      giving up 2.4 dB to save 80 kbit/s that did not exist;
    * at exactly the all-lowest budget, the deficit check dropped a clip whose
      cheapest rung did fit, showing seven panes instead of eight.

    So two passes against the *exact* rates: put back a clip that fits after
    all, then upgrade while anything still fits. Greedy is safe here in a way
    it is not for the allocation itself -- each step strictly increases utility
    and strictly decreases headroom, so it terminates, and neither pass can do
    worse than what the program returned.
    """
    by_clip = {ladder[0].clip: ladder for ladder in ladders}
    dropped = list(dropped)

    # Re-add, cheapest first, so the most recoverable clip goes back first.
    still_dropped: list[str] = []
    for clip in sorted(dropped, key=lambda name: (
            by_clip[name][0].bits_per_second if name in by_clip else 0.0)):
        ladder = by_clip.get(clip)
        spent = sum(choice.bits_per_second for choice in choices)
        if ladder is None or spent + ladder[0].bits_per_second > budget:
            still_dropped.append(clip)
            continue
        floor_rung = ladder[0]
        if floor_rung.utility(metric) == UNMEASURED:
            still_dropped.append(clip)      # unmeasured: no basis to show it
            continue
        choices.append(Choice(
            clip=clip, variant=floor_rung.variant,
            bits_per_second=floor_rung.bits_per_second,
            utility=floor_rung.utility(metric), weight=weight(clip),
        ))
    dropped = still_dropped
    if not choices:
        return (), dropped
    # The rung each clip is currently on, from the ladder itself rather than
    # rebuilt from the Choice: `score` may apply a weight and a switch penalty,
    # and a reconstructed rung would be scored against the wrong history.
    current = {
        choice.clip: next(
            rung for rung in by_clip[choice.clip] if rung.variant == choice.variant
        )
        for choice in choices
        if choice.clip in by_clip
    }
    picked = {choice.clip: choice for choice in choices}

    while True:
        spent = sum(choice.bits_per_second for choice in picked.values())
        best_gain, best_upgrade = 0.0, None
        for clip, choice in picked.items():
            if clip not in current:
                continue
            here = score(current[clip])
            for rung in by_clip[clip]:
                extra = rung.bits_per_second - choice.bits_per_second
                if extra <= 0 or spent + extra > budget:
                    continue
                gain = score(rung) - here
                if gain > best_gain:
                    best_gain, best_upgrade = gain, (clip, rung)
        if best_upgrade is None:
            break
        clip, rung = best_upgrade
        current[clip] = rung
        picked[clip] = Choice(
            clip=clip, variant=rung.variant,
            bits_per_second=rung.bits_per_second,
            utility=rung.utility(metric), weight=picked[clip].weight,
        )

    order = [choice.clip for choice in choices]
    return tuple(picked[clip] for clip in order), dropped


def _short(names: Sequence[str]) -> dict[str, str]:
    """Clip names with their shared prefix removed.

    Eight stations of one object share 19 of 21 characters, so a table of full
    names is a wall in which only the last two digits carry information.
    """
    if len(names) < 2:
        return {name: name for name in names}
    shared = 0
    for position in range(min(len(name) for name in names)):
        if len({name[position] for name in names}) > 1:
            break
        shared = position + 1
    # Back off to the last separator, so a name is cut at a boundary rather
    # than mid-token.
    prefix = names[0][:shared]
    cut = max(prefix.rfind("-"), prefix.rfind("_"), prefix.rfind("/")) + 1
    return {name: (name[cut:] or name) for name in names}


def render_table(selections: Sequence[Selection]) -> str:
    """One row per budget, showing which rung each clip landed on."""
    if not selections:
        return "nothing to choose between"
    clips: list[str] = []
    for selection in selections:
        for choice in selection.choices:
            if choice.clip not in clips:
                clips.append(choice.clip)
    short = _short(clips)
    width = max(9, max((len(short[name]) for name in clips), default=9)) + 2

    header = (f"{'budget':>9}{'spent':>9}{'mean':>8}  "
              + "".join(f"{short[name]:<{width}}" for name in clips))
    lines = [header, "-" * len(header)]
    for selection in selections:
        picked = {choice.clip: choice for choice in selection.choices}
        mean = (
            sum(choice.utility for choice in selection.choices) / len(selection.choices)
            if selection.choices else float("nan")
        )
        row = (f"{selection.budget / 1e6:>8.1f}M{selection.bits_per_second / 1e6:>8.1f}M"
               f"{mean:>8.2f}  ")
        for name in clips:
            choice = picked.get(name)
            row += f"{(choice.name if choice else '-'):<{width}}"
        lines.append(row.rstrip())
    dropped = {name for selection in selections for name in selection.dropped}
    if dropped:
        lines.append("")
        lines.append("a dash is a clip dropped at that budget: even its cheapest "
                     "rung did not fit")
    return "\n".join(lines)


def main(argv=None) -> int:
    import argparse
    import json

    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("bundle", help="a bundle directory holding view.json")
    parser.add_argument("--scene", help="only this scene's clips")
    parser.add_argument("--method", help="only this method's clips")
    parser.add_argument("--budget", type=float, action="append", default=None,
                        help="Mbit/s to spend; repeat to sweep several")
    parser.add_argument("--metric", default="psnr", choices=("psnr", "ssim"))
    parser.add_argument("--switch-penalty", type=float, default=0.0,
                        help="utility charged for moving a clip off its previous rung")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)

    ladders = measured_rungs(args.bundle, scene=args.scene, method=args.method)
    if not ladders:
        print("no clip in this bundle has more than one rendition; nothing to choose")
        return 0

    floor = sum(ladder[0].bits_per_second for ladder in ladders)
    ceiling = sum(ladder[-1].bits_per_second for ladder in ladders)
    budgets = (
        [value * 1e6 for value in args.budget] if args.budget
        else [floor * 0.5, floor, (floor + ceiling) / 2, ceiling, ceiling * 1.5]
    )

    selections = [
        choose(ladders, budget=budget, metric=args.metric,
               switch_penalty=args.switch_penalty)
        for budget in budgets
    ]
    if args.json:
        print(json.dumps([s.as_dict() for s in selections], indent=2))
        return 0

    print(f"{len(ladders)} laddered clips, "
          f"{floor / 1e6:.1f}-{ceiling / 1e6:.1f} Mbit/s between all-lowest and "
          f"all-highest\n")
    print(render_table(selections))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
