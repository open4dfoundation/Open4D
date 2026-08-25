"""Shared PyTorch device selection for optional neural codecs."""

from __future__ import annotations

from ._protocol import CodecError


def torch_device(torch, requested):
    """Resolve ``auto`` across CUDA, Apple Metal/MPS, and CPU."""
    if requested in (None, "auto"):
        if torch.cuda.is_available():
            requested = "cuda"
        elif (
            getattr(torch.backends, "mps", None) is not None
            and torch.backends.mps.is_available()
        ):
            requested = "mps"
        else:
            requested = "cpu"
    target = torch.device(requested)
    if target.type == "cuda" and not torch.cuda.is_available():
        raise CodecError("CUDA was requested but is unavailable in this PyTorch runtime")
    if target.type == "mps" and (
        getattr(torch.backends, "mps", None) is None
        or not torch.backends.mps.is_available()
    ):
        raise CodecError(
            "Apple Metal/MPS was requested but is unavailable in this PyTorch runtime"
        )
    return target
