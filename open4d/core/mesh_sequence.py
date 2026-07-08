"""open4d.core.mesh_sequence — the ``MeshSequence`` 4D abstraction.

Prototype, pure-Python implementation of the contract promised in the core
README. A :class:`MeshSequence` is an ordered, timestamped list of triangle
mesh *frames* (vertices + faces). Algorithms in :mod:`open4d.modules` are
meant to depend on this abstraction rather than on raw arrays or file formats.

Design for the eventual native backend
--------------------------------------
Storage lives behind a tiny interface, :class:`FrameStore`. The default,
:class:`NumpyFrameStore`, keeps frames as Python-side numpy arrays. A future
native store can implement the same four methods while returning numpy *views*
onto a C++ memory buffer, and nothing above it changes.

To make that swap seamless, the numpy backend already enforces the exact
memory layout a native buffer exposes:

* vertices — ``float32``, shape ``(N, 3)``, C-contiguous
* faces    — ``uint32``,  shape ``(M, 3)``, C-contiguous

so callers written against this prototype keep working unchanged once frames
are backed by native memory.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Iterator, List, Optional, Sequence, Union

import numpy as np

__all__ = ["MeshFrame", "MeshSequence", "FrameStore", "NumpyFrameStore"]


# --------------------------------------------------------------------------- #
# Validation helpers — the single choke point that guarantees the memory
# layout a native buffer will rely on.
# --------------------------------------------------------------------------- #
def _as_vertices(v) -> np.ndarray:
    a = np.ascontiguousarray(v, dtype=np.float32)
    if a.ndim != 2 or a.shape[1] != 3:
        raise ValueError(f"vertices must have shape (N, 3), got {a.shape}")
    return a


def _as_faces(f) -> np.ndarray:
    a = np.ascontiguousarray(f, dtype=np.uint32)
    if a.ndim != 2 or a.shape[1] != 3:
        raise ValueError(f"faces must have shape (M, 3), got {a.shape}")
    return a


# --------------------------------------------------------------------------- #
# One frame of the sequence.
# --------------------------------------------------------------------------- #
@dataclass(frozen=True)
class MeshFrame:
    """A single timestamped triangle mesh.

    ``vertices`` and ``faces`` are always C-contiguous arrays of dtype
    ``float32`` / ``uint32`` respectively. The object is frozen so a frame
    handed out by a :class:`MeshSequence` cannot silently mutate the store.
    """

    index: int
    timestamp: float
    vertices: np.ndarray  # (N, 3) float32
    faces: np.ndarray     # (M, 3) uint32

    @property
    def num_vertices(self) -> int:
        return int(self.vertices.shape[0])

    @property
    def num_faces(self) -> int:
        return int(self.faces.shape[0])

    def __repr__(self) -> str:
        return (
            f"MeshFrame(index={self.index}, t={self.timestamp:.4g}, "
            f"V={self.num_vertices}, F={self.num_faces})"
        )


# --------------------------------------------------------------------------- #
# Storage backend interface + default numpy implementation.
# --------------------------------------------------------------------------- #
class FrameStore:
    """Backend interface for frame storage.

    A native (C++) implementation subclasses this and returns numpy views onto
    its own buffers; the numpy default below is the prototype backing store.
    """

    def __len__(self) -> int:  # number of frames
        raise NotImplementedError

    def append(self, vertices: np.ndarray, faces: np.ndarray, timestamp: float) -> None:
        raise NotImplementedError

    def vertices(self, i: int) -> np.ndarray:
        raise NotImplementedError

    def faces(self, i: int) -> np.ndarray:
        raise NotImplementedError

    def timestamp(self, i: int) -> float:
        raise NotImplementedError


class NumpyFrameStore(FrameStore):
    """Default Python-side store: three parallel lists of frame data."""

    def __init__(self) -> None:
        self._vertices: List[np.ndarray] = []
        self._faces: List[np.ndarray] = []
        self._timestamps: List[float] = []

    def __len__(self) -> int:
        return len(self._timestamps)

    def append(self, vertices: np.ndarray, faces: np.ndarray, timestamp: float) -> None:
        # arrays are already validated/normalized by MeshSequence before this.
        self._vertices.append(vertices)
        self._faces.append(faces)
        self._timestamps.append(float(timestamp))

    def vertices(self, i: int) -> np.ndarray:
        return self._vertices[i]

    def faces(self, i: int) -> np.ndarray:
        return self._faces[i]

    def timestamp(self, i: int) -> float:
        return self._timestamps[i]


# --------------------------------------------------------------------------- #
# The sequence.
# --------------------------------------------------------------------------- #
class MeshSequence:
    """An ordered, timestamped sequence of triangle-mesh frames.

    Behaves like a fixed-length container of :class:`MeshFrame` once built: it
    is sized (``len``), iterable, and indexable (integer index -> a
    :class:`MeshFrame`; slice -> a new :class:`MeshSequence`). Build it
    incrementally with :meth:`append`, or in one shot with
    :meth:`from_frames` / :meth:`from_o4d`.
    """

    def __init__(self, *, name: Optional[str] = None, store: Optional[FrameStore] = None) -> None:
        self.name = name
        self._store: FrameStore = store if store is not None else NumpyFrameStore()

    # ---- construction ---------------------------------------------------- #
    def append(self, vertices, faces, timestamp: Optional[float] = None) -> "MeshSequence":
        """Append one frame. Returns ``self`` so calls can be chained.

        ``timestamp`` defaults to the integer frame index as a float (matching
        the Open4D container convention).
        """
        v = _as_vertices(vertices)
        f = _as_faces(faces)
        if timestamp is None:
            timestamp = float(len(self._store))
        self._store.append(v, f, timestamp)
        return self

    @classmethod
    def from_frames(
        cls,
        vertices: Sequence[np.ndarray],
        faces: Union[np.ndarray, Sequence[np.ndarray]],
        timestamps: Optional[Sequence[float]] = None,
        *,
        name: Optional[str] = None,
    ) -> "MeshSequence":
        """Build from a list of per-frame vertices.

        ``faces`` may be a single ``(M, 3)`` array shared by every frame (the
        common topology-constant case) or one array per frame. ``timestamps``
        defaults to ``0, 1, 2, ...``.
        """
        n = len(vertices)
        shared = isinstance(faces, np.ndarray)
        if not shared and len(faces) != n:
            raise ValueError(f"got {n} vertex frames but {len(faces)} face arrays")
        if timestamps is not None and len(timestamps) != n:
            raise ValueError(f"got {n} frames but {len(timestamps)} timestamps")

        seq = cls(name=name)
        for i in range(n):
            fcs = faces if shared else faces[i]
            ts = None if timestamps is None else timestamps[i]
            seq.append(vertices[i], fcs, ts)
        return seq

    @classmethod
    def from_o4d(cls, path: str, *, name: Optional[str] = None) -> "MeshSequence":
        """Load every keyframe of an ``.o4d`` mesh container into a sequence."""
        from open4d.io import O4DMeshReader  # local import: keep core import light

        reader = O4DMeshReader(path)
        reader.open()
        try:
            seq = cls(name=name or path)
            for frame_index, _ in reader.iter_frames():
                v, f, t = reader.get_frame(frame_index)
                seq.append(v, f, t)
            return seq
        finally:
            reader.close()

    # ---- container protocol ---------------------------------------------- #
    def __len__(self) -> int:
        return len(self._store)

    def __getitem__(self, key: Union[int, slice]) -> Union[MeshFrame, "MeshSequence"]:
        if isinstance(key, slice):
            out = MeshSequence(name=self.name)
            for i in range(*key.indices(len(self))):
                out.append(self._store.vertices(i), self._store.faces(i), self._store.timestamp(i))
            return out
        i = key + len(self) if key < 0 else key
        if not 0 <= i < len(self):
            raise IndexError(f"frame index {key} out of range for {len(self)} frames")
        return MeshFrame(
            index=i,
            timestamp=self._store.timestamp(i),
            vertices=self._store.vertices(i),
            faces=self._store.faces(i),
        )

    def __iter__(self) -> Iterator[MeshFrame]:
        for i in range(len(self)):
            yield self[i]

    # ---- properties ------------------------------------------------------ #
    @property
    def num_frames(self) -> int:
        return len(self)

    @property
    def timestamps(self) -> np.ndarray:
        return np.array([self._store.timestamp(i) for i in range(len(self))], dtype=np.float64)

    @property
    def duration(self) -> float:
        """Span from first to last timestamp (0.0 for fewer than 2 frames)."""
        if len(self) < 2:
            return 0.0
        t = self.timestamps
        return float(t[-1] - t[0])

    def is_topology_constant(self) -> bool:
        """True if every frame shares identical faces (fixed connectivity)."""
        if len(self) < 2:
            return True
        f0 = self._store.faces(0)
        for i in range(1, len(self)):
            fi = self._store.faces(i)
            if fi.shape != f0.shape or not np.array_equal(fi, f0):
                return False
        return True

    @property
    def faces(self) -> np.ndarray:
        """Shared face connectivity. Only valid when topology is constant."""
        if len(self) == 0:
            raise ValueError("empty sequence has no faces")
        if not self.is_topology_constant():
            raise ValueError(
                "faces vary per frame; index a frame and use frame.faces instead"
            )
        return self._store.faces(0)

    # ---- export ---------------------------------------------------------- #
    def to_o4d(self, path: str) -> str:
        """Write the sequence to an ``.o4d`` mesh container. Returns the path."""
        from open4d.io import O4DMeshWriter  # local import: keep core import light

        writer = O4DMeshWriter(path)
        writer.open()
        try:
            for frame in self:
                writer.write_keyframe(frame.vertices, frame.faces, timestamp=frame.timestamp)
        finally:
            writer.close()
        return path

    # ---- misc ------------------------------------------------------------ #
    def __repr__(self) -> str:
        name = f"{self.name!r}, " if self.name else ""
        topo = "fixed-topology" if self.is_topology_constant() else "varying-topology"
        return f"MeshSequence({name}frames={len(self)}, duration={self.duration:.4g}, {topo})"
