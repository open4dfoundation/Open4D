"""Convert frames from Open4D's v1 readers to Open3D geometry objects."""

from __future__ import annotations

import os
import struct
from collections.abc import Iterator, Mapping, Sequence
from typing import Any

import numpy as np
from numpy.typing import ArrayLike, NDArray

_CHUNK_HEADER = struct.Struct("<4sQ")
_HEAD_FIXED = struct.Struct("<4sHBBII")
_MESH = 1
_POINT_CLOUD = 2
_RAW = 1
_DRACO_POINTS = 3


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

    Native reader tuples and frame-like mappings/objects are supported. A
    frame-like object may expose ``vertices``/``points``, ``faces``/``triangles``,
    ``colors``/``vertex_colors``, and ``normals``/``vertex_normals``.
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


def _container_kind(path: os.PathLike[str] | str) -> tuple[int, int]:
    with open(path, "rb") as stream:
        chunk_header = stream.read(_CHUNK_HEADER.size)
        if len(chunk_header) != _CHUNK_HEADER.size:
            raise OSError("File is too small to be an Open4D container")
        chunk_type, payload_size = _CHUNK_HEADER.unpack(chunk_header)
        if chunk_type != b"HEAD" or payload_size < _HEAD_FIXED.size:
            raise OSError("Missing or invalid Open4D HEAD chunk")
        fixed = stream.read(_HEAD_FIXED.size)
    magic, version, geometry, codec, _flags, _meta_size = _HEAD_FIXED.unpack(fixed)
    if magic != b"O4D1":
        raise OSError("Not an Open4D file (bad magic)")
    if version != 1:
        raise OSError(f"Unsupported Open4D version: {version}")
    return geometry, codec


def _reader(path: os.PathLike[str] | str) -> Any:
    geometry, codec = _container_kind(path)
    path_string = os.fspath(path)
    if (geometry, codec) == (_MESH, _RAW):
        from open4d.io.o4d_mesh_io import O4DMeshReader

        return O4DMeshReader(path_string)
    if (geometry, codec) == (_POINT_CLOUD, _RAW):
        from open4d.io.o4d_pointcloud_io import O4DPointCloudReader

        return O4DPointCloudReader(path_string)
    if (geometry, codec) == (_POINT_CLOUD, _DRACO_POINTS):
        try:
            from open4d.io.o4d_draco_pointcloud_io import O4DDracoPointCloudReader
        except ImportError as exc:
            raise ImportError(
                "This file uses Draco point-cloud compression. Install Open4D's "
                "Draco extra with: python -m pip install -e '.[draco]'"
            ) from exc
        return O4DDracoPointCloudReader(path_string)
    raise OSError(
        f"Unsupported Open4D geometry/codec combination: {geometry}/{codec}"
    )


def _frame_ids(reader: Any) -> list[int]:
    return [frame_index for frame_index, _timestamp in reader.iter_frames()]


def load_frame(path: os.PathLike[str] | str, frame_index: int = 0) -> Any:
    """Decode one stored frame ID from *path* and return Open3D geometry."""
    if not isinstance(frame_index, int) or isinstance(frame_index, bool):
        raise TypeError("frame_index must be an integer")
    with _reader(path) as reader:
        frame_ids = _frame_ids(reader)
        if frame_index not in frame_ids:
            raise IndexError(
                f"Frame {frame_index} is not present; available frame IDs: {frame_ids}"
            )
        return frame_to_open3d(reader.get_frame(frame_index))


def iter_frames(
    path: os.PathLike[str] | str,
    start: int = 0,
    stop: int | None = None,
    step: int = 1,
) -> Iterator[Any]:
    """Lazily decode an ordinal slice of the frames in *path*.

    ``start``, ``stop``, and ``step`` use normal Python slicing semantics over
    the ordered frame index. Only the current decoded frame is retained here.
    """
    if not all(
        isinstance(value, int) and not isinstance(value, bool)
        for value in (start, step)
    ):
        raise TypeError("start and step must be integers")
    if stop is not None and (not isinstance(stop, int) or isinstance(stop, bool)):
        raise TypeError("stop must be an integer or None")
    if step == 0:
        raise ValueError("step must not be zero")

    with _reader(path) as reader:
        frame_ids = _frame_ids(reader)
        for frame_id in frame_ids[slice(start, stop, step)]:
            yield frame_to_open3d(reader.get_frame(frame_id))


def sequence_info(path: os.PathLike[str] | str) -> dict[str, Any]:
    """Return container metadata without decoding its geometry frames."""
    geometry, codec = _container_kind(path)
    with _reader(path) as reader:
        frame_ids = _frame_ids(reader)
        metadata = dict(reader.meta)

    geometry_name = "triangle_mesh" if geometry == _MESH else "point_cloud"
    codec_name = {
        (_MESH, _RAW): "raw_mesh",
        (_POINT_CLOUD, _RAW): "raw_points",
        (_POINT_CLOUD, _DRACO_POINTS): "draco_points",
    }[(geometry, codec)]
    return {
        "frame_count": len(frame_ids),
        "frame_ids": frame_ids,
        "geometry_type": geometry_name,
        "codec": codec_name,
        "attributes": {
            "positions": True,
            "triangles": geometry == _MESH,
            "colors": "optional_per_frame" if geometry == _POINT_CLOUD else False,
            "normals": False,
        },
        "metadata": metadata,
    }
