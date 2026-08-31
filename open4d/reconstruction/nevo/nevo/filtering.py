"""Drop the unimportant voxels and look at what changed.

Step 2 produces a number per voxel. The claim that hangs off it -- that most of
them can go without anyone noticing -- is a claim about pixels, so it is worth
checking as pixels rather than only as a CDF.

:func:`preview` renders a viewport twice from the same frame, once with the
whole grid and once with every block below a threshold removed, and scores the
pair. "Removed" means what ReRF's decoder means by a block that never arrived:
``rerf_render.py`` initialises missing blocks to a raw density of -4.1 and zero
features, so that is what gets written in, rather than a zero density (which
is *not* empty -- it activates to a substantial alpha).

This is a diagnostic for step 2, not the streaming simulation. It filters a
frame in place with perfect knowledge of the viewport being rendered; the real
system has to decide from a *predicted* viewport several frames ahead, which
is what step 4 is for.
"""
from __future__ import annotations

import contextlib
from dataclasses import dataclass
import numpy as np

from . import rerf_env
from .blocks import BlockGrid
from .cameras import Camera

ABSENT_DENSITY = -4.1
"""What ReRF's decoder fills an undelivered block's density with.

``rerf_render.py``: ``former_rec[:, 0, :] = former_rec[:, 0, :] - 4.1`` over a
zero-initialised buffer. Not 0.0 -- raw density 0 activates through
``softplus(0 - 4.595)`` to a visible alpha, so zeroing a block would paint fog
where the paper's filter leaves nothing.
"""


def entry_mask_from_blocks(grid: BlockGrid, block_mask):
    """Expand a per-block bool to a per-entry bool over the unpadded grid."""
    bx, by, bz = grid.blocks_shape
    block = grid.block_size
    expanded = (
        block_mask.reshape(bx, by, bz)
        .repeat_interleave(block, dim=0)
        .repeat_interleave(block, dim=1)
        .repeat_interleave(block, dim=2)
    )
    x, y, z = grid.grid_shape
    return expanded[:x, :y, :z]


@contextlib.contextmanager
def voxels_dropped(frame, grid: BlockGrid, keep_blocks):
    """Temporarily strip the blocks outside ``keep_blocks`` from ``frame.model``.

    A context manager because the model is shared with the caller's
    :class:`~nevo.sequence.ReRFSequence`; leaving a filtered grid behind would
    silently corrupt every later render of the same frame.
    """
    model = frame.model
    keep = entry_mask_from_blocks(grid, keep_blocks)
    original_density = model.density.data.clone()
    original_k0 = model.k0.k0.data.clone()
    original_former = None
    if hasattr(model.k0, "former_k0_cur"):
        original_former = model.k0.former_k0_cur.clone()
    try:
        drop = ~keep
        model.density.data[0, 0][drop] = ABSENT_DENSITY
        model.k0.k0.data[0, :, drop] = 0.0
        if original_former is not None:
            # A P-frame renders `former_k0_cur + k0`; zeroing only the residual
            # would leave the predecessor's features behind, which is not what
            # a dropped block looks like to the decoder.
            model.k0.former_k0_cur[0, :, drop] = 0.0
        yield model
    finally:
        model.density.data.copy_(original_density)
        model.k0.k0.data.copy_(original_k0)
        if original_former is not None:
            model.k0.former_k0_cur.copy_(original_former)


@dataclass
class FilterPreview:
    full: np.ndarray
    filtered: np.ndarray
    difference: np.ndarray
    psnr: float
    ssim: float
    kept_blocks: int
    total_blocks: int
    threshold: float

    @property
    def dropped_fraction(self) -> float:
        return 1.0 - self.kept_blocks / self.total_blocks


def preview(
    sequence,
    frame,
    camera: Camera,
    scores,
    grid: BlockGrid,
    threshold: float,
    occupancy=None,
) -> FilterPreview:
    """Render ``camera`` with and without the sub-threshold blocks."""
    rerf_env.activate()
    with rerf_env.rerf_cwd():
        import torch
        from lib import utils

    from .render import psnr, render_view

    keep = scores >= threshold
    if occupancy is not None:
        # Blocks the codec would not have sent are absent either way, so they
        # are not part of what filtering chooses to drop.
        keep = keep & occupancy
    considered = occupancy if occupancy is not None else torch.ones_like(keep)

    full = render_view(sequence, frame, camera)
    with voxels_dropped(frame, grid, keep):
        filtered = render_view(sequence, frame, camera)

    difference = np.abs(full - filtered)
    return FilterPreview(
        full=full,
        filtered=filtered,
        difference=difference,
        psnr=psnr(filtered, full),
        ssim=float(utils.rgb_ssim(filtered, full, max_val=1.0)),
        kept_blocks=int(keep.sum().item()),
        total_blocks=int(considered.sum().item()),
        threshold=threshold,
    )
