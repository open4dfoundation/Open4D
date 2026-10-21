"""ReRF as a streamable compression method.

Upstream ReRF (CVPR 2023) lives unmodified under ``upstream/``; this package is
what a streaming platform needs from it and nothing else:

``env``
    Make upstream importable at all -- its entropy coder is a prebuilt Python
    3.8 binary with a stale RUNPATH, and one of its modules reads a relative
    path at import time.
``cameras``
    Viewpoints and near/far planes, read from the corpus so they match what the
    trainer used.
``bitstream``
    Decode the bitstream frame by frame and ray-march a view. This is the
    client path: what a receiver has, rather than what a trainer left behind.
``serve``
    Push those frames to a browser over MJPEG, because a neural field cannot be
    decoded in one.

Deliberately absent: the NeVo layer that used to live here (neural-visibility
scoring, voxel filtering, the importance CDF). It reimplemented a paper with no
released code, its results needed training checkpoints that no longer exist,
and none of it is required to stream ReRF. It is in git history at 3d33655.
"""
from __future__ import annotations

from . import bitstream, cameras, env
from .bitstream import BitstreamPlayer, DecodedFrame
from .cameras import Camera, captured_image, inward_near_far, psnr, training_cameras

__all__ = [
    "BitstreamPlayer",
    "Camera",
    "DecodedFrame",
    "bitstream",
    "cameras",
    "captured_image",
    "env",
    "inward_near_far",
    "psnr",
    "training_cameras",
]
