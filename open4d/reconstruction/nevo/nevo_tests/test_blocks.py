"""Block indexing has to agree with ReRF's, bit for bit.

A block index in this codebase is meant to be the same integer as the bit
position in ReRF's ``mask_<frame>.rerf`` and the same slot in its bitstream.
Nothing enforces that at runtime, so it is pinned here against a transcription
of upstream's ``codec.utils.split_volume`` and ``compress_utils.get_masks``.
"""
from __future__ import annotations

import numpy as np
import pytest

torch = pytest.importorskip("torch")

from nevo.blocks import (  # noqa: E402
    BlockGrid,
    nearest_entry,
    surrounding_entries,
)


def reference_split(data: torch.Tensor, voxel_size: int):
    """``codec.utils.zero_pads`` + ``split_volume``, transcribed.

    Deliberately the slow triple loop upstream uses, so the ordering under
    test is compared against the ordering that actually ships rather than
    against another vectorised guess at it.
    """
    if data.size(0) == 1:
        data = data.squeeze(0)
    size = list(data.size())
    padded_size = list(size)
    for axis in range(1, 4):
        if padded_size[axis] % voxel_size:
            padded_size[axis] = (padded_size[axis] // voxel_size + 1) * voxel_size
    padded = torch.zeros(padded_size, dtype=data.dtype)
    padded[:, : size[1], : size[2], : size[3]] = data
    blocks = []
    for x in range(padded_size[1] // voxel_size):
        for y in range(padded_size[2] // voxel_size):
            for z in range(padded_size[3] // voxel_size):
                blocks.append(
                    padded[
                        :,
                        x * voxel_size : (x + 1) * voxel_size,
                        y * voxel_size : (y + 1) * voxel_size,
                        z * voxel_size : (z + 1) * voxel_size,
                    ]
                )
    return torch.stack(blocks)


@pytest.mark.parametrize("shape", [(9, 17, 5), (16, 16, 16), (126, 26, 13)])
@pytest.mark.parametrize("block_size", [1, 2, 8])
def test_block_index_matches_split_volume_ordering(shape, block_size):
    """Give every entry a unique value, then find where each one landed."""
    volume = torch.arange(int(np.prod(shape)), dtype=torch.float64).reshape(1, 1, *shape)
    blocks = reference_split(volume, block_size)

    grid = BlockGrid(shape, block_size)
    assert grid.num_blocks == blocks.shape[0]

    entries = torch.stack(
        torch.meshgrid(
            *[torch.arange(n) for n in shape],
            indexing="ij",
        ),
        dim=-1,
    ).reshape(-1, 3)
    predicted = grid.block_index(entries)

    for entry, block in zip(entries.tolist(), predicted.tolist()):
        value = float(volume[0, 0, entry[0], entry[1], entry[2]])
        assert value in set(blocks[block].reshape(-1).tolist()), (entry, block)


@pytest.mark.parametrize("shape,block_size", [((9, 17, 5), 8), ((32, 32, 32), 8), ((7, 7, 7), 4)])
def test_occupancy_matches_the_codec(shape, block_size):
    torch.manual_seed(0)
    density = torch.randn(1, 1, *shape) * 4.0
    grid = BlockGrid(shape, block_size)
    ours = grid.occupancy(density).cpu().numpy()

    # codec.compress_utils.get_masks, on the single-channel case.
    blocks = reference_split(density, block_size)
    occupied = torch.nn.functional.softplus(blocks - 4.1) > 0.4
    theirs = occupied.reshape(blocks.shape[0], -1).any(dim=-1).numpy()

    assert np.array_equal(ours, theirs)


def test_occupancy_ignores_padding():
    """Padding is zeros; softplus(0 - 4.1) is far below the threshold, so a
    partly-padded block must be occupied only if a real entry says so."""
    shape = (9, 9, 9)
    density = torch.full((1, 1, *shape), -10.0)
    density[0, 0, 8, 8, 8] = 100.0
    grid = BlockGrid(shape, 8)
    occupancy = grid.occupancy(density)
    assert int(occupancy.sum()) == 1
    corner = grid.block_index(torch.tensor([[8, 8, 8]]))
    assert bool(occupancy[corner.item()])


def test_padded_and_block_shapes():
    grid = BlockGrid((126, 256, 125), 8)
    assert grid.padded_shape == (128, 256, 128)
    assert grid.blocks_shape == (16, 32, 16)
    assert grid.num_blocks == 16 * 32 * 16


def test_nearest_entry_hits_the_grid_corners():
    """align_corners=True: the first and last entries sit exactly on the bbox."""
    shape = (5, 9, 3)
    xyz_min = torch.tensor([-1.0, -2.0, 0.0])
    xyz_max = torch.tensor([1.0, 2.0, 4.0])
    points = torch.stack([xyz_min, xyz_max, (xyz_min + xyz_max) / 2])
    entries = nearest_entry(points, xyz_min, xyz_max, shape)
    assert entries[0].tolist() == [0, 0, 0]
    assert entries[1].tolist() == [4, 8, 2]
    assert entries[2].tolist() == [2, 4, 1]


def test_nearest_entry_clamps_points_outside_the_box():
    shape = (4, 4, 4)
    xyz_min = torch.zeros(3)
    xyz_max = torch.ones(3)
    points = torch.tensor([[-5.0, -5.0, -5.0], [5.0, 5.0, 5.0]])
    entries = nearest_entry(points, xyz_min, xyz_max, shape)
    assert entries[0].tolist() == [0, 0, 0]
    assert entries[1].tolist() == [3, 3, 3]


def test_surrounding_entries_bracket_the_sample():
    shape = (8, 8, 8)
    xyz_min = torch.zeros(3)
    xyz_max = torch.ones(3)
    point = torch.tensor([[0.31, 0.52, 0.77]])
    corners = surrounding_entries(point, xyz_min, xyz_max, shape).squeeze(1)
    assert corners.shape == (8, 3)
    exact = point[0] * 7.0
    assert torch.all(corners.min(dim=0).values.float() <= exact)
    assert torch.all(corners.max(dim=0).values.float() >= exact)
    assert len({tuple(row) for row in corners.tolist()}) == 8


def test_surrounding_entries_agree_with_nearest_at_a_grid_point():
    shape = (8, 8, 8)
    xyz_min = torch.zeros(3)
    xyz_max = torch.ones(3)
    point = torch.tensor([[3.0 / 7.0, 3.0 / 7.0, 3.0 / 7.0]])
    nearest = nearest_entry(point, xyz_min, xyz_max, shape)[0]
    corners = surrounding_entries(point, xyz_min, xyz_max, shape).squeeze(1)
    assert nearest.tolist() in corners.tolist()


def test_rejects_a_non_volume():
    with pytest.raises(ValueError):
        BlockGrid.from_volume(torch.zeros(4, 4, 4))
    with pytest.raises(ValueError):
        BlockGrid((8, 8, 8), 0)
