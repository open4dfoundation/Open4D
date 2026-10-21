"""Optional encoder instrumentation used by codec quality and speed studies.

Research adapters accept ``stage_hook=callable(kind, ordinal, payload)``. It
never changes an artifact. ``ordinal`` is a frame ordinal or ``None`` for
sequence-wide work. Events are:

``("phase", ordinal, (name, seconds))``
    Wall-clock duration of one encoder phase such as ``"preprocessing"``,
    ``"training"``, ``"export"`` or ``"packing"``. CUDA and Metal work is
    synchronized before each clock reading.
``("mesh", ordinal, (stage, TriangleMesh))``
    An intermediate reconstruction in the source coordinate system. ``stage``
    is ``"target"`` for the preprocessing target the codec fits (for example a
    projected subdivision or a TSDF zero level set), ``"trained"`` for the model
    output before export, and ``"trained@<epoch>"`` for checkpoint exports.
``("rate", ordinal, mapping)``
    Codec-internal rate estimates, such as a learned entropy model's bits.

Mesh events are emitted outside timed phases, so slow hooks do not inflate
phase durations.
"""

from __future__ import annotations

from collections.abc import Mapping
from contextlib import contextmanager
from pathlib import Path
from time import perf_counter

import numpy as np

from open4d.core import TriangleMesh


def validate_stage_hook(hook) -> None:
    if hook is not None and not callable(hook):
        raise TypeError("stage_hook must be callable or None")


def synchronize(torch, device) -> None:
    """Wait for queued accelerator work so wall-clock phases are meaningful."""
    if torch is None or device is None:
        return
    kind = getattr(device, "type", str(device))
    if kind == "cuda" and torch.cuda.is_available():
        torch.cuda.synchronize(device)
    elif kind == "mps" and getattr(torch, "mps", None) is not None:
        torch.mps.synchronize()


class Stages:
    """Emit stage events when a hook is configured; otherwise do nothing."""

    def __init__(self, hook, torch=None, device=None) -> None:
        validate_stage_hook(hook)
        self.hook, self.torch, self.device = hook, torch, device

    @property
    def enabled(self) -> bool:
        return self.hook is not None

    @contextmanager
    def phase(self, name: str, ordinal: int | None = None):
        if self.hook is None:
            yield
            return
        synchronize(self.torch, self.device)
        started = perf_counter()
        try:
            yield
        finally:
            synchronize(self.torch, self.device)
            self.hook("phase", ordinal, (name, perf_counter() - started))

    def mesh(self, stage: str, ordinal: int, positions, triangles) -> None:
        if self.hook is not None:
            self.hook("mesh", ordinal, (stage, TriangleMesh(
                np.asarray(positions, dtype=np.float32),
                np.asarray(triangles, dtype=np.int64),
            )))

    def rate(self, ordinal: int | None, values: dict) -> None:
        if self.hook is not None:
            self.hook("rate", ordinal, dict(values))


def checkpoint_destinations(checkpoints, epochs, destination, overwrite) -> dict:
    """Validate extra artifacts exported from intermediate training epochs."""
    if checkpoints is None:
        return {}
    if not isinstance(checkpoints, Mapping):
        raise TypeError("checkpoint_artifacts must map epochs to .o4d destinations")
    result = {}
    for epoch, path in checkpoints.items():
        if type(epoch) is not int or not 1 <= epoch < epochs:
            raise ValueError("checkpoint_artifacts epochs must be integers from 1 to epochs - 1")
        path = Path(path).absolute()
        if path.suffix.lower() != ".o4d" or path == Path(destination).absolute() or path in result.values():
            raise ValueError("checkpoint artifacts need distinct .o4d destinations")
        if path.exists() and not overwrite:
            raise FileExistsError(f"artifact already exists: {path}")
        result[epoch] = path
    return dict(sorted(result.items()))
