"""Convert decoded Open4D frames to Open3D geometry objects.

This is a converter, not a loader: it takes a frame that something else has
already decoded — an `open4d.core.Frame`'s geometry, or any mapping or object
exposing the usual attribute names — and returns the matching Open3D mesh or
point cloud.
"""

from __future__ import annotations

from collections.abc import Mapping, Sequence
from typing import Any

import numpy as np
from numpy.typing import ArrayLike, NDArray


def _open3d() -> Any:
    try:
        import open3d as o3d
    except ImportError as exc:
        raise ImportError(
            "The Open3D integration requires the optional 'open3d' package. "
            "Install it with: python -m pip install -r "
            "integrations/open3d/requirements.txt"
        ) from exc
    return o3d


def _member(frame: object, *names: str) -> Any:
    if isinstance(frame, Mapping):
        for name in names:
            if name in frame:
                return frame[name]
    else:
        for name in names:
            if hasattr(frame, name):
                return getattr(frame, name)
    return None


def _unpack_frame(frame: object) -> tuple[Any, Any, Any, Any]:
    """Return positions, triangles, colors, and normals from a decoded frame."""
    if isinstance(frame, Sequence) and not isinstance(frame, (str, bytes, np.ndarray)):
        if len(frame) != 3:
            raise TypeError(
                "Decoded Open4D tuple frames must contain three items: geometry, "
                "connectivity/colors, and timestamp"
            )
        positions, auxiliary, _timestamp = frame
        if auxiliary is None or np.asarray(auxiliary).dtype == np.uint8:
            return positions, None, auxiliary, None
        return positions, auxiliary, None, None

    positions = _member(frame, "vertices", "points", "positions", "points_xyz")
    triangles = _member(frame, "triangles", "faces", "indices")
    colors = _member(frame, "vertex_colors", "colors", "colors_rgb")
    normals = _member(frame, "vertex_normals", "normals")
    return positions, triangles, colors, normals


def _positions(value: ArrayLike | None) -> NDArray[np.float64]:
    if value is None:
        raise ValueError("Frame does not contain vertices or points")
    array = np.asarray(value)
    if array.ndim != 2 or array.shape[1:] != (3,):
        raise ValueError(f"Vertices/points must have shape (N, 3); got {array.shape}")
    if not np.issubdtype(array.dtype, np.number):
        raise TypeError("Vertices/points must be numeric")
    result = np.asarray(array, dtype=np.float64)
    if not np.isfinite(result).all():
        raise ValueError("Vertices/points must contain only finite values")
    return result


def _triangles(value: ArrayLike, vertex_count: int) -> NDArray[np.int32]:
    array = np.asarray(value)
    if array.ndim != 2 or array.shape[1:] != (3,):
        raise ValueError(f"Triangles must have shape (M, 3); got {array.shape}")
    if not np.issubdtype(array.dtype, np.integer):
        raise TypeError("Triangle indices must be integers")
    if array.size and (np.any(array < 0) or np.any(array >= vertex_count)):
        raise ValueError(
            f"Triangle indices must be between 0 and {vertex_count - 1}"
        )
    return np.asarray(array, dtype=np.int32)


def _vertex_attribute(
    value: ArrayLike, vertex_count: int, name: str
) -> NDArray[np.float64]:
    array = np.asarray(value)
    if array.shape != (vertex_count, 3):
        raise ValueError(
            f"{name} must have shape ({vertex_count}, 3); got {array.shape}"
        )
    if not np.issubdtype(array.dtype, np.number):
        raise TypeError(f"{name} must be numeric")
    result = np.asarray(array, dtype=np.float64)
    if not np.isfinite(result).all():
        raise ValueError(f"{name} must contain only finite values")
    return result


def _colors(value: ArrayLike, vertex_count: int) -> NDArray[np.float64]:
    array = _vertex_attribute(value, vertex_count, "Colors")
    if np.issubdtype(np.asarray(value).dtype, np.integer):
        if np.any(array < 0) or np.any(array > 255):
            raise ValueError("Integer colors must be in the range [0, 255]")
        array = array / 255.0
    elif np.any(array < 0.0) or np.any(array > 1.0):
        raise ValueError("Floating-point colors must be in the range [0, 1]")
    return array


def frame_to_open3d(frame: object) -> Any:
    """Convert a decoded Open4D frame to an Open3D mesh or point cloud.

    Frame-like mappings and objects are supported, as are plain tuples. A
    frame-like object may expose ``vertices``/``points``, ``faces``/``triangles``,
    ``colors``/``vertex_colors``, and ``normals``/``vertex_normals``. A frame
    carrying no triangles becomes a point cloud.
    """
    positions_value, triangles_value, colors_value, normals_value = _unpack_frame(frame)
    positions = _positions(positions_value)
    o3d = _open3d()

    if triangles_value is not None:
        triangles = _triangles(triangles_value, len(positions))
        if len(triangles) > 0:
            geometry = o3d.geometry.TriangleMesh(
                o3d.utility.Vector3dVector(positions),
                o3d.utility.Vector3iVector(triangles),
            )
            if colors_value is not None:
                geometry.vertex_colors = o3d.utility.Vector3dVector(
                    _colors(colors_value, len(positions))
                )
            if normals_value is not None:
                geometry.vertex_normals = o3d.utility.Vector3dVector(
                    _vertex_attribute(normals_value, len(positions), "Normals")
                )
            return geometry

    geometry = o3d.geometry.PointCloud(o3d.utility.Vector3dVector(positions))
    if colors_value is not None:
        geometry.colors = o3d.utility.Vector3dVector(
            _colors(colors_value, len(positions))
        )
    if normals_value is not None:
        geometry.normals = o3d.utility.Vector3dVector(
            _vertex_attribute(normals_value, len(positions), "Normals")
        )
    return geometry
