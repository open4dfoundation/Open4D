"""Frame provider contracts and in-memory implementation."""

from __future__ import annotations

from collections.abc import Sequence as CollectionSequence
from dataclasses import dataclass
from enum import Enum
from numbers import Integral
import operator
from types import MappingProxyType
from typing import Any, Mapping, Protocol, runtime_checkable

from .frame import Frame


class TopologyMode(str, Enum):
    """How triangle connectivity varies over a sequence."""

    FIXED = "fixed"
    CHANGING = "changing"
    UNKNOWN = "unknown"


class DependencyMode(str, Enum):
    """Whether a frame can be decoded on its own.

    :class:`Sequence` is lazy and random-access, which is the right contract for
    a directory of files but assumes a frame is always reachable in one step.
    Every real 4D codec here breaks that assumption in one of two ways, and a
    consumer that wants to seek has to know which.
    """

    #: Every frame stands alone. A directory of per-frame OBJ or PLY files.
    INDEPENDENT = "independent"
    #: Frames depend on the most recent key frame and on each other in order,
    #: as in Vega's group-of-volumes structure: a residual frame is undecodable
    #: without its key. Seeking backwards is allowed, but only to a key frame.
    GOP = "gop"
    #: The decode stream is pulled in order and cannot be rewound at all, so
    #: reaching an earlier frame means replaying from the start. ReRF's decoder
    #: is this: `gs_tools.methods._rerf_rig_render` exists because of it.
    SEQUENTIAL = "sequential"


@dataclass(frozen=True)
class Dependency:
    """How frames in a sequence depend on one another.

    Declared by a provider so a consumer can plan a seek instead of discovering
    mid-playback that frame 12 renders as garbage without frame 8. The default is
    :attr:`DependencyMode.INDEPENDENT`, which is what a directory of files is and
    what every existing provider gets without changing.
    """

    mode: DependencyMode = DependencyMode.INDEPENDENT
    #: Ordinals that can be decoded without prior state. Required for
    #: :attr:`DependencyMode.GOP` and meaningless otherwise.
    key_frames: tuple[int, ...] = ()

    def __post_init__(self) -> None:
        if not isinstance(self.mode, DependencyMode):
            raise TypeError("mode must be a DependencyMode")
        keys = tuple(self.key_frames)
        for key in keys:
            if not isinstance(key, Integral) or isinstance(key, bool):
                raise TypeError("key_frames must contain integers")
            if key < 0:
                raise ValueError("key_frames must be nonnegative")
        keys = tuple(sorted({int(key) for key in keys}))
        if self.mode is DependencyMode.GOP:
            if not keys:
                raise ValueError("GOP dependency requires at least one key frame")
            if keys[0] != 0:
                raise ValueError(
                    "GOP dependency requires frame 0 to be a key frame; a "
                    "sequence whose first frame cannot be decoded has no entry "
                    "point"
                )
        elif keys:
            raise ValueError(f"key_frames is meaningless for {self.mode.value}")
        object.__setattr__(self, "key_frames", keys)

    def key_for(self, index: int) -> int | None:
        """The key frame ``index`` decodes from, or None when it needs no key."""
        ordinal = operator.index(index)
        if self.mode is not DependencyMode.GOP:
            return None
        candidates = [key for key in self.key_frames if key <= ordinal]
        if not candidates:
            raise IndexError(f"no key frame at or before {ordinal}")
        return candidates[-1]

    def chain(self, index: int, *, decoded: int | None = None) -> tuple[int, ...]:
        """Frames to decode, in order, to make ``index`` available.

        ``decoded`` is the ordinal the consumer's decoder is currently positioned
        at, if any -- passing it lets a forward seek reuse that state instead of
        restarting. Under :attr:`DependencyMode.SEQUENTIAL` a *backward* seek
        cannot reuse it, because the stream does not rewind, so the chain comes
        back as a full replay from zero. That asymmetry is the point of
        declaring the mode at all.

        :attr:`DependencyMode.INDEPENDENT` always returns just ``index``: it
        carries no decoder state, so whether the frame is already in a consumer's
        cache is the consumer's business, not this model's.
        """
        ordinal = operator.index(index)
        if ordinal < 0:
            raise ValueError("index must be nonnegative")
        if decoded is not None:
            decoded = operator.index(decoded)
            if decoded < 0:
                raise ValueError("decoded must be nonnegative")

        if self.mode is DependencyMode.INDEPENDENT:
            return (ordinal,)

        if decoded == ordinal:
            return ()

        if self.mode is DependencyMode.GOP:
            start = self.key_for(ordinal)
            if decoded is not None and start <= decoded < ordinal:
                start = decoded + 1
        else:  # SEQUENTIAL
            start = decoded + 1 if decoded is not None and decoded < ordinal else 0

        return tuple(range(start, ordinal + 1))


@runtime_checkable
class FrameProvider(Protocol):
    """Minimum random-access contract used by :class:`Sequence`.

    Providers may additionally expose ``metadata``, ``timestamps``,
    ``topology``, ``dependency``, ``has_constant_vertex_count``,
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
        allow_nonmonotonic_timestamps: bool = False,
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
        if not isinstance(allow_nonmonotonic_timestamps, bool):
            raise TypeError("allow_nonmonotonic_timestamps must be bool")

        self._frames = stored_frames
        self.metadata = MappingProxyType(dict(metadata or {}))
        self.topology = topology
        self.has_constant_vertex_count = has_constant_vertex_count
        self.has_vertex_correspondence = has_vertex_correspondence
        self.allow_nonmonotonic_timestamps = allow_nonmonotonic_timestamps

    @property
    def frame_count(self) -> int:
        return len(self._frames)

    @property
    def timestamps(self) -> tuple[float, ...]:
        return tuple(frame.timestamp for frame in self._frames)

    def get_frame(self, index: int) -> Frame:
        if isinstance(index, bool):
            raise TypeError("frame index must be an integer")
        try:
            ordinal = operator.index(index)
        except TypeError as exc:
            raise TypeError("frame index must be an integer") from exc
        if ordinal < 0 or ordinal >= self.frame_count:
            raise IndexError("frame index out of range")
        return self._frames[ordinal]
