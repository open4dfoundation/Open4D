"""Open4D IO — readers and writers for the Open4D-native container format.

The ``.o4d`` container stores time-varying geometry (meshes, point clouds,
Draco-compressed point clouds) as a chunked stream with a seekable frame
index. Each geometry kind has a paired writer/reader:

    >>> from open4d.io import O4DMeshWriter, O4DMeshReader
    >>> with O4DMeshWriter("clip.o4d") as w:
    ...     w.add_frame(vertices, faces, timestamp=0.0)
    >>> reader = O4DMeshReader("clip.o4d")
    >>> for frame in reader:
    ...     ...

The mesh and point-cloud codecs depend only on :mod:`numpy`. The Draco
point-cloud codec additionally needs the optional ``DracoPy`` package; those
two classes are resolved lazily so ``import open4d.io`` works without it and
only errors (with an install hint) when you actually touch them.
"""
from importlib import import_module

# numpy-only codecs — always safe to import eagerly.
from .o4d_mesh_io import O4DMeshWriter, O4DMeshReader
from .o4d_pointcloud_io import O4DPointCloudWriter, O4DPointCloudReader

# Draco codec pulls the optional DracoPy dependency -> resolve lazily.
_LAZY = {
    "O4DDracoPointCloudWriter": ".o4d_draco_pointcloud_io",
    "O4DDracoPointCloudReader": ".o4d_draco_pointcloud_io",
}

__all__ = [
    "O4DMeshWriter",
    "O4DMeshReader",
    "O4DPointCloudWriter",
    "O4DPointCloudReader",
    "O4DDracoPointCloudWriter",
    "O4DDracoPointCloudReader",
]


def __getattr__(name):  # PEP 562
    if name in _LAZY:
        module = import_module(_LAZY[name], __name__)
        obj = getattr(module, name)
        globals()[name] = obj
        return obj
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")


def __dir__():
    return sorted(set(globals()) | set(__all__))
