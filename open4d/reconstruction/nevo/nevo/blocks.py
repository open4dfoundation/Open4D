"""Block geometry of a ReRF feature volume.

ReRF does not transmit individual grid entries. ``codec/compress.py`` stacks
density and the 12 colour features into a ``[1, 13, X, Y, Z]`` volume,
zero-pads each spatial axis up to a multiple of ``voxel_size`` (8), splits it
into ``8x8x8`` blocks, drops the blocks that hold no occupied entry, and
DCT+entropy-codes the survivors. The kept/dropped decision is shipped as a
packed bitfield (``mask_<frame>.rerf``).

So the *feature voxel* that NeVo talks about -- the thing whose neural
visibility gets scored, that gets filtered out of a transmission, and that a
packet carries a piece of -- is one of these blocks, not one grid entry. That
is also what Figure 6 of the NeVo paper draws: a grid far coarser than the
250^3 feature grid, a few dozen cells across the subject.

This module reproduces that geometry exactly, including ``split_volume``'s
x-major block ordering, so a block index here is the same integer as the bit
position in ReRF's own mask and the same slot in its bitstream. Nothing
downstream has to guess an ordering.

``block_size=1`` degenerates to per-entry accounting, which is useful for
showing how much of the paper's long tail is an artefact of granularity.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Sequence, Tuple

import torch

RERF_BLOCK_SIZE = 8
"""``voxel_size`` in codec/compress.py and codec/compress_utils.py."""

_DENSITY_SHIFT = 4.1
_OCCUPANCY_THRESHOLD = 0.4
"""The codec's own occupancy test, ``get_masks``: an entry counts as occupied
when ``softplus(density - 4.1) > 0.4``, i.e. raw density above ~3.39. Note
this is a hardcoded constant upstream, *not* the model's ``act_shift`` (which
is -4.595 for ``alpha_init=1e-2``); mirrored rather than corrected so block
occupancy here matches the bitfield ReRF would actually send."""


@dataclass(frozen=True)
class BlockGrid:
    """Maps grid entries and world points onto ReRF block indices."""

    grid_shape: Tuple[int, int, int]
    block_size: int = RERF_BLOCK_SIZE

    def __post_init__(self) -> None:
        if self.block_size < 1:
            raise ValueError("block_size must be >= 1")
        if any(int(n) < 1 for n in self.grid_shape):
            raise ValueError(f"degenerate grid shape {self.grid_shape}")

    @classmethod
    def from_volume(cls, volume: torch.Tensor, block_size: int = RERF_BLOCK_SIZE) -> "BlockGrid":
        """``volume`` is ``[1, C, X, Y, Z]`` as ReRF stores density and k0."""
        if volume.dim() != 5:
            raise ValueError(f"expected a [1, C, X, Y, Z] volume, got {tuple(volume.shape)}")
        return cls(tuple(int(n) for n in volume.shape[2:]), block_size)

    @property
    def padded_shape(self) -> Tuple[int, int, int]:
        block = self.block_size
        return tuple(-(-int(n) // block) * block for n in self.grid_shape)

    @property
    def blocks_shape(self) -> Tuple[int, int, int]:
        block = self.block_size
        return tuple(n // block for n in self.padded_shape)

    @property
    def num_blocks(self) -> int:
        x, y, z = self.blocks_shape
        return x * y * z

    def block_index(self, ijk: torch.Tensor) -> torch.Tensor:
        """Flat block index for integer entry coordinates ``ijk`` of shape [N, 3].

        The stride order matches ``codec.utils.split_volume``, which appends
        blocks in ``for x: for y: for z:`` order.
        """
        if ijk.dim() != 2 or ijk.shape[1] != 3:
            raise ValueError(f"expected [N, 3] entry coordinates, got {tuple(ijk.shape)}")
        _, by, bz = self.blocks_shape
        block = self.block_size
        coords = ijk // block
        return (coords[:, 0] * by + coords[:, 1]) * bz + coords[:, 2]

    def occupancy(self, density: torch.Tensor) -> torch.Tensor:
        """Per-block occupancy, mirroring ``codec.compress_utils.get_masks``.

        ``density`` is the raw (pre-activation) density volume ``[1, 1, X, Y, Z]``.
        Returns a bool tensor of length :attr:`num_blocks`.
        """
        if density.dim() != 5 or density.shape[:2] != (1, 1):
            raise ValueError(f"expected a [1, 1, X, Y, Z] density volume, got {tuple(density.shape)}")
        occupied = torch.nn.functional.softplus(
            density.detach() - _DENSITY_SHIFT
        ) > _OCCUPANCY_THRESHOLD
        return self.reduce_any(occupied[0, 0])

    def reduce_any(self, entry_mask: torch.Tensor) -> torch.Tensor:
        """Reduce an ``[X, Y, Z]`` bool grid to one bool per block."""
        if tuple(entry_mask.shape) != self.grid_shape:
            raise ValueError(
                f"mask shape {tuple(entry_mask.shape)} does not match grid {self.grid_shape}"
            )
        block = self.block_size
        padded = torch.zeros(self.padded_shape, dtype=torch.bool, device=entry_mask.device)
        x, y, z = self.grid_shape
        padded[:x, :y, :z] = entry_mask
        bx, by, bz = self.blocks_shape
        return (
            padded.reshape(bx, block, by, block, bz, block)
            .permute(0, 2, 4, 1, 3, 5)
            .reshape(bx, by, bz, -1)
            .any(dim=-1)
            .reshape(-1)
        )


def nearest_entry(
    points: torch.Tensor,
    xyz_min: torch.Tensor,
    xyz_max: torch.Tensor,
    grid_shape: Sequence[int],
) -> torch.Tensor:
    """Nearest grid entry to each world point, as integer ``[N, 3]``.

    ReRF samples its grids with ``align_corners=True``, so entry ``(i, j, k)``
    sits exactly at ``xyz_min + (i, j, k) / (shape - 1) * (xyz_max - xyz_min)``
    -- the entries are the *corners* of the sampled domain, not cell centres.
    """
    size = torch.tensor(
        [float(n) for n in grid_shape], device=points.device, dtype=points.dtype
    )
    normalised = (points - xyz_min) / (xyz_max - xyz_min)
    index = torch.round(normalised * (size - 1.0))
    return index.clamp_(torch.zeros_like(size), size - 1.0).long()


def surrounding_entries(
    points: torch.Tensor,
    xyz_min: torch.Tensor,
    xyz_max: torch.Tensor,
    grid_shape: Sequence[int],
) -> torch.Tensor:
    """The eight entries a trilinear sample at each point actually reads.

    Returns ``[8, N, 3]``. Use this when "which feature voxels does rendering
    this point depend on" is the question being asked, rather than the paper's
    "sampled points inside a voxel" -- at a block boundary the two differ.
    """
    size = torch.tensor(
        [float(n) for n in grid_shape], device=points.device, dtype=points.dtype
    )
    normalised = (points - xyz_min) / (xyz_max - xyz_min) * (size - 1.0)
    low = torch.floor(normalised)
    corners = []
    for offset in range(8):
        delta = torch.tensor(
            [float((offset >> shift) & 1) for shift in range(3)],
            device=points.device,
            dtype=points.dtype,
        )
        corners.append((low + delta).clamp(torch.zeros_like(size), size - 1.0).long())
    return torch.stack(corners)
