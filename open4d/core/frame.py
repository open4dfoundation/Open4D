"""Temporal frame values."""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from numbers import Integral, Real
from types import MappingProxyType
from typing import Any, Mapping

from .geometry import Geometry, Representation


@dataclass(frozen=True, eq=False)
class Frame:
    """A geometry sample identified by a nonnegative index and timestamp."""

    frame_index: int
    timestamp: float
    geometry: Geometry
    metadata: Mapping[str, Any] = field(default_factory=dict)

    def __post_init__(self) -> None:
        if not isinstance(self.frame_index, Integral) or isinstance(
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
        # Any representation, not just triangles: see `open4d.core.geometry`.
        # The check is against the protocol rather than a fixed tuple of types so
        # that a new representation -- in this package or a third party's -- needs
        # no edit here.
        if not isinstance(self.geometry, Geometry):
            raise TypeError(
                "geometry must implement open4d.core.Geometry (a "
                "`representation` property); got "
                f"{type(self.geometry).__name__}"
            )
        if not isinstance(self.geometry.representation, Representation):
            raise TypeError("geometry.representation must be a Representation")
        if not isinstance(self.metadata, Mapping):
            raise TypeError("metadata must be a mapping")

        object.__setattr__(self, "frame_index", int(self.frame_index))
        object.__setattr__(self, "timestamp", timestamp)
        object.__setattr__(self, "metadata", MappingProxyType(dict(self.metadata)))
