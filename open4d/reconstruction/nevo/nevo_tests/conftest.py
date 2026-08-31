"""Shared gating for the tests that need a real ReRF model.

Most of this suite is pure tensor/geometry logic and always runs. A handful of
cases load a trained sequence and render it, which needs both a checkpoint on
disk and a GPU with room to work in.

"A GPU is present" is not the right condition. This box trains ReRF on both
cards for hours at a time, and a test that starts while 20 GB of the 24 is
already committed does not fail meaningfully -- it raises ``CUDA error: out of
memory`` somewhere inside DVGO's kernels and reports a red suite that says
nothing about the code. So the gate is *free* memory, and falling short of it
skips.
"""
from __future__ import annotations

import os
from pathlib import Path

import pytest

REQUIRED_FREE_BYTES = 3 << 30
"""Headroom a trained frame needs: its density and feature grids, the
occupancy pass, and a viewport's worth of ray samples."""

DEFAULT_CONFIG = Path(__file__).resolve().parents[1] / "rerf" / "configs" / "nevo"


def _first_trained_config() -> Path | None:
    """The config named by ``NEVO_TEST_CONFIG``, else any run with a frame."""
    override = os.environ.get("NEVO_TEST_CONFIG")
    if override:
        candidate = Path(override)
        return candidate if candidate.is_file() else None
    if not DEFAULT_CONFIG.is_dir():
        return None
    for candidate in sorted(DEFAULT_CONFIG.glob("*.py")):
        return candidate
    return None


def gpu_headroom() -> int:
    try:
        import torch
    except ImportError:
        return 0
    if not torch.cuda.is_available():
        return 0
    try:
        free, _total = torch.cuda.mem_get_info()
    except Exception:
        return 0
    return int(free)


def _decide() -> "tuple[Path | None, str]":
    free = gpu_headroom()
    if free < REQUIRED_FREE_BYTES:
        return None, (
            f"needs {REQUIRED_FREE_BYTES >> 30} GB of free GPU memory, "
            f"{free / (1 << 30):.1f} GB available (training probably has the cards)"
        )
    config = _first_trained_config()
    if config is None:
        return None, "needs a trained ReRF sequence; point NEVO_TEST_CONFIG at one"
    return config, ""


# Decided once, at collection. Deciding per call would let the skip marker and
# the test body disagree -- the marker is evaluated at import, and free GPU
# memory moves while a suite runs, so a re-check inside the test can come back
# None on a test the marker already let through.
_CONFIG, _REASON = _decide()


def trained_config() -> "Path | None":
    """The config the model tests should use, or None if they must skip."""
    return _CONFIG


needs_sequence = pytest.mark.skipif(_CONFIG is None, reason=_REASON)
