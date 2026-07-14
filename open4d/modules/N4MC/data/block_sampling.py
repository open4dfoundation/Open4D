from __future__ import annotations

from typing import Iterable

import torch


def _as_3tuple(value: int | Iterable[int]) -> tuple[int, int, int]:
    if isinstance(value, int):
        return (value, value, value)
    values = tuple(int(v) for v in value)
    if len(values) != 3:
        raise ValueError(f"Expected 3 values, received {values}")
    return values


def _axis_starts(length: int, block: int, stride: int) -> list[int]:
    if block <= 0 or stride <= 0:
        raise ValueError("Block size and stride must be positive.")
    if block > length:
        raise ValueError(f"Block size {block} exceeds axis length {length}.")

    starts = list(range(0, max(length - block + 1, 1), stride))
    last_start = length - block
    if starts[-1] != last_start:
        starts.append(last_start)
    return starts


def compute_block_origins(
    spatial_shape: Iterable[int],
    block_size: int | Iterable[int],
    stride: int | Iterable[int],
) -> list[tuple[int, int, int]]:
    depth, height, width = (int(v) for v in spatial_shape)
    block_d, block_h, block_w = _as_3tuple(block_size)
    stride_d, stride_h, stride_w = _as_3tuple(stride)

    origins = []
    for d in _axis_starts(depth, block_d, stride_d):
        for h in _axis_starts(height, block_h, stride_h):
            for w in _axis_starts(width, block_w, stride_w):
                origins.append((d, h, w))
    return origins


def extract_blocks(
    volume: torch.Tensor,
    block_size: int | Iterable[int],
    stride: int | Iterable[int],
) -> tuple[torch.Tensor, list[tuple[int, int, int]]]:
    if volume.ndim != 4:
        raise ValueError(f"Expected (C, D, H, W), received {tuple(volume.shape)}")

    origins = compute_block_origins(volume.shape[1:], block_size, stride)
    block_d, block_h, block_w = _as_3tuple(block_size)
    blocks = []
    for d, h, w in origins:
        blocks.append(volume[:, d : d + block_d, h : h + block_h, w : w + block_w])
    return torch.stack(blocks, dim=0), origins


def stitch_blocks(
    blocks: torch.Tensor,
    origins: list[tuple[int, int, int]],
    volume_shape: Iterable[int],
) -> torch.Tensor:
    channels, depth, height, width = (int(v) for v in volume_shape)
    if blocks.ndim != 5:
        raise ValueError(f"Expected (N, C, D, H, W), received {tuple(blocks.shape)}")
    if blocks.shape[1] != channels:
        raise ValueError("Block channels do not match requested output shape.")
    if len(origins) != blocks.shape[0]:
        raise ValueError("Number of origins must match number of blocks.")

    output = torch.zeros((channels, depth, height, width), dtype=blocks.dtype)
    counts = torch.zeros((1, depth, height, width), dtype=blocks.dtype)
    block_d, block_h, block_w = blocks.shape[-3:]

    for block, (d, h, w) in zip(blocks, origins):
        output[:, d : d + block_d, h : h + block_h, w : w + block_w] += block
        counts[:, d : d + block_d, h : h + block_h, w : w + block_w] += 1

    if torch.any(counts == 0):
        raise ValueError("Block stitching left uncovered voxels.")
    return output / counts


def _sample_surface_center(
    volume: torch.Tensor,
    block_size: tuple[int, int, int],
    threshold: float,
    generator: torch.Generator | None,
) -> tuple[int, int, int]:
    narrow_band = torch.nonzero(volume[0].abs() <= threshold, as_tuple=False)
    if narrow_band.numel() == 0:
        return _sample_uniform_origin(volume.shape[1:], block_size, generator)

    index = torch.randint(
        low=0,
        high=narrow_band.shape[0],
        size=(1,),
        generator=generator,
    ).item()
    center = narrow_band[index]

    starts = []
    for coord, length, block in zip(center.tolist(), volume.shape[1:], block_size):
        start = int(coord - block // 2)
        start = max(start, 0)
        start = min(start, length - block)
        starts.append(start)
    return tuple(starts)


def _sample_uniform_origin(
    spatial_shape: Iterable[int],
    block_size: tuple[int, int, int],
    generator: torch.Generator | None,
) -> tuple[int, int, int]:
    starts = []
    for length, block in zip(spatial_shape, block_size):
        max_start = int(length) - int(block)
        if max_start < 0:
            raise ValueError(f"Block size {block} exceeds spatial length {length}.")
        if max_start == 0:
            starts.append(0)
            continue
        start = torch.randint(0, max_start + 1, (1,), generator=generator).item()
        starts.append(int(start))
    return tuple(starts)


def sample_random_blocks(
    volumes: torch.Tensor,
    block_size: int | Iterable[int],
    blocks_per_volume: int,
    narrow_band_threshold: float,
    prefer_narrow_band: bool = True,
    generator: torch.Generator | None = None,
) -> tuple[torch.Tensor, list[tuple[int, int, int, int]]]:
    if volumes.ndim != 5:
        raise ValueError(f"Expected (B, C, D, H, W), received {tuple(volumes.shape)}")

    block = _as_3tuple(block_size)
    sampled_blocks = []
    sampled_origins = []
    for batch_index, volume in enumerate(volumes):
        for _ in range(blocks_per_volume):
            use_surface = prefer_narrow_band and torch.rand(1, generator=generator).item() < 0.7
            if use_surface:
                origin = _sample_surface_center(volume, block, narrow_band_threshold, generator)
            else:
                origin = _sample_uniform_origin(volume.shape[1:], block, generator)
            d, h, w = origin
            sampled_blocks.append(volume[:, d : d + block[0], h : h + block[1], w : w + block[2]])
            sampled_origins.append((batch_index, d, h, w))
    return torch.stack(sampled_blocks, dim=0), sampled_origins
