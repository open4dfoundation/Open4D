"""Lazy temporal geometry sequences and views."""

from __future__ import annotations

import math
import operator
from collections.abc import Iterator
from numbers import Integral, Real
from types import MappingProxyType
from typing import Any, Mapping, overload

from .frame import Frame
from .provider import FrameProvider, TopologyMode


class Sequence:
    """A lazy, random-access temporal geometry sequence."""

    def __init__(self, provider: FrameProvider) -> None:
        """
        Initialize a sequence from a frame provider.
        
        Parameters:
            provider (FrameProvider): Provider supplying the sequence frames, metadata, and topology.
        """
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
        """
        Retrieve a frame by ordinal index or create a view for a slice.
        
        Parameters:
            index (int | slice): The frame index or slice of frame indices.
        
        Returns:
            Frame | SequenceView: The selected frame or a view containing the selected frames.
        
        Raises:
            TypeError: If `index` is not an integer or slice, or if the provider returns an invalid frame.
            IndexError: If an integer index is outside the sequence bounds.
        """
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
        for index in range(len(self)):
            yield self[index]

    @property
    def metadata(self) -> Mapping[str, Any]:
        return self._metadata

    @property
    def timestamps(self) -> tuple[float, ...]:
        """
        Provide the sequence timestamps, validating and caching them on first access.
        
        Returns:
        	tuple[float, ...]: Finite timestamp values for the sequence.
        
        Raises:
        	TypeError: If timestamps or the provider's monotonicity setting have an invalid type.
        	ValueError: If timestamps have an invalid length, contain non-finite values, or decrease when nonmonotonic timestamps are disallowed.
        """
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
        """
        Calculate the elapsed time between the first and last timestamps.
        
        Returns:
        	float: The absolute difference between the first and last timestamps, or 0.0 for a sequence with fewer than two timestamps.
        """
        timestamps = self.timestamps
        return abs(timestamps[-1] - timestamps[0]) if len(timestamps) > 1 else 0.0

    @property
    def fps(self) -> float | None:
        """Compute the average frame rate from the sequence duration.
        
        Returns:
        	float | None: The frames per second, or `None` when the sequence has fewer than two frames or no positive duration.
        """
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
        """
        Close the sequence's provider resources once.
        
        Raises:
        	TypeError: If the provider defines a non-callable `close` attribute.
        """
        if self._closed:
            return
        close = getattr(self._provider, "close", None)
        if close is not None:
            if not callable(close):
                raise TypeError("provider close must be callable")
            close()
        self._closed = True

    def __enter__(self) -> "Sequence":
        """Enter the sequence's context-manager scope and return the sequence."""
        return self

    def __exit__(self, exc_type: object, exc: object, traceback: object) -> None:
        self.close()


class _ViewProvider:
    def __init__(self, parent: Sequence, indices: range) -> None:
        """Initialize a sequence view over selected indices of a parent sequence.
        
        Parameters:
        	parent (Sequence): The sequence providing the view's data and metadata.
        	indices (range): The parent sequence indices included in the view.
        """
        self.parent = parent
        self.indices = indices
        self.metadata = parent.metadata
        self.topology = parent.topology
        self.has_constant_vertex_count = parent.has_constant_vertex_count
        self.has_vertex_correspondence = parent.has_vertex_correspondence
        self.allow_nonmonotonic_timestamps = True

    @property
    def frame_count(self) -> int:
        return len(self.indices)

    @property
    def timestamps(self) -> tuple[float, ...]:
        """Return the timestamps corresponding to the selected frames.
        
        Returns:
            tuple[float, ...]: The selected frame timestamps in view order.
        """
        return tuple(self.parent.timestamps[index] for index in self.indices)

    def get_frame(self, index: int) -> Frame:
        """
        Retrieve a frame by its ordinal index within the view.
        
        Parameters:
        	index (int): Ordinal index of the frame in the view.
        
        Returns:
        	Frame: The frame at the specified view index.
        """
        return self.parent[self.indices[index]]


class SequenceView(Sequence):
    """A lightweight ordinal view into another sequence."""

    def __init__(self, parent: Sequence, indices: range) -> None:
        self.parent = parent
        self.indices = indices
        super().__init__(_ViewProvider(parent, indices))
