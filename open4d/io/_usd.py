"""OpenUSD sequence-container reader and writer."""

from __future__ import annotations

from collections.abc import Mapping
import json
import math
from numbers import Real
import os
from pathlib import Path
import tempfile
from types import MappingProxyType
from typing import Any

import numpy as np

from open4d.core import Frame, Sequence, TopologyMode, TriangleMesh

from ._errors import (
    DecodeError,
    EncodeError,
    MissingDependencyError,
    UnsupportedFeatureError,
)

USD_SUFFIXES = (".usd", ".usda", ".usdc", ".usdz")
SCHEMA = "open4d.usd-sequence/v1"
PRIM_PATH = "/Open4D/Sequence"
_DEFAULT_FPS = 30.0
_NO_TRIANGLES = np.empty((0, 3), dtype=np.uint32)


def _pxr():
    try:
        from pxr import Sdf, Usd, UsdGeom, Vt
    except ImportError as error:
        raise MissingDependencyError(
            "OpenUSD support needs the optional dependency; install it with: "
            "python -m pip install 'open4d[usd]'"
        ) from error
    return Sdf, Usd, UsdGeom, Vt


def _json_value(value: Any, name: str) -> Any:
    if value is None or isinstance(value, (str, int, bool)):
        return value
    if isinstance(value, float):
        if not math.isfinite(value):
            raise EncodeError(f"{name} metadata numbers must be finite")
        return value
    if isinstance(value, np.generic):
        return _json_value(value.item(), name)
    if isinstance(value, Path):
        return str(value)
    if isinstance(value, Mapping):
        if any(not isinstance(key, str) for key in value):
            raise EncodeError(f"{name} metadata keys must be strings")
        return {
            key: _json_value(item, f"{name}.{key}")
            for key, item in value.items()
        }
    if isinstance(value, (list, tuple)):
        return [_json_value(item, name) for item in value]
    raise EncodeError(
        f"{name} metadata value {type(value).__name__} is not serializable"
    )


def _positive_fps(value: float | None, sequence: Sequence | None = None) -> float:
    if value is None:
        declared = None if sequence is None else sequence.metadata.get("fps")
        value = declared if declared is not None else (
            None if sequence is None else sequence.fps
        )
        if value is None:
            value = _DEFAULT_FPS
    if not isinstance(value, Real) or isinstance(value, bool):
        raise TypeError("fps must be a real number or None")
    result = float(value)
    if not math.isfinite(result) or result <= 0:
        raise ValueError("fps must be finite and greater than zero")
    return result


def _attribute_kind(array: np.ndarray) -> str:
    if np.issubdtype(array.dtype, np.floating):
        return "float"
    if np.issubdtype(array.dtype, np.integer):
        return "int"
    if np.issubdtype(array.dtype, np.bool_):
        return "bool"
    raise EncodeError(f"unsupported custom attribute dtype {array.dtype}")


def _triangulate(counts: np.ndarray, indices: np.ndarray) -> np.ndarray:
    if not len(counts):
        return _NO_TRIANGLES
    if np.any(counts < 3) or int(counts.sum()) != len(indices):
        raise DecodeError("USD face counts do not match face indices")
    if np.all(counts == 3):
        return np.asarray(indices, dtype=np.uint32).reshape(-1, 3)
    triangles: list[tuple[int, int, int]] = []
    offset = 0
    for count in counts.tolist():
        face = indices[offset : offset + count]
        triangles.extend(
            (int(face[0]), int(face[corner]), int(face[corner + 1]))
            for corner in range(1, count - 1)
        )
        offset += count
    return np.asarray(triangles, dtype=np.uint32).reshape(-1, 3)


def _geometry_prim(stage: Any, UsdGeom: Any, prim_path: str | None) -> Any:
    if prim_path is not None:
        prim = stage.GetPrimAtPath(prim_path)
        if not prim or not prim.IsValid():
            raise DecodeError(f"USD prim does not exist: {prim_path}")
        return prim
    prim = stage.GetPrimAtPath(PRIM_PATH)
    if prim and prim.IsValid():
        return prim
    for candidate in stage.Traverse():
        if candidate.IsA(UsdGeom.Mesh) or candidate.IsA(UsdGeom.Points):
            return candidate
    raise DecodeError("USD stage contains no mesh or point geometry")


def _time_samples(attributes: tuple[Any, ...]) -> tuple[float | None, ...]:
    """Return the ordered union of samples that can change a decoded frame."""
    samples: set[float] = set()
    for attribute in attributes:
        if attribute:
            samples.update(float(value) for value in attribute.GetTimeSamples())
    return tuple(sorted(samples)) or (None,)


def _read_manifest(stage: Any) -> tuple[dict[str, Any], bool]:
    record = dict(
        (stage.GetRootLayer().customLayerData or {}).get("open4d", {})
    )
    if record.get("schema") == SCHEMA:
        try:
            manifest = json.loads(record["manifest"])
        except (KeyError, TypeError, json.JSONDecodeError) as error:
            raise DecodeError(f"invalid Open4D USD manifest: {error}") from error
        if not isinstance(manifest, dict):
            raise DecodeError("Open4D USD manifest must be an object")
        return manifest, True
    return record, False


class UsdSequenceProvider:
    """Lazy frame provider over one time-sampled USD geometry prim."""

    def __init__(
        self,
        path: str | os.PathLike[str],
        *,
        fps: float | None = None,
        prim_path: str | None = None,
    ) -> None:
        self.path = Path(path).absolute()
        self._Sdf, self._Usd, self._UsdGeom, self._Vt = _pxr()
        try:
            self._stage = self._Usd.Stage.Open(str(self.path))
        except Exception as error:
            raise DecodeError(f"USD could not open {self.path}: {error}") from error
        if self._stage is None:
            raise DecodeError(f"USD could not open {self.path}")
        self._prim = _geometry_prim(self._stage, self._UsdGeom, prim_path)
        self._points = self._UsdGeom.PointBased(self._prim).GetPointsAttr()
        if not self._points:
            raise DecodeError(f"USD geometry prim {self._prim.GetPath()} has no points")
        mesh = self._UsdGeom.Mesh(self._prim) if self._prim.IsA(self._UsdGeom.Mesh) else None
        self._counts = mesh.GetFaceVertexCountsAttr() if mesh else None
        self._indices = mesh.GetFaceVertexIndicesAttr() if mesh else None
        self._extent = self._UsdGeom.Boundable(self._prim).GetExtentAttr()
        self._colors = self._UsdGeom.PrimvarsAPI(self._prim).GetPrimvar("displayColor")
        self._opacity = self._UsdGeom.PrimvarsAPI(self._prim).GetPrimvar("displayOpacity")
        self._normals = mesh.GetNormalsAttr() if mesh else None
        self._vertex_uv = self._prim.GetAttribute("open4d:vertexUV")
        self._corner_uv = self._prim.GetAttribute("open4d:cornerUV")
        self._frame_index = self._prim.GetAttribute("open4d:frameIndex")
        self._timestamp = self._prim.GetAttribute("open4d:timestamp")
        self._descriptor = self._prim.GetAttribute("open4d:frameDescriptor")
        self._legacy_streams = {
            name: self._prim.GetAttribute(f"open4d:{name}")
            for name in (
                "frameIndex",
                "timestamp",
                "keyFrame",
                "vertexCount",
                "triangleCount",
            )
        }
        self._legacy_streams = {
            name: attribute
            for name, attribute in self._legacy_streams.items()
            if attribute and attribute.GetNumTimeSamples() > 0
        }

        self._manifest, self._native = _read_manifest(self._stage)
        custom_streams = tuple(
            attribute
            for attribute in self._prim.GetAttributes()
            if attribute.GetName().startswith("open4d:attribute")
        )
        self._times = _time_samples((
            self._points,
            self._counts,
            self._indices,
            self._colors,
            self._opacity,
            self._normals,
            self._vertex_uv,
            self._corner_uv,
            self._frame_index,
            self._timestamp,
            self._descriptor,
            *self._legacy_streams.values(),
            *custom_streams,
        ))
        stage_fps = self._stage.GetTimeCodesPerSecond() or _DEFAULT_FPS
        self.fps = _positive_fps(stage_fps if fps is None else fps)

        if self._native:
            try:
                self.topology = TopologyMode(
                    self._manifest.get("topology", "unknown")
                )
            except ValueError as error:
                raise DecodeError(f"invalid USD topology declaration: {error}") from error
            self.has_constant_vertex_count = self._manifest.get(
                "has_constant_vertex_count"
            )
            self.has_vertex_correspondence = self._manifest.get(
                "has_vertex_correspondence"
            )
            self.allow_nonmonotonic_timestamps = self._manifest.get(
                "allow_nonmonotonic_timestamps", False
            )
            sequence_metadata = self._manifest.get("metadata", {})
        else:
            self.topology = (
                TopologyMode.CHANGING
                if any(
                    attribute and attribute.GetNumTimeSamples() > 0
                    for attribute in (self._counts, self._indices)
                )
                else TopologyMode.FIXED
            )
            self.has_constant_vertex_count = None
            self.has_vertex_correspondence = None
            self.allow_nonmonotonic_timestamps = False
            sequence_metadata = {}
        if not isinstance(sequence_metadata, Mapping):
            raise DecodeError("USD sequence metadata must be an object")
        provider_metadata = {
            "name": self.path.stem,
            "source": str(self.path),
            "format": self.path.suffix.lower(),
            "fps": float(self._manifest.get("fps", self.fps)),
            "up_axis": str(self._UsdGeom.GetStageUpAxis(self._stage)).lower(),
            "prim": str(self._prim.GetPath()),
            "prim_type": str(self._prim.GetTypeName()),
            "schema": self._manifest.get("schema", SCHEMA if self._native else None),
        }
        # The prototype layout exposed its custom-layer record as provider
        # metadata. Keep that behavior for old files; v1 instead restores the
        # sequence metadata stored in its manifest and reserves provider keys.
        if self._native:
            metadata = {**dict(sequence_metadata), **provider_metadata}
        else:
            metadata = {**provider_metadata, **dict(self._manifest)}
        self.metadata = MappingProxyType(metadata)

    @property
    def frame_count(self) -> int:
        return len(self._times)

    def _time(self, ordinal: int):
        value = self._times[ordinal]
        return self._Usd.TimeCode.Default() if value is None else self._Usd.TimeCode(value)

    @property
    def timestamps(self) -> tuple[float, ...]:
        if self._timestamp and self._timestamp.GetNumTimeSamples() > 0:
            return tuple(float(self._timestamp.Get(self._time(i))) for i in range(len(self._times)))
        return tuple(
            index / self.fps if value is None else float(value) / self.fps
            for index, value in enumerate(self._times)
        )

    def _descriptor_at(self, index: int) -> dict[str, Any]:
        if not self._descriptor:
            return {}
        value = self._descriptor.Get(self._time(index))
        if not value:
            return {}
        try:
            result = json.loads(value)
        except (TypeError, json.JSONDecodeError) as error:
            raise DecodeError(f"invalid USD frame {index} descriptor: {error}") from error
        if not isinstance(result, dict):
            raise DecodeError(f"USD frame {index} descriptor must be an object")
        return result

    def get_frame(self, index: int) -> Frame:
        if index < 0 or index >= self.frame_count:
            raise IndexError("frame index out of range")
        time = self._time(index)
        try:
            points_value = self._points.Get(time)
            positions = np.asarray(points_value or [], dtype=np.float32).reshape(-1, 3)
            triangles = _NO_TRIANGLES
            if self._counts and self._indices:
                counts_value = self._counts.Get(time)
                indices_value = self._indices.Get(time)
                if counts_value is not None and indices_value is not None:
                    triangles = _triangulate(
                        np.asarray(counts_value, dtype=np.int64),
                        np.asarray(indices_value, dtype=np.int64),
                    )

            descriptor = self._descriptor_at(index)
            colors = None
            channels = int(descriptor.get("color_channels", 0))
            if channels and self._colors:
                rgb = np.asarray(self._colors.Get(time), dtype=np.float32).reshape(-1, 3)
                if channels == 4:
                    alpha = np.asarray(self._opacity.Get(time), dtype=np.float32).reshape(-1, 1)
                    colors = np.column_stack((rgb, alpha))
                else:
                    colors = rgb
            elif not self._native and self._colors:
                value = self._colors.Get(time)
                if value is not None:
                    candidate = np.asarray(value, dtype=np.float32).reshape(-1, 3)
                    if len(candidate) == len(positions):
                        colors = candidate

            normals = None
            if descriptor.get("normals") and self._normals:
                normals = np.asarray(self._normals.Get(time), dtype=np.float32).reshape(-1, 3)

            texture_coordinates = None
            uv_layout = descriptor.get("uv_layout")
            uv_shape = tuple(descriptor.get("uv_shape", ()))
            if uv_layout == "vertex" and self._vertex_uv:
                texture_coordinates = np.asarray(
                    self._vertex_uv.Get(time), dtype=np.float32
                ).reshape(uv_shape)
            elif uv_layout == "corner" and self._corner_uv:
                texture_coordinates = np.asarray(
                    self._corner_uv.Get(time), dtype=np.float32
                ).reshape(uv_shape)

            attributes = {}
            for record in descriptor.get("attributes", ()):
                stream = self._prim.GetAttribute(record["stream"])
                if not stream:
                    raise DecodeError(
                        f"USD frame {index} is missing custom stream {record['stream']}"
                    )
                dtype = {
                    "float": np.float32,
                    "int": np.int32,
                    "bool": np.bool_,
                }[record["kind"]]
                attributes[record["name"]] = np.asarray(
                    stream.Get(time), dtype=dtype
                ).reshape(tuple(record["shape"]))

            frame_index = (
                int(self._frame_index.Get(time))
                if self._frame_index
                else index
            )
            timestamp = (
                float(self._timestamp.Get(time))
                if self._timestamp
                else self.timestamps[index]
            )
            metadata = descriptor.get("metadata", {})
            if not self._native:
                metadata = {"time_code": self._times[index]}
                metadata.update({
                    name: attribute.Get(time)
                    for name, attribute in self._legacy_streams.items()
                })
            return Frame(
                frame_index,
                timestamp,
                TriangleMesh(
                    positions,
                    triangles,
                    colors=colors,
                    normals=normals,
                    texture_coordinates=texture_coordinates,
                    attributes=attributes,
                ),
                metadata,
            )
        except (DecodeError, IndexError):
            raise
        except Exception as error:
            raise DecodeError(
                f"Could not decode USD frame {index} from {self.path}: "
                f"{type(error).__name__}: {error}"
            ) from error

    def close(self) -> None:
        self._stage = None
        self._prim = None


def open_usd_sequence(
    source: str | os.PathLike[str],
    *,
    fps: float | None = None,
    options: Mapping[str, object] | None = None,
) -> Sequence:
    values = dict(options or {})
    unknown = set(values) - {"prim_path"}
    if unknown:
        raise UnsupportedFeatureError(
            f"Unknown OpenUSD reader options: {', '.join(sorted(unknown))}"
        )
    return Sequence(UsdSequenceProvider(source, fps=fps, **values))


def inspect_usd_sequence(source: str | os.PathLike[str]):
    provider = UsdSequenceProvider(source)
    try:
        return {
            "frame_count": provider.frame_count,
            "fps": provider.metadata.get("fps"),
            "topology": provider.topology,
        }
    finally:
        provider.close()


def _preflight(sequence: Sequence) -> tuple[dict[str, Any], list[dict[str, Any]], dict[tuple[str, str], str]]:
    if not len(sequence):
        raise UnsupportedFeatureError("empty sequences cannot be exported")
    manifest = {
        "schema": SCHEMA,
        "metadata": _json_value(sequence.metadata, "sequence"),
        "topology": sequence.topology.value,
        "has_constant_vertex_count": sequence.has_constant_vertex_count,
        "has_vertex_correspondence": sequence.has_vertex_correspondence,
        "allow_nonmonotonic_timestamps": sequence.allow_nonmonotonic_timestamps,
        "frame_count": len(sequence),
    }
    raw_descriptors: list[dict[str, Any]] = []
    schemas: set[tuple[str, str]] = set()
    for ordinal, frame in enumerate(sequence):
        mesh = frame.geometry
        attributes = []
        for name, array in mesh.attributes.items():
            kind = _attribute_kind(array)
            schemas.add((name, kind))
            attributes.append({
                "name": name,
                "kind": kind,
                "shape": list(array.shape),
            })
        uv = mesh.texture_coordinates
        raw_descriptors.append({
            "metadata": _json_value(frame.metadata, f"frame {ordinal}"),
            "color_channels": 0 if mesh.colors is None else int(mesh.colors.shape[1]),
            "normals": mesh.normals is not None,
            "uv_layout": None if uv is None else ("vertex" if uv.ndim == 2 else "corner"),
            "uv_shape": [] if uv is None else list(uv.shape),
            "attributes": attributes,
        })
    streams = {
        schema: f"open4d:attribute{index:04d}:{schema[1]}"
        for index, schema in enumerate(sorted(schemas))
    }
    for descriptor in raw_descriptors:
        for record in descriptor["attributes"]:
            record["stream"] = streams[(record["name"], record["kind"])]
    return manifest, raw_descriptors, streams


def _author_stage(
    path: Path,
    sequence: Sequence,
    manifest: dict[str, Any],
    descriptors: list[dict[str, Any]],
    streams: dict[tuple[str, str], str],
    *,
    fps: float,
    up_axis: str,
) -> None:
    Sdf, Usd, UsdGeom, Vt = _pxr()
    stage = Usd.Stage.CreateNew(str(path))
    if stage is None:
        raise EncodeError(f"USD could not create {path}")
    stage.SetInterpolationType(Usd.InterpolationTypeHeld)
    stage.SetTimeCodesPerSecond(fps)
    stage.SetFramesPerSecond(fps)
    UsdGeom.SetStageUpAxis(
        stage, UsdGeom.Tokens.y if up_axis == "y" else UsdGeom.Tokens.z
    )
    schema = UsdGeom.Mesh.Define(stage, PRIM_PATH)
    prim = schema.GetPrim()
    points = schema.CreatePointsAttr()
    counts = schema.CreateFaceVertexCountsAttr()
    indices = schema.CreateFaceVertexIndicesAttr()
    extent = schema.CreateExtentAttr()
    colors = UsdGeom.PrimvarsAPI(prim).CreatePrimvar(
        "displayColor", Sdf.ValueTypeNames.Color3fArray, UsdGeom.Tokens.vertex
    )
    opacity = UsdGeom.PrimvarsAPI(prim).CreatePrimvar(
        "displayOpacity", Sdf.ValueTypeNames.FloatArray, UsdGeom.Tokens.vertex
    )
    normals = schema.CreateNormalsAttr()
    schema.SetNormalsInterpolation(UsdGeom.Tokens.vertex)
    vertex_uv = prim.CreateAttribute("open4d:vertexUV", Sdf.ValueTypeNames.Float2Array)
    corner_uv = prim.CreateAttribute("open4d:cornerUV", Sdf.ValueTypeNames.Float2Array)
    frame_index = prim.CreateAttribute("open4d:frameIndex", Sdf.ValueTypeNames.Int)
    timestamp = prim.CreateAttribute("open4d:timestamp", Sdf.ValueTypeNames.Double)
    descriptor_attr = prim.CreateAttribute("open4d:frameDescriptor", Sdf.ValueTypeNames.String)
    custom_attrs = {}
    for (_name, kind), stream in streams.items():
        type_name = {
            "float": Sdf.ValueTypeNames.FloatArray,
            "int": Sdf.ValueTypeNames.IntArray,
            "bool": Sdf.ValueTypeNames.BoolArray,
        }[kind]
        custom_attrs[stream] = prim.CreateAttribute(stream, type_name)

    previous_triangles = None
    key_frames = []
    for ordinal, frame in enumerate(sequence):
        time = Usd.TimeCode(ordinal)
        mesh = frame.geometry
        points.Set(Vt.Vec3fArray.FromNumpy(np.ascontiguousarray(mesh.positions)), time)
        if len(mesh.positions):
            extent.Set(
                Vt.Vec3fArray.FromNumpy(
                    np.asarray(
                        [mesh.positions.min(axis=0), mesh.positions.max(axis=0)],
                        dtype=np.float32,
                    )
                ),
                time,
            )
        triangles = np.asarray(mesh.triangles, dtype=np.int32)
        is_key = previous_triangles is None or not np.array_equal(previous_triangles, triangles)
        if is_key:
            counts.Set(Vt.IntArray([3] * len(triangles)), time)
            indices.Set(Vt.IntArray(triangles.reshape(-1).tolist()), time)
            key_frames.append(ordinal)
        previous_triangles = triangles.copy()

        descriptor = descriptors[ordinal]
        if mesh.colors is not None:
            colors.Set(
                Vt.Vec3fArray.FromNumpy(
                    np.ascontiguousarray(mesh.colors[:, :3], dtype=np.float32)
                ),
                time,
            )
            if mesh.colors.shape[1] == 4:
                opacity.Set(
                    Vt.FloatArray.FromNumpy(
                        np.ascontiguousarray(mesh.colors[:, 3], dtype=np.float32)
                    ),
                    time,
                )
            else:
                opacity.Set(Vt.FloatArray(), time)
        else:
            colors.Set(Vt.Vec3fArray(), time)
            opacity.Set(Vt.FloatArray(), time)
        if mesh.normals is not None:
            normals.Set(
                Vt.Vec3fArray.FromNumpy(np.ascontiguousarray(mesh.normals)), time
            )
        else:
            normals.Set(Vt.Vec3fArray(), time)
        uv = mesh.texture_coordinates
        if uv is not None:
            target = vertex_uv if uv.ndim == 2 else corner_uv
            target.Set(
                Vt.Vec2fArray.FromNumpy(
                    np.ascontiguousarray(uv.reshape(-1, 2), dtype=np.float32)
                ),
                time,
            )
        for record in descriptor["attributes"]:
            value = mesh.attributes[record["name"]].reshape(-1)
            array_type = {
                "float": Vt.FloatArray,
                "int": Vt.IntArray,
                "bool": Vt.BoolArray,
            }[record["kind"]]
            custom_attrs[record["stream"]].Set(
                array_type.FromNumpy(np.ascontiguousarray(value)), time
            )
        frame_index.Set(int(frame.frame_index), time)
        timestamp.Set(float(frame.timestamp), time)
        descriptor_attr.Set(
            json.dumps(descriptor, separators=(",", ":"), sort_keys=True), time
        )

    stage.SetStartTimeCode(0)
    stage.SetEndTimeCode(len(sequence) - 1)
    manifest.update({
        "fps": fps,
        "up_axis": up_axis,
        "key_frame_indices": key_frames,
    })
    stage.GetRootLayer().customLayerData = {
        "open4d": {
            "schema": SCHEMA,
            "manifest": json.dumps(manifest, separators=(",", ":"), sort_keys=True),
        }
    }
    if not stage.GetRootLayer().Save():
        raise EncodeError(f"USD could not save {path}")


def write_usd_sequence(
    sequence: Sequence,
    destination: str | os.PathLike[str],
    *,
    overwrite: bool = False,
    fps: float | None = None,
    up_axis: str | None = None,
) -> Path:
    if not isinstance(sequence, Sequence):
        raise TypeError("sequence must be an open4d.Sequence")
    destination = Path(destination).absolute()
    if destination.suffix.lower() not in USD_SUFFIXES:
        raise ValueError("OpenUSD destination must end in .usd, .usda, .usdc, or .usdz")
    if destination.exists() and not overwrite:
        raise FileExistsError(f"destination already exists: {destination}")
    selected_up = up_axis or str(sequence.metadata.get("up_axis", "z")).lower()
    if selected_up not in {"y", "z"}:
        raise ValueError("up_axis must be 'y' or 'z'")
    selected_fps = _positive_fps(fps, sequence)
    manifest, descriptors, streams = _preflight(sequence)
    destination.parent.mkdir(parents=True, exist_ok=True)
    handle, temporary_name = tempfile.mkstemp(
        prefix=f".{destination.stem}.", suffix=destination.suffix,
        dir=destination.parent,
    )
    os.close(handle)
    temporary = Path(temporary_name)
    temporary.unlink()
    try:
        if destination.suffix.lower() == ".usdz":
            _Sdf, _Usd, _UsdGeom, _Vt = _pxr()
            from pxr import UsdUtils
            with tempfile.TemporaryDirectory() as directory:
                inner = Path(directory) / f"{destination.stem}.usdc"
                _author_stage(
                    inner, sequence, manifest, descriptors, streams,
                    fps=selected_fps, up_axis=selected_up,
                )
                if not UsdUtils.CreateNewUsdzPackage(str(inner), str(temporary)):
                    raise EncodeError(f"USD could not package {destination}")
        else:
            _author_stage(
                temporary, sequence, manifest, descriptors, streams,
                fps=selected_fps, up_axis=selected_up,
            )
        temporary.replace(destination)
        return destination
    except Exception as error:
        temporary.unlink(missing_ok=True)
        if isinstance(error, (EncodeError, UnsupportedFeatureError, MissingDependencyError, TypeError, ValueError)):
            raise
        raise EncodeError(
            f"Could not encode {destination}: {type(error).__name__}: {error}"
        ) from error
