"""Lazy temporal geometry sequences and views."""

from __future__ import annotations

import math
import operator
from collections.abc import Iterator
from types import MappingProxyType
from typing import Any, Mapping, overload

from .frame import Frame
from .provider import FrameProvider, TopologyMode


class Sequence:
    """A lazy, random-access temporal geometry sequence."""

    def __init__(self, provider: FrameProvider) -> None:
        if not isinstance(provider, FrameProvider):
            raise TypeError("provider must implement FrameProvider")
        count = provider.frame_count
        if not isinstance(count, int) or isinstance(count, bool) or count < 0:
            raise ValueError("provider.frame_count must be a nonnegative integer")
        self._provider = provider
        self._frame_count = count
        self._timestamps_cache: tuple[float, ...] | None = None

        metadata = getattr(provider, "metadata", {})
        if not isinstance(metadata, Mapping):
            raise TypeError("provider metadata must be a mapping")
        self._metadata = MappingProxyType(dict(metadata))

        topology = getattr(provider, "topology", TopologyMode.UNKNOWN)
        if not isinstance(topology, TopologyMode):
            raise TypeError("provider topology must be a TopologyMode")
        self._topology = topology

    def __len__(self) -> int:
        return self._frame_count

    @property
    def frame_count(self) -> int:
        return len(self)

    @overload
    def __getitem__(self, index: int) -> Frame: ...

    @overload
    def __getitem__(self, index: slice) -> "SequenceView": ...

    def __getitem__(self, index: int | slice) -> Frame | "SequenceView":
        if isinstance(index, slice):
            return SequenceView(self, range(len(self))[index])
        try:
            ordinal = operator.index(index)
        except TypeError as exc:
            raise TypeError("sequence indices must be integers or slices") from exc
        if ordinal < 0:
            ordinal += len(self)
        if ordinal < 0 or ordinal >= len(self):
            raise IndexError("sequence index out of range")
        frame = self._provider.get_frame(ordinal)
        if not isinstance(frame, Frame):
            raise TypeError("provider.get_frame() must return a Frame")
        return frame

    def frame(self, index: int) -> Frame:
        """Return a frame by ordinal position."""
        return self[index]

    def __iter__(self) -> Iterator[Frame]:
        for index in range(len(self)):
            yield self[index]

    @property
    def metadata(self) -> Mapping[str, Any]:
        return self._metadata

    @property
    def timestamps(self) -> tuple[float, ...]:
        """Return ordered timestamps, decoding frames only if required."""
        if self._timestamps_cache is None:
            provided = getattr(self._provider, "timestamps", None)
            values = provided if provided is not None else (
                self[index].timestamp for index in range(len(self))
            )
            timestamps = tuple(float(value) for value in values)
            if len(timestamps) != len(self):
                raise ValueError("provider timestamps length does not match frame_count")
            if any(not math.isfinite(value) for value in timestamps):
                raise ValueError("provider timestamps must be finite")
            if any(a > b for a, b in zip(timestamps, timestamps[1:])):
                raise ValueError("sequence timestamps must be nondecreasing")
            self._timestamps_cache = timestamps
        return self._timestamps_cache

    @property
    def duration(self) -> float:
        timestamps = self.timestamps
        return timestamps[-1] - timestamps[0] if len(timestamps) > 1 else 0.0

    @property
    def fps(self) -> float | None:
        duration = self.duration
        return (len(self) - 1) / duration if len(self) > 1 and duration > 0 else None

    @property
    def topology(self) -> TopologyMode:
        return self._topology

    @property
    def has_constant_topology(self) -> bool | None:
        if self.topology is TopologyMode.FIXED:
            return True
        if self.topology is TopologyMode.CHANGING:
            return False
        return None

    def _optional_provider_flag(self, name: str) -> bool | None:
        value = getattr(self._provider, name, None)
        if value is not None and not isinstance(value, bool):
            raise TypeError(f"provider {name} must be bool or None")
        if value is None and self.topology is TopologyMode.FIXED:
            return True
        return value

    @property
    def has_constant_vertex_count(self) -> bool | None:
        return self._optional_provider_flag("has_constant_vertex_count")

    @property
    def has_vertex_correspondence(self) -> bool | None:
        return self._optional_provider_flag("has_vertex_correspondence")

    def close(self) -> None:
        """Close provider resources when the provider supports it."""
        close = getattr(self._provider, "close", None)
        if close is not None:
            close()

    def __enter__(self) -> "Sequence":
        return self

    def __exit__(self, exc_type: object, exc: object, traceback: object) -> None:
        self.close()


class _ViewProvider:
    def __init__(self, parent: Sequence, indices: range) -> None:
        self.parent = parent
        self.indices = indices
        self.metadata = parent.metadata
        self.topology = parent.topology
        self.has_constant_vertex_count = parent.has_constant_vertex_count
        self.has_vertex_correspondence = parent.has_vertex_correspondence

    @property
    def frame_count(self) -> int:
        return len(self.indices)

    @property
    def timestamps(self) -> tuple[float, ...]:
        return tuple(self.parent[index].timestamp for index in self.indices)

    def get_frame(self, index: int) -> Frame:
        return self.parent[self.indices[index]]


class SequenceView(Sequence):
    """A lightweight ordinal view into another sequence."""

    def __init__(self, parent: Sequence, indices: range) -> None:
        self.parent = parent
        self.indices = indices
        super().__init__(_ViewProvider(parent, indices))
