"""Frame provider contracts and in-memory implementation."""

from __future__ import annotations

from collections.abc import Sequence as CollectionSequence
from enum import Enum
from types import MappingProxyType
from typing import Any, Mapping, Protocol, runtime_checkable

from .frame import Frame


class TopologyMode(str, Enum):
    """How triangle connectivity varies over a sequence."""

    FIXED = "fixed"
    CHANGING = "changing"
    UNKNOWN = "unknown"


@runtime_checkable
class FrameProvider(Protocol):
    """Minimum random-access contract used by :class:`Sequence`.

    Providers may additionally expose ``metadata``, ``timestamps``,
    ``topology``, ``has_constant_vertex_count``,
    ``has_vertex_correspondence``, and ``close``. Sequence consumes these
    declarations when present without requiring them from every provider.
    """

    @property
    def frame_count(self) -> int:
        """Number of addressable frames."""

    def get_frame(self, index: int) -> Frame:
        """Return the frame at a nonnegative ordinal position."""


class MemoryFrameProvider:
    """Expose an existing in-memory frame sequence through FrameProvider."""

    def __init__(
        self,
        frames: CollectionSequence[Frame],
        *,
        metadata: Mapping[str, Any] | None = None,
        topology: TopologyMode = TopologyMode.UNKNOWN,
        has_constant_vertex_count: bool | None = None,
        has_vertex_correspondence: bool | None = None,
    ) -> None:
        if not isinstance(frames, CollectionSequence):
            raise TypeError("frames must be a sequence")
        stored_frames = tuple(frames)
        if any(not isinstance(frame, Frame) for frame in stored_frames):
            raise TypeError("frames must contain only Frame instances")
        if not isinstance(topology, TopologyMode):
            raise TypeError("topology must be a TopologyMode")
        if metadata is not None and not isinstance(metadata, Mapping):
            raise TypeError("metadata must be a mapping")
        for name, value in (
            ("has_constant_vertex_count", has_constant_vertex_count),
            ("has_vertex_correspondence", has_vertex_correspondence),
        ):
            if value is not None and not isinstance(value, bool):
                raise TypeError(f"{name} must be bool or None")

        self._frames = stored_frames
        self.metadata = MappingProxyType(dict(metadata or {}))
        self.topology = topology
        self.has_constant_vertex_count = has_constant_vertex_count
        self.has_vertex_correspondence = has_vertex_correspondence

    @property
    def frame_count(self) -> int:
        return len(self._frames)

    @property
    def timestamps(self) -> tuple[float, ...]:
        return tuple(frame.timestamp for frame in self._frames)

    def get_frame(self, index: int) -> Frame:
        if index < 0 or index >= self.frame_count:
            raise IndexError("frame index out of range")
        return self._frames[index]
