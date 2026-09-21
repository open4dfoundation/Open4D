"""Choosing a rung under a budget.

The decision this makes is the point of the ladder existing, so most of these
check the *shape* of the decision rather than a number: that it never overruns,
that it is exact rather than greedy, that it spends the whole budget when it
can, and that what it cannot fit is named instead of vanishing.
"""
from __future__ import annotations

import json

import pytest

from streamer import bundle, policy
from streamer.policy import Rung, choose

pytestmark = pytest.mark.cpu


# The measured `g_thomas` ladder, in bits per second at 30 fps.
LOW, MEDIUM, HIGH = 728_000, 2_376_000, 8_040_000
QUALITY = {"low": 41.60, "medium": 44.80, "default": 47.24}


def ladder(clip: str, *, rates=(LOW, MEDIUM, HIGH)):
    names = ("low", "medium", None)
    return [
        Rung(clip=clip, variant=name, bits_per_second=rate,
             quality={"psnr": QUALITY[name or "default"], "ssim": 0.99})
        for name, rate in zip(names, rates)
    ]


def ladders(count: int):
    return [ladder(f"pane{index}") for index in range(count)]


def names(selection):
    return sorted({choice.name for choice in selection.choices})


# ------------------------------------------------------------ never overrun ---


@pytest.mark.parametrize("budget", [0, 1e5, 7e5, 3e6, 1e7, 2.4e7, 3.3e7, 1e9])
def test_the_budget_is_never_exceeded(budget):
    """The one invariant that cannot bend: a chooser that overspends is worse
    than no chooser, because the overspend is what causes the stall."""
    selection = choose(ladders(4), budget=budget)
    assert selection.bits_per_second <= budget
    assert selection.headroom >= 0


def test_a_zero_budget_chooses_nothing_and_says_so():
    selection = choose(ladders(3), budget=0)
    assert selection.choices == ()
    assert len(selection.dropped) == 3


def test_no_ladders_is_not_an_error():
    assert choose([], budget=1e7).choices == ()


def test_a_negative_budget_is_refused():
    with pytest.raises(ValueError, match="must not be negative"):
        choose(ladders(1), budget=-1)


def test_the_quantum_must_be_positive():
    with pytest.raises(ValueError, match="quantum must be positive"):
        choose(ladders(1), budget=1e7, quantum=0)


# ---------------------------------------------------------------- the floor ---


def test_at_exactly_the_floor_every_clip_is_kept():
    """Boundary that a rounded-up cost gets wrong: eight clips each rounding up
    by under a quantum accumulate into one clip being dropped whose cheapest
    rung did in fact fit. Measured on `g_thomas`: seven panes instead of eight.
    """
    given = ladders(8)
    floor = sum(rungs[0].bits_per_second for rungs in given)
    selection = choose(given, budget=floor)
    assert selection.dropped == ()
    assert len(selection.choices) == 8
    assert names(selection) == ["low"]
    assert selection.bits_per_second == pytest.approx(floor)


def test_below_the_floor_clips_are_dropped_not_starved():
    """There is no rung cheaper than the cheapest, so something has to go.
    Showing everything at a rate that does not fit would overrun the budget."""
    given = ladders(4)
    floor = sum(rungs[0].bits_per_second for rungs in given)
    selection = choose(given, budget=floor / 2)
    assert selection.dropped
    assert len(selection.choices) + len(selection.dropped) == 4
    assert selection.bits_per_second <= floor / 2


def test_the_lowest_weighted_clip_is_dropped_first():
    """Which pane the viewer is looking at is the whole reason weights exist."""
    given = ladders(3)
    floor = sum(rungs[0].bits_per_second for rungs in given)
    selection = choose(
        given, budget=floor * 0.7,
        weights={"pane0": 10.0, "pane1": 5.0, "pane2": 0.1},
    )
    assert selection.dropped == ("pane2",)


# -------------------------------------------------------------- the ceiling ---


def test_at_exactly_the_ceiling_everything_is_at_its_best():
    """The mirror boundary: rounding up left one clip a rung down, giving up
    2.4 dB to save 80 kbit/s of phantom cost."""
    given = ladders(8)
    ceiling = sum(rungs[-1].bits_per_second for rungs in given)
    selection = choose(given, budget=ceiling)
    assert names(selection) == ["default"]
    assert selection.bits_per_second == pytest.approx(ceiling)


def test_surplus_budget_is_not_spent():
    given = ladders(4)
    ceiling = sum(rungs[-1].bits_per_second for rungs in given)
    selection = choose(given, budget=ceiling * 3)
    assert selection.bits_per_second == pytest.approx(ceiling)
    assert names(selection) == ["default"]


# ------------------------------------------------------------------- exact ---


def test_it_spreads_rather_than_spending_everything_on_one_clip():
    """Why the allocation is a dynamic program and not a greedy pass.

    Greedy "best quality per byte" promotes whatever it looks at first until
    the budget is gone. With rungs 3x apart that leaves one pane sharp and the
    rest at the floor, which is exactly the lopsided result a comparison view
    must not have.
    """
    selection = choose(ladders(4), budget=4 * MEDIUM)
    assert names(selection) == ["medium"]


def test_it_beats_every_uniform_choice_it_could_have_made():
    """The exactness claim, checked against the alternatives rather than
    asserted: no single rung applied to all four clips does better."""
    given = ladders(4)
    budget = 4 * MEDIUM + MEDIUM        # room to lift one clip
    selection = choose(given, budget=budget)
    for index in range(3):
        uniform = sum(rungs[index].bits_per_second for rungs in given)
        if uniform > budget:
            continue
        value = sum(rungs[index].utility("psnr") for rungs in given)
        assert selection.utility >= value


def test_a_richer_budget_never_does_worse():
    given = ladders(5)
    previous = -1.0
    for budget in (5e6, 1e7, 2e7, 4e7, 6e7):
        selection = choose(given, budget=budget)
        assert selection.utility >= previous
        previous = selection.utility


# ----------------------------------------------------------------- weights ---


def test_weight_decides_who_gets_the_headroom():
    given = ladders(3)
    budget = 3 * LOW + (MEDIUM - LOW)   # exactly one promotion affordable
    selection = choose(given, budget=budget, weights={"pane1": 20.0})
    assert selection.of("pane1").name == "medium"
    assert selection.of("pane0").name == "low"


# ------------------------------------------------------------ switch churn ---


def test_a_switch_penalty_holds_a_clip_where_it_was():
    """Churn is visible, so a marginal gain should not buy it. The gain from
    low to default here is 5.64 dB, so a larger penalty must hold."""
    given = ladders(4)
    ceiling = sum(rungs[-1].bits_per_second for rungs in given)
    previous = {f"pane{index}": "low" for index in range(4)}
    assert names(choose(given, budget=ceiling, previous=previous,
                        switch_penalty=1.0)) == ["default"]
    assert names(choose(given, budget=ceiling, previous=previous,
                        switch_penalty=8.0)) == ["low"]


def test_no_penalty_means_history_is_ignored():
    given = ladders(2)
    ceiling = sum(rungs[-1].bits_per_second for rungs in given)
    selection = choose(given, budget=ceiling,
                       previous={"pane0": "low", "pane1": "low"})
    assert names(selection) == ["default"]


# ------------------------------------------------------------ what it reads ---


def test_an_unmeasured_rung_is_never_chosen():
    """A rung with no measured quality has no basis for being preferred, and
    defaulting it to zero would look like a deliberate avoidance."""
    given = [[
        Rung(clip="c", variant="low", bits_per_second=LOW, quality={"psnr": 40.0}),
        Rung(clip="c", variant="high", bits_per_second=MEDIUM, quality={}),
    ]]
    selection = choose(given, budget=1e9)
    assert selection.of("c").name == "low"


def test_a_clip_with_only_unmeasured_rungs_is_dropped():
    given = [[Rung(clip="c", variant="x", bits_per_second=LOW, quality={})]]
    selection = choose(given, budget=1e9)
    assert selection.choices == ()
    assert selection.dropped == ("c",)


def test_ssim_can_be_optimised_instead():
    given = [[
        Rung(clip="c", variant="low", bits_per_second=LOW,
             quality={"psnr": 99.0, "ssim": 0.1}),
        Rung(clip="c", variant="high", bits_per_second=MEDIUM,
             quality={"psnr": 1.0, "ssim": 0.99}),
    ]]
    assert choose(given, budget=1e9, metric="psnr").of("c").name == "low"
    assert choose(given, budget=1e9, metric="ssim").of("c").name == "high"


def test_a_callable_metric_is_accepted():
    """Utility need not be either of the measured scores. The fixture's rungs
    differ in ssim so the callable has something to prefer -- with equal
    utility the cheapest rung wins, which would pass for the wrong reason."""
    given = [[
        Rung(clip="c", variant="low", bits_per_second=LOW,
             quality={"psnr": 40.0, "ssim": 0.90}),
        Rung(clip="c", variant=None, bits_per_second=MEDIUM,
             quality={"psnr": 44.0, "ssim": 0.99}),
    ]]
    selection = choose(given, budget=1e9,
                       metric=lambda quality: quality.get("ssim", 0.0) * 100)
    assert selection.of("c").name == "default"
    assert selection.of("c").utility == pytest.approx(99.0)


# ------------------------------------------------------- reading a bundle ---


def a_laddered_bundle(tmp_path, *, quality=True):
    from PIL import Image
    import numpy as np

    rng = np.random.default_rng(0)
    def frames(folder, count, size):
        made = []
        for index in range(count):
            relative = f"{folder}/frame_{index:04d}.png"
            target = tmp_path / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            Image.fromarray(
                (rng.random((size, size, 3)) * 255).astype("uint8")
            ).save(target)
            made.append(relative)
        return made

    default = frames("c", 3, 32)
    low = frames("c@low", 3, 8)
    low_bytes = sum((tmp_path / path).stat().st_size for path in low)
    variant = bundle.Variant(
        name="low", frames=low, bytes=low_bytes,
        quality={"psnr": 30.0, "ssim": 0.9} if quality else {},
    )
    bundle.write(tmp_path, title="t", source="s", fps=30, clips=[
        bundle.Clip(name="c", representation="pixels", scene="s1", method="m",
                    frames=default, variants=[variant.as_dict()],
                    detail={"quality": {"psnr": 45.0, "ssim": 0.99}}
                    if quality else {}),
        bundle.Clip(name="plain", representation="pixels", scene="s1",
                    method="m", frames=frames("plain", 3, 16)),
    ])
    return tmp_path


def test_reading_a_bundle_finds_the_ladder_and_measures_the_default(tmp_path):
    """The default rendition's rate is not in the manifest -- only a variant
    carries `bytes` -- so it is measured off disk."""
    found = policy.measured_rungs(a_laddered_bundle(tmp_path))
    assert len(found) == 1                      # only the laddered clip
    rungs = found[0]
    assert [rung.name for rung in rungs] == ["low", "default"]
    assert rungs[-1].bits_per_second > rungs[0].bits_per_second
    assert rungs[-1].quality["psnr"] == 45.0


def test_a_clip_with_one_rendition_is_not_a_choice(tmp_path):
    """Including it would let the budget be spent on something a chooser cannot
    change, which makes every decision look tighter than it is."""
    found = policy.measured_rungs(a_laddered_bundle(tmp_path))
    assert all(rungs[0].clip == "c" for rungs in found)


def test_a_bundle_is_required(tmp_path):
    with pytest.raises(FileNotFoundError, match="view.json"):
        policy.measured_rungs(tmp_path / "nothing")


def test_the_scene_filter_narrows_what_is_read(tmp_path):
    root = a_laddered_bundle(tmp_path)
    assert policy.measured_rungs(root, scene="s1")
    assert policy.measured_rungs(root, scene="elsewhere") == ()


def test_choosing_straight_off_a_bundle(tmp_path):
    root = a_laddered_bundle(tmp_path)
    rungs = policy.measured_rungs(root)
    cheap = choose(rungs, budget=rungs[0][0].bits_per_second)
    rich = choose(rungs, budget=1e12)
    assert cheap.of("c").name == "low"
    assert rich.of("c").name == "default"


# ------------------------------------------------------------------ output ---


def test_the_report_round_trips_as_json():
    payload = json.loads(json.dumps(choose(ladders(3), budget=1e7).as_dict()))
    assert payload["choices"][0]["rung"] in ("low", "medium", "default")
    assert payload["spent_mbit_s"] <= payload["budget_mbit_s"]


def test_the_table_shortens_a_shared_prefix():
    """Eight stations of one object share 19 of 21 characters, and a table of
    full names is a wall in which only the last two digits carry anything."""
    short = policy._short(["g_thomas-rerf-cam00", "g_thomas-rerf-cam01"])
    assert short["g_thomas-rerf-cam00"] == "cam00"


def test_a_single_name_is_left_alone():
    assert policy._short(["only"]) == {"only": "only"}


def test_the_table_marks_a_dropped_clip():
    given = ladders(4)
    floor = sum(rungs[0].bits_per_second for rungs in given)
    table = policy.render_table([choose(given, budget=floor / 3)])
    assert "-" in table
    assert "dropped at that budget" in table


def test_an_empty_sweep_says_so():
    assert "nothing to choose" in policy.render_table([])
