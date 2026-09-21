"""Lazy temporal geometry sequences and views."""

from __future__ import annotations

import math
import operator
from collections.abc import Iterator
from numbers import Integral, Real
from types import MappingProxyType
from typing import Any, Mapping, overload

from .frame import Frame
from .provider import Dependency, DependencyMode, FrameProvider, TopologyMode


class Sequence:
    """A lazy, random-access temporal geometry sequence."""

    def __init__(self, provider: FrameProvider) -> None:
        if not isinstance(provider, FrameProvider):
            raise TypeError("provider must implement FrameProvider")
        count = provider.frame_count
        if not isinstance(count, Integral) or isinstance(count, bool) or count < 0:
            raise ValueError("provider.frame_count must be a nonnegative integer")
        self._provider = provider
        self._frame_count = int(count)
        self._timestamps_cache: tuple[float, ...] | None = None
        self._closed = False

        metadata = getattr(provider, "metadata", {})
        if not isinstance(metadata, Mapping):
            raise TypeError("provider metadata must be a mapping")
        self._metadata = MappingProxyType(dict(metadata))

        topology = getattr(provider, "topology", TopologyMode.UNKNOWN)
        if not isinstance(topology, TopologyMode):
            raise TypeError("provider topology must be a TopologyMode")
        self._topology = topology

        dependency = getattr(provider, "dependency", None) or Dependency()
        if not isinstance(dependency, Dependency):
            raise TypeError("provider dependency must be a Dependency")
        self._dependency = dependency

    def __len__(self) -> int:
        return self._frame_count

    @property
    def closed(self) -> bool:
        """Whether this sequence has released its provider resources."""
        return self._closed

    def _ensure_open(self) -> None:
        if self._closed:
            raise RuntimeError("sequence is closed")

    @property
    def frame_count(self) -> int:
        return len(self)

    @overload
    def __getitem__(self, index: int) -> Frame: ...

    @overload
    def __getitem__(self, index: slice) -> "SequenceView": ...

    def __getitem__(self, index: int | slice) -> Frame | "SequenceView":
        self._ensure_open()
        if isinstance(index, slice):
            return SequenceView(self, range(len(self))[index])
        if isinstance(index, bool):
            raise TypeError("sequence indices must be integers or slices")
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
        self._ensure_open()
        for index in range(len(self)):
            yield self[index]

    @property
    def metadata(self) -> Mapping[str, Any]:
        return self._metadata

    @property
    def timestamps(self) -> tuple[float, ...]:
        """Return ordered timestamps, decoding frames only if required."""
        self._ensure_open()
        if self._timestamps_cache is None:
            provided = getattr(self._provider, "timestamps", None)
            values = provided if provided is not None else (
                self[index].timestamp for index in range(len(self))
            )
            normalized: list[float] = []
            for value in values:
                if not isinstance(value, Real) or isinstance(value, bool):
                    raise TypeError("provider timestamps must be real numbers")
                normalized.append(float(value))
            timestamps = tuple(normalized)
            if len(timestamps) != len(self):
                raise ValueError("provider timestamps length does not match frame_count")
            if any(not math.isfinite(value) for value in timestamps):
                raise ValueError("provider timestamps must be finite")
            allow_nonmonotonic = getattr(
                self._provider, "allow_nonmonotonic_timestamps", False
            )
            if not isinstance(allow_nonmonotonic, bool):
                raise TypeError("provider allow_nonmonotonic_timestamps must be bool")
            if not allow_nonmonotonic and any(
                a > b for a, b in zip(timestamps, timestamps[1:])
            ):
                raise ValueError("sequence timestamps must be nondecreasing")
            self._timestamps_cache = timestamps
        return self._timestamps_cache

    @property
    def duration(self) -> float:
        timestamps = self.timestamps
        return abs(timestamps[-1] - timestamps[0]) if len(timestamps) > 1 else 0.0

    @property
    def fps(self) -> float | None:
        duration = self.duration
        return (len(self) - 1) / duration if len(self) > 1 and duration > 0 else None

    @property
    def topology(self) -> TopologyMode:
        return self._topology

    @property
    def dependency(self) -> Dependency:
        """How frames in this sequence depend on one another.

        Advisory, not a restriction: :meth:`__getitem__` stays random-access, and
        a provider whose codec needs prior state is expected to replay
        internally to honour that. What this declares is the *cost and ordering*
        of doing so -- which is exactly what a consumer needs to prefetch
        sensibly, or to know that seeking backwards in a ReRF stream is a full
        replay rather than a step.
        """
        return self._dependency

    def decode_chain(
        self, index: int, *, decoded: int | None = None
    ) -> tuple[int, ...]:
        """Frames to decode, in order, to reach ``index``.

        Convenience for ``sequence.dependency.chain(...)``; see
        :meth:`open4d.core.Dependency.chain`.
        """
        self._ensure_open()
        if not 0 <= operator.index(index) < len(self):
            raise IndexError("frame index out of range")
        return self._dependency.chain(index, decoded=decoded)

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
        return value

    @property
    def has_constant_vertex_count(self) -> bool | None:
        return self._optional_provider_flag("has_constant_vertex_count")

    @property
    def has_vertex_correspondence(self) -> bool | None:
        return self._optional_provider_flag("has_vertex_correspondence")

    @property
    def allow_nonmonotonic_timestamps(self) -> bool:
        value = getattr(self._provider, "allow_nonmonotonic_timestamps", False)
        if not isinstance(value, bool):
            raise TypeError("provider allow_nonmonotonic_timestamps must be bool")
        return value

    def close(self) -> None:
        """Close provider resources when the provider supports it."""
        if self._closed:
            return
        close = getattr(self._provider, "close", None)
        if close is not None:
            if not callable(close):
                raise TypeError("provider close must be callable")
            close()
        self._closed = True

    def __enter__(self) -> "Sequence":
        self._ensure_open()
        return self

    def __exit__(self, exc_type: object, exc: object, traceback: object) -> None:
        self.close()


class _ViewProvider:
    def __init__(self, parent: Sequence, indices: range) -> None:
        self.parent = parent
        self.indices = indices
        self.metadata = parent.metadata
        fps = self.metadata.get("fps")
        if isinstance(fps, Real) and not isinstance(fps, bool) and math.isfinite(fps) and fps > 0:
            self.metadata = {**self.metadata, "fps": float(fps) / abs(indices.step)}
        self.topology = parent.topology
        # Key-frame ordinals are the parent's, and a slice may start mid-group or
        # skip frames, so they cannot be rebased onto this view in general.
        # Declaring SEQUENTIAL is pessimistic but never wrong: a consumer walks
        # the view in order, which is what it would do anyway.
        self.dependency = (
            Dependency()
            if parent.dependency.mode is DependencyMode.INDEPENDENT
            else Dependency(mode=DependencyMode.SEQUENTIAL)
        )
        self.has_constant_vertex_count = parent.has_constant_vertex_count
        self.has_vertex_correspondence = parent.has_vertex_correspondence
        self.allow_nonmonotonic_timestamps = True

    @property
    def frame_count(self) -> int:
        return len(self.indices)

    @property
    def timestamps(self) -> tuple[float, ...]:
        return tuple(self.parent.timestamps[index] for index in self.indices)

    def get_frame(self, index: int) -> Frame:
        return self.parent[self.indices[index]]


class SequenceView(Sequence):
    """A lightweight ordinal view into another sequence."""

    def __init__(self, parent: Sequence, indices: range) -> None:
        if not isinstance(parent, Sequence):
            raise TypeError("parent must be a Sequence")
        parent._ensure_open()
        if not isinstance(indices, range):
            raise TypeError("view indices must be a range")
        if indices and (min(indices[0], indices[-1]) < 0
                        or max(indices[0], indices[-1]) >= len(parent)):
            raise IndexError("view indices are outside the parent sequence")
        self.parent = parent
        self.indices = indices
        super().__init__(_ViewProvider(parent, indices))

    @property
    def closed(self) -> bool:
        return self._closed or self.parent.closed

    def _ensure_open(self) -> None:
        super()._ensure_open()
        self.parent._ensure_open()
