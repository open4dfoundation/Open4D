"""Open4D core data structures.

The fundamental 4D abstractions shared across Open4D. Algorithms in
:mod:`open4d.modules` are meant to depend on these rather than on raw arrays
or file formats:

- :class:`MeshSequence`  — ordered, timestamped triangle-mesh frames  (implemented)
- ``PointCloudSequence`` — planned
- ``TransformSequence``  — planned

These abstractions depend only on :mod:`numpy`. The heavy research pipelines
that also live under ``open4d/core`` (``tsmc``, ``tvmc``, ``N4MC``) have their
own toolchains (.NET, CUDA, Draco) and are intentionally NOT imported here;
reach one explicitly, e.g. ``from open4d.core import tsmc``, so that
``import open4d`` stays lightweight.
"""
from .mesh_sequence import MeshFrame, MeshSequence, FrameStore, NumpyFrameStore

__all__ = ["MeshFrame", "MeshSequence", "FrameStore", "NumpyFrameStore"]
