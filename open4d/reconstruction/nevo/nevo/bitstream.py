"""Real ReRF bytes on the wire, for a chosen subset of feature voxels.

The question this answers is "how many bytes does frame *t* cost if we only
send the blocks whose importance clears a threshold", and it answers it by
running ReRF's own encoder rather than by estimating.

Why not attribute the joint bitstream's bytes to individual blocks: measured on
this codec, encoding one 13x8x8x8 block on its own costs ~9 kB almost
regardless of content, because each of the 13 channels gets its own file and
header. A 400-block frame costs 824 kB jointly and 3.96 MB block-by-block --
4.8x -- and rescaling those per-block numbers mispredicts a half-subset's real
cost by 30%. Per-block attribution is not recoverable from this encoder, so
instead the retained subset is encoded *as a set*, which is exactly what ReRF
would transmit.

What a frame costs, mirroring ``codec/compress.py``:

``feature_<frame>_<quality>.rerf*``
    The DCT + entropy-coded payload of the retained blocks, one file per
    channel. This is the part a threshold changes.
``mask_<frame>.rerf``
    A packed bitfield, one bit per block of the padded grid, telling the
    decoder which blocks are present. Its size does not depend on the
    threshold -- ``ceil(blocks / 8)`` bytes either way -- but it is on the wire
    and is counted.
``deform_<frame>.npy`` + ``deform_mask_<frame>.rerf``
    P-frames only: the motion vectors, fp16 with the all-zero entries dropped,
    plus their own bitfield.

Not counted per frame, and reported separately: ``rgb_net.tar`` (the colour MLP
shared by the whole sequence) and ``model_kwargs.json``. Those are a one-time
startup payload, not a per-frame cost.

The residual chain is advanced **unfiltered**. A P-frame codes a residual
against the previous frame's reconstruction, so filtering frame *t-1* would
change what frame *t* has to encode; letting that compound would mean every
threshold measured a different bitstream. Holding the chain fixed measures one
well-defined thing -- the delivery cost of a subset of ReRF's own stream --
which is the quantity that compares like-for-like against another codec's
output.
"""
from __future__ import annotations

import glob
import json
import os
import shutil
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

import numpy as np

from . import rerf_env
from .blocks import RERF_BLOCK_SIZE

DENSITY_ACT = -4.1
"""``density_act`` in codec/compress.py: the raw density an absent voxel decodes
to, and the offset an I-frame's density is coded relative to."""

DEFAULT_QUALITY = 99
"""compress.py's default ``--quality``."""


@dataclass(frozen=True)
class FrameBytes:
    """Per-frame wire cost at one threshold."""

    frame: int
    key_frame: bool
    kept_blocks: int
    occupied_blocks: int
    total_blocks: int
    feature_bytes: int
    mask_bytes: int
    motion_bytes: int

    @property
    def total_bytes(self) -> int:
        return self.feature_bytes + self.mask_bytes + self.motion_bytes

    @property
    def kept_fraction(self) -> float:
        return self.kept_blocks / self.occupied_blocks if self.occupied_blocks else 0.0

    def as_dict(self) -> dict:
        payload = {
            "frame": self.frame,
            "key_frame": self.key_frame,
            "kept_blocks": self.kept_blocks,
            "occupied_blocks": self.occupied_blocks,
            "total_blocks": self.total_blocks,
            "feature_bytes": self.feature_bytes,
            "mask_bytes": self.mask_bytes,
            "motion_bytes": self.motion_bytes,
            "total_bytes": self.total_bytes,
            "kept_fraction": self.kept_fraction,
        }
        return payload


class SequenceCoder:
    """Walks a ReRF sequence, producing each frame's codable residual and cost.

    Use as an iterator over frames: each :meth:`advance` yields the state
    needed to price any number of thresholds for that frame, then moves the
    reconstruction chain on.
    """

    def __init__(self, sequence, quality: int = DEFAULT_QUALITY,
                 voxel_size: int = RERF_BLOCK_SIZE, workdir: Optional[str] = None):
        rerf_env.activate()
        with rerf_env.rerf_cwd():
            import torch
            from codec import (  # noqa: F401
                decode_motion,
                deform_warp,
                encode_entropy_motion_npy,
                encode_jpeg_huffman,
                encode_motion,
                get_masks,
                grid_sampler,
                quant_motion,
                split_volume,
                zero_pads,
            )

        self._torch = torch
        self._codec = {
            "encode_jpeg_huffman": encode_jpeg_huffman,
            "get_masks": get_masks,
            "split_volume": split_volume,
            "zero_pads": zero_pads,
            "encode_motion": encode_motion,
            "decode_motion": decode_motion,
            "quant_motion": quant_motion,
            "encode_entropy_motion_npy": encode_entropy_motion_npy,
            "deform_warp": deform_warp,
            "grid_sampler": grid_sampler,
        }
        self.sequence = sequence
        self.quality = quality
        self.voxel_size = voxel_size
        self._owns_workdir = workdir is None
        self._workdir = Path(workdir or tempfile.mkdtemp(prefix="nevo-bitstream-"))
        self._workdir.mkdir(parents=True, exist_ok=True)
        self._former = None          # previous frame's reconstruction, [1, 13, X, Y, Z]

    def close(self) -> None:
        if self._owns_workdir and self._workdir.exists():
            shutil.rmtree(self._workdir, ignore_errors=True)

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    # ------------------------------------------------------------------ bytes
    def _encoded_size(self, blocks, tag: str) -> int:
        """Bytes ReRF's entropy coder writes for these blocks, across channels."""
        target = self._workdir / tag
        for stale in glob.glob(str(target) + "*"):
            os.remove(stale)
        if blocks.shape[0] == 0:
            return 0
        self._codec["encode_jpeg_huffman"](blocks, self.quality, str(target))
        return sum(os.path.getsize(path) for path in glob.glob(str(target) + "*"))

    @staticmethod
    def _bitfield_bytes(count: int) -> int:
        """``bitarray.pack`` + ``tofile`` rounds up to whole bytes."""
        return (count + 7) // 8

    def _motion_bytes(self, frame) -> int:
        if frame.motion is None:
            return 0
        cube, _grid_size, _origin = self._codec["encode_motion"](frame.motion)
        payload, mask = self._codec["encode_entropy_motion_npy"](cube)
        path = self._workdir / "deform.npy"
        np.save(str(path), payload)
        size = os.path.getsize(path)
        os.remove(path)
        return size + self._bitfield_bytes(int(mask.shape[0]))

    # ------------------------------------------------------------------ chain
    def _residual_volume(self, frame):
        """``residual_full`` and the occupancy mask, as compress.py computes them."""
        torch = self._torch
        density = frame.density
        features = frame.features
        if self._former is None:
            residual = torch.cat([density - DENSITY_ACT, features], dim=1)
            occupancy_source = [torch.zeros_like(density) + DENSITY_ACT, density]
            return residual, occupancy_source

        # Motion-compensate the previous reconstruction, then code the delta.
        motion = self._codec["quant_motion"](frame.motion) if frame.motion is not None else None
        xyz_min, xyz_max = frame.xyz_min, frame.xyz_max
        former = self._former
        if motion is not None:
            grid_xyz = torch.stack(
                torch.meshgrid(
                    torch.linspace(xyz_min[0], xyz_max[0], motion.shape[2]),
                    torch.linspace(xyz_min[1], xyz_max[1], motion.shape[3]),
                    torch.linspace(xyz_min[2], xyz_max[2], motion.shape[4]),
                ),
                -1,
            )
            warped = self._codec["deform_warp"](grid_xyz, motion, xyz_min, xyz_max)
            former = self._codec["grid_sampler"](warped, xyz_min, xyz_max, former)
            former = former.permute(3, 0, 1, 2).unsqueeze(0)
        residual = torch.cat([density - former[:, :1], features - former[:, 1:]], dim=1)
        return residual, [former[:, :1], density]

    def advance(self, frame):
        """Price ``frame`` and move the chain on. Returns a per-frame pricer."""
        torch = self._torch
        with torch.no_grad():
            residual, occupancy_source = self._residual_volume(frame)
            masks = self._codec["get_masks"](
                torch.cat(occupancy_source, dim=1), self.voxel_size
            )
            blocks, _grid = self._codec["split_volume"](
                self._codec["zero_pads"](residual, voxel_size=self.voxel_size),
                voxel_size=self.voxel_size,
            )
            blocks = blocks.cuda()
            motion_bytes = self._motion_bytes(frame)
            mask_bytes = self._bitfield_bytes(int(masks.shape[0]))
            # The chain advances on the *unfiltered* frame; see the module
            # docstring for why thresholds must not compound.
            self._former = torch.cat([frame.density, frame.features], dim=1)

        occupied = int(masks.sum().item())
        total = int(masks.shape[0])

        def price(keep) -> FrameBytes:
            selected = keep & masks if keep is not None else masks
            kept = int(selected.sum().item())
            feature_bytes = self._encoded_size(blocks[selected], "feature")
            return FrameBytes(
                frame=frame.index,
                key_frame=frame.is_key_frame,
                kept_blocks=kept,
                occupied_blocks=occupied,
                total_blocks=total,
                feature_bytes=feature_bytes,
                mask_bytes=mask_bytes,
                motion_bytes=motion_bytes,
            )

        return price


def startup_bytes(sequence) -> dict:
    """The one-time payload: the shared colour MLP and the model header.

    Reported separately from per-frame cost because it is sent once for the
    whole sequence. Any comparison against another representation has to agree
    on whether this is amortised in or not.
    """
    run_dir = Path(sequence.run_dir)
    mlp = run_dir / "rgb_net.tar"
    header = {
        "rgb_net_bytes": os.path.getsize(mlp) if mlp.is_file() else 0,
        "model_kwargs_bytes": 0,
    }
    kwargs = run_dir / "model_kwargs.json"
    if kwargs.is_file():
        header["model_kwargs_bytes"] = os.path.getsize(kwargs)
    header["total_bytes"] = header["rgb_net_bytes"] + header["model_kwargs_bytes"]
    return header


def write_json(path, payload) -> None:
    with open(path, "w") as handle:
        json.dump(payload, handle, indent=1)
