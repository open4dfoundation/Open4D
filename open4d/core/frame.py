"""Temporal frame values."""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from numbers import Real
from types import MappingProxyType
from typing import Any, Mapping

from .geometry import TriangleMesh


@dataclass(frozen=True, eq=False)
class Frame:
    """A geometry sample identified by a nonnegative index and timestamp."""

    frame_index: int
    timestamp: float
    geometry: TriangleMesh
    metadata: Mapping[str, Any] = field(default_factory=dict)

    def __post_init__(self) -> None:
        if not isinstance(self.frame_index, int) or isinstance(
            self.frame_index, bool
        ):
            raise TypeError("frame_index must be an integer")
        if self.frame_index < 0:
            raise ValueError("frame_index must be nonnegative")
        if not isinstance(self.timestamp, Real) or isinstance(self.timestamp, bool):
            raise TypeError("timestamp must be a real number")
        timestamp = float(self.timestamp)
        if not math.isfinite(timestamp):
            raise ValueError("timestamp must be finite")
        if not isinstance(self.geometry, TriangleMesh):
            raise TypeError("geometry must be a TriangleMesh")
        if not isinstance(self.metadata, Mapping):
            raise TypeError("metadata must be a mapping")

        object.__setattr__(self, "timestamp", timestamp)
        object.__setattr__(self, "metadata", MappingProxyType(dict(self.metadata)))
