"""Dropping voxels the way ReRF's decoder sees a block that never arrived."""
from __future__ import annotations

import numpy as np
import pytest

torch = pytest.importorskip("torch")

from nevo.blocks import BlockGrid  # noqa: E402
from nevo.filtering import ABSENT_DENSITY, entry_mask_from_blocks  # noqa: E402
from nevo_tests.conftest import needs_sequence, trained_config  # noqa: E402


def test_entry_mask_expands_each_block_over_its_entries():
    grid = BlockGrid((4, 4, 4), 2)          # 2x2x2 blocks
    keep = torch.zeros(grid.num_blocks, dtype=torch.bool)
    keep[grid.block_index(torch.tensor([[0, 0, 0]]))] = True
    mask = entry_mask_from_blocks(grid, keep)
    assert mask.shape == (4, 4, 4)
    assert int(mask.sum()) == 8
    assert bool(mask[0, 0, 0]) and bool(mask[1, 1, 1])
    assert not bool(mask[2, 0, 0])


def test_entry_mask_trims_the_padding_off_a_ragged_grid():
    """A 9-entry axis pads to 16; the expansion must come back at 9, not 16,
    or it will not line up with the density grid it indexes."""
    grid = BlockGrid((9, 5, 3), 8)
    keep = torch.ones(grid.num_blocks, dtype=torch.bool)
    mask = entry_mask_from_blocks(grid, keep)
    assert mask.shape == (9, 5, 3)
    assert bool(mask.all())


def test_entry_mask_round_trips_every_block_index():
    grid = BlockGrid((6, 10, 4), 2)
    for block in range(grid.num_blocks):
        keep = torch.zeros(grid.num_blocks, dtype=torch.bool)
        keep[block] = True
        mask = entry_mask_from_blocks(grid, keep)
        entries = torch.nonzero(mask)
        if entries.numel() == 0:
            continue  # a block that is entirely padding
        assert set(grid.block_index(entries).tolist()) == {block}


def test_absent_density_is_the_decoders_value_not_zero():
    """Zeroing a dropped block would paint fog: raw density 0 activates through
    softplus(0 - 4.595) to a clearly visible alpha, whereas -4.1 (what
    rerf_render.py fills an undelivered block with) does not."""
    act_shift = -4.595119850134584
    interval = 1.0
    def alpha(density):
        return 1.0 - np.exp(-float(np.log1p(np.exp(density + act_shift))) * interval)

    assert alpha(0.0) > 1e-2
    assert alpha(ABSENT_DENSITY) < 1e-3


# ------------------------------------------------------- against a real model
@needs_sequence
def test_dropping_voxels_restores_the_model_exactly():
    """The model is shared with the caller's sequence. A leaked mutation would
    silently corrupt every later render of the same frame, and the symptom -- a
    frame that renders slightly wrong the second time -- is nasty to trace."""
    from nevo import rerf_env

    rerf_env.activate()
    from nevo.filtering import voxels_dropped
    from nevo.sequence import ReRFSequence

    sequence = ReRFSequence(trained_config())
    frames = sequence.available_frames()
    if not frames:
        pytest.skip("sequence has no trained frames yet")
    frame = sequence.frame(frames[0])
    grid = BlockGrid(frame.grid_shape, 8)
    before_density = frame.model.density.data.clone()
    before_k0 = frame.model.k0.k0.data.clone()

    keep = torch.zeros(grid.num_blocks, dtype=torch.bool, device=before_k0.device)
    keep[: grid.num_blocks // 2] = True
    with voxels_dropped(frame, grid, keep) as model:
        assert not torch.equal(model.density.data, before_density), "nothing was dropped"
        assert float(model.density.data.min()) <= ABSENT_DENSITY + 1e-6

    assert torch.equal(frame.model.density.data, before_density)
    assert torch.equal(frame.model.k0.k0.data, before_k0)


@needs_sequence
def test_dropping_nothing_leaves_the_render_unchanged():
    """Keeping every block must be a no-op on the pixels.

    Not *bit*-identical: DVGO accumulates a pixel's colour with
    ``torch_scatter.segment_coo``, whose atomic adds run in whatever order the
    GPU schedules, so two renders of the same model differ in the last couple
    of float bits. The bound below is far tighter than any filtering effect and
    is measured against a repeat render of the untouched model, so it fails if
    the mask leaks rather than if the GPU is merely non-deterministic.
    """
    from nevo import rerf_env

    rerf_env.activate()
    from nevo.filtering import voxels_dropped
    from nevo.render import render_view, training_view
    from nevo.sequence import ReRFSequence

    sequence = ReRFSequence(trained_config())
    frames = sequence.available_frames()
    if not frames:
        pytest.skip("sequence has no trained frames yet")
    frame = sequence.frame(frames[0])
    grid = BlockGrid(frame.grid_shape, 8)
    camera, _ = training_view(sequence, frames[0], 0)
    reference = render_view(sequence, frame, camera)
    repeated = render_view(sequence, frame, camera)
    noise = float(np.abs(reference - repeated).max())

    keep = torch.ones(grid.num_blocks, dtype=torch.bool, device=frame.density.device)
    with voxels_dropped(frame, grid, keep):
        kept_everything = render_view(sequence, frame, camera)
    difference = float(np.abs(reference - kept_everything).max())
    assert difference <= max(noise, 1e-5), (difference, noise)
