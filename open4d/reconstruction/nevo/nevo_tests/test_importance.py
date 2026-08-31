"""The importance pass.

The scatter half is pure tensor arithmetic and always runs. The marching half
needs a trained ReRF sequence and a GPU with room to work in; see
``conftest.py`` for how that is gated.
"""
from __future__ import annotations

import numpy as np
import pytest

torch = pytest.importorskip("torch")

from nevo.blocks import BlockGrid  # noqa: E402
from nevo_tests.conftest import needs_sequence, trained_config  # noqa: E402


# ------------------------------------------------------------------- scatter
def test_scatter_max_keeps_the_largest_weight_per_block():
    from nevo.importance import scatter_max

    grid = BlockGrid((8, 8, 8), 4)          # 2x2x2 = 8 blocks
    xyz_min = torch.zeros(3)
    xyz_max = torch.ones(3)
    # Two samples in the origin block, one in the far corner block.
    points = torch.tensor([[0.0, 0.0, 0.0], [0.05, 0.05, 0.05], [1.0, 1.0, 1.0]])
    weights = torch.tensor([0.2, 0.7, 0.4])
    scores = torch.zeros(grid.num_blocks)
    scatter_max(grid, points, weights, xyz_min, xyz_max, (8, 8, 8), "nearest", scores)
    assert scores[0].item() == pytest.approx(0.7)
    assert scores[-1].item() == pytest.approx(0.4)
    assert scores.gt(0).sum().item() == 2


def test_scatter_max_accumulates_across_calls():
    """One viewport is marched in ray chunks, so the running max has to survive
    being fed a piece at a time."""
    from nevo.importance import scatter_max

    grid = BlockGrid((4, 4, 4), 4)
    xyz_min, xyz_max = torch.zeros(3), torch.ones(3)
    scores = torch.zeros(grid.num_blocks)
    for weight in (0.1, 0.9, 0.3):
        scatter_max(
            grid,
            torch.tensor([[0.5, 0.5, 0.5]]),
            torch.tensor([weight]),
            xyz_min,
            xyz_max,
            (4, 4, 4),
            "nearest",
            scores,
            )
    assert scores.max().item() == pytest.approx(0.9)


def test_scatter_max_handles_an_empty_sample_list():
    from nevo.importance import scatter_max

    grid = BlockGrid((4, 4, 4), 4)
    scores = torch.zeros(grid.num_blocks)
    result = scatter_max(
        grid,
        torch.zeros(0, 3),
        torch.zeros(0),
        torch.zeros(3),
        torch.ones(3),
        (4, 4, 4),
        "nearest",
        scores,
        )
    assert result.abs().sum().item() == 0.0


def test_trilinear_assignment_covers_at_least_what_nearest_does():
    """A sample's nearest entry is always one of the eight it interpolates
    from, so trilinear can only ever mark more blocks, never fewer."""
    from nevo.importance import scatter_max

    torch.manual_seed(0)
    shape = (16, 16, 16)
    grid = BlockGrid(shape, 4)
    xyz_min, xyz_max = torch.zeros(3), torch.ones(3)
    points = torch.rand(500, 3)
    weights = torch.rand(500)
    nearest = torch.zeros(grid.num_blocks)
    trilinear = torch.zeros(grid.num_blocks)
    scatter_max(grid, points, weights, xyz_min, xyz_max, shape, "nearest", nearest)
    scatter_max(grid, points, weights, xyz_min, xyz_max, shape, "trilinear", trilinear)
    assert torch.all(trilinear >= nearest - 1e-12)
    assert trilinear.gt(0).sum() >= nearest.gt(0).sum()


def test_config_rejects_an_unknown_assignment():
    from nevo.importance import ImportanceConfig

    with pytest.raises(ValueError):
        ImportanceConfig(assignment="bilinear")
    with pytest.raises(ValueError):
        ImportanceConfig(ray_chunk=0)


# ------------------------------------------------------- against a real model
@pytest.fixture(scope="module")
def scored():
    config = trained_config()
    if config is None:
        pytest.skip("no trained sequence")
    from nevo import rerf_env

    rerf_env.activate()
    import json

    from nevo.importance import ImportanceConfig
    from nevo.sequence import ReRFSequence
    from nevo.viewports import sample_viewports

    sequence = ReRFSequence(config)
    frames = sequence.available_frames()
    if not frames:
        pytest.skip("sequence has no trained frames yet")
    frame = sequence.frame(frames[0])
    with open(sequence.corpus_dir / "nevo_corpus.json") as handle:
        manifest = json.load(handle)
    reference = manifest["cameras"][0]
    radius = float(np.linalg.norm(np.asarray(reference["c2w_normalised"])[:3, 3]))
    cameras = sample_viewports(
        4,
        manifest["xyz_min"],
        manifest["xyz_max"],
        reference_radius=radius,
        width=320,
        height=240,
        focal=float(reference["fx"]) / 4.0,
        seed=11,
    )
    return sequence, frame, cameras, ImportanceConfig


@needs_sequence
def test_marching_reproduces_rerfs_own_forward_pass(scored):
    """The whole of step 2 rests on this: our transcription of ReRF's ray
    marching produces the identical weight sequence, so the weights we scatter
    are the ones that actually rendered the frame."""
    from nevo.importance import check_against_rerf

    sequence, frame, cameras, _ = scored
    result = check_against_rerf(sequence, frame, cameras[0])
    assert result["agrees"], result
    assert result["samples_ours"] > 0


@needs_sequence
def test_importance_is_a_weight_so_it_stays_in_the_unit_interval(scored):
    from nevo.importance import ImportanceScorer

    sequence, frame, cameras, ImportanceConfig = scored
    scores = ImportanceScorer(sequence, frame, ImportanceConfig(block_size=8)).score(cameras[0])
    assert float(scores.min()) >= 0.0
    assert float(scores.max()) <= 1.0 + 1e-6
    assert float(scores.max()) > 0.1, "no voxel was visible at all"


@needs_sequence
def test_codec_empty_blocks_can_still_render(scored):
    """ReRF's two "is there anything here" tests disagree, on purpose.

    The codec keeps a block when some entry has raw density above ~3.39
    (``softplus(d - 4.1) > 0.4``). The renderer keeps a *sample* when its alpha
    clears ``fast_color_thres = 1e-4``, which for ``act_shift = -4.595`` is raw
    density above about -4.6. Six orders of magnitude apart: low-density haze
    renders but is never transmitted.

    That is ReRF's own behaviour, not something NeVo introduces, and it bounds
    what block-level filtering can preserve -- so it is pinned rather than
    asserted away. :func:`unscored_weight_outside_codec_mask` reports how much
    of it there is; if this ever starts failing, the codec mask and the
    renderer have been reconciled and the CDF's denominator should be revisited.
    """
    from nevo.importance import ImportanceScorer

    sequence, frame, cameras, ImportanceConfig = scored
    scorer = ImportanceScorer(sequence, frame, ImportanceConfig(block_size=8))
    scores = scorer.score(cameras[0]).cpu().numpy()
    occupied = scorer.occupancy.cpu().numpy()
    assert float(scores[occupied].max(initial=0.0)) > 0.5, "the subject did not render"
    outside = scores[~occupied]
    assert float(outside.max(initial=0.0)) > 0.0, (
        "the codec mask and the renderer now agree; revisit the CDF denominator"
    )
    # Whatever leaks past the codec mask is a minority of the visible weight.
    assert outside.sum() < scores[occupied].sum()


@needs_sequence
def test_finer_blocks_leave_a_longer_tail(scored):
    """Importance is a max over a block, so coarsening can only raise it. This
    is why the fraction below a threshold is a statement about granularity as
    much as about content."""
    from nevo.importance import ImportanceScorer

    sequence, frame, cameras, ImportanceConfig = scored
    fractions = {}
    for block_size in (1, 8):
        scorer = ImportanceScorer(sequence, frame, ImportanceConfig(block_size=block_size))
        scores = scorer.score(cameras[0]).cpu().numpy()[scorer.occupancy.cpu().numpy()]
        fractions[block_size] = float((scores < 0.025).mean())
    assert fractions[1] > fractions[8]
