"""OpenUSD as Open4D's sequence container, plus USD frame reading.

USD is time-sampled by design, so one file holds a whole 4D sequence: the frames
are time samples on a `points` attribute. That makes it a container rather than
just a mesh format, and `.usdc` — USD's binary crate format — stores those
samples compressed.

Needs the usd extra, which provides the `pxr` bindings:

    python -m pip install -e '.[usd]'

## What the container holds

Stage metadata:

- `timeCodesPerSecond` / `framesPerSecond` — the frame rate
- `startTimeCode` / `endTimeCode` — the frame range
- up axis

`customLayerData["open4d"]` — the sequence-level record:

- `version`, `generator`, `created`
- `source`, `source_format` — where the frames came from
- `frame_count`, `fps`, `duration_seconds`
- `key_frame_indices` — frames that do not share the previous frame's
  connectivity, so each one starts a new run of constant topology. Frame 0 is
  always a key frame.

Geometry streams on `/Open4D/Sequence`, one sample per frame:

- `points` — vertex positions
- `faceVertexCounts` / `faceVertexIndices` — connectivity, written once when it
  never changes and time-sampled when it does
- `extent` — per-frame bounds
- `primvars:displayColor` — per-vertex colors, when the source has them

Per-frame streams, as custom time-sampled attributes on the same prim:

- `open4d:frameIndex`, `open4d:timestamp`, `open4d:keyFrame`
- `open4d:vertexCount`, `open4d:triangleCount`

A mesh sequence is written as a `UsdGeom.Mesh`; a point-cloud sequence (frames
with no faces) as a `UsdGeom.Points`. Both are read back the same way.
"""

from __future__ import annotations

import datetime as _datetime
import math
from numbers import Real
import sys
from pathlib import Path
from typing import Any, Iterable

import numpy as np

# Import first: this puts the repository on sys.path for uninstalled clones.
import _common  # noqa: F401

from open4d import Frame, Sequence, TopologyMode, TriangleMesh

SUFFIXES = (".usd", ".usda", ".usdc", ".usdz")

CONTAINER_VERSION = 1
PRIM_PATH = "/Open4D/Sequence"

# Custom per-frame streams: attribute suffix -> USD type name attribute.
_STREAM_TYPES = {
    "frameIndex": "Int",
    "timestamp": "Double",
    "keyFrame": "Bool",
    "vertexCount": "Int",
    "triangleCount": "Int",
}

_NO_TRIANGLES = np.empty((0, 3), dtype=np.uint32)


def _positive_fps(value: float) -> float:
    if not isinstance(value, Real) or isinstance(value, bool):
        raise TypeError("fps must be a real number")
    result = float(value)
    if not math.isfinite(result) or result <= 0:
        raise ValueError("fps must be finite and greater than zero")
    return result


def _pxr() -> tuple[Any, Any, Any, Any]:
    """Import the USD bindings, or exit with the pip command that supplies them."""
    try:
        from pxr import Sdf, Usd, UsdGeom, Vt
    except ImportError:
        sys.exit(
            "OpenUSD support needs the 'pxr' bindings, which are not installed.\n"
            "Install them with: python -m pip install -e '.[usd]'"
        )
    return Sdf, Usd, UsdGeom, Vt


# ----------------------------
# Reading
# ----------------------------
def _open_stage(path: Path, Usd: Any) -> Any:
    stage = Usd.Stage.Open(str(path))
    if stage is None:
        raise ValueError(f"USD could not open {path}")
    return stage


def _geometry_prim(stage: Any, UsdGeom: Any) -> Any:
    """Return the container prim, or the first geometry prim on the stage.

    Containers written here live at a known path. Anything else — a DCC export,
    a scene with cameras and lights and several meshes — falls back to the first
    mesh or point-based prim, and the reader reports which one it picked.
    """
    prim = stage.GetPrimAtPath(PRIM_PATH)
    if prim and prim.IsValid():
        return prim
    for candidate in stage.Traverse():
        if candidate.IsA(UsdGeom.Mesh) or candidate.IsA(UsdGeom.Points):
            return candidate
    raise ValueError("no UsdGeom.Mesh or UsdGeom.Points prim found on the stage")


def _triangulate(counts: np.ndarray, indices: np.ndarray) -> np.ndarray:
    """Fan-triangulate USD's faceVertexCounts/faceVertexIndices pair."""
    if len(counts) == 0:
        return _NO_TRIANGLES
    if np.all(counts == 3):
        return indices.reshape(-1, 3).astype(np.uint32)

    triangles: list[tuple[int, int, int]] = []
    offset = 0
    for count in counts.tolist():
        face = indices[offset : offset + count]
        for corner in range(1, count - 1):
            triangles.append((face[0], face[corner], face[corner + 1]))
        offset += count
    return np.array(triangles, dtype=np.uint32).reshape(-1, 3)


def _read_colors(prim: Any, vertex_count: int, time: Any, UsdGeom: Any):
    """Return per-vertex colors from `displayColor`, if there is one per vertex."""
    primvar = UsdGeom.PrimvarsAPI(prim).GetPrimvar("displayColor")
    if not primvar:
        return None
    value = primvar.Get(time)
    if value is None:
        return None
    colors = np.asarray(value, dtype=np.float32)
    # USD also allows a single constant color; only per-vertex arrays map onto
    # TriangleMesh.colors.
    if colors.ndim != 2 or len(colors) != vertex_count:
        return None
    return np.clip(colors, 0.0, 1.0)


class UsdSequenceProvider:
    """Lazy `FrameProvider` over the time samples of one USD geometry prim."""

    def __init__(self, path: Path | str, fps: float | None = None) -> None:
        self.path = Path(path)
        self._Sdf, self._Usd, self._UsdGeom, _Vt = _pxr()
        self._stage = _open_stage(self.path, self._Usd)
        self._prim = _geometry_prim(self._stage, self._UsdGeom)

        self._points_attr = self._UsdGeom.PointBased(self._prim).GetPointsAttr()
        if not self._points_attr:
            raise ValueError(f"{self.path} geometry prim has no points attribute")

        mesh = (
            self._UsdGeom.Mesh(self._prim)
            if self._prim.IsA(self._UsdGeom.Mesh)
            else None
        )
        self._counts_attr = mesh.GetFaceVertexCountsAttr() if mesh else None
        self._indices_attr = mesh.GetFaceVertexIndicesAttr() if mesh else None

        # The time samples on `points` are the frames. A prim with no samples is
        # a static mesh, which is a one-frame sequence.
        samples = list(self._points_attr.GetTimeSamples())
        self._times: list[float | None] = samples or [None]

        # Per-frame streams are optional: a DCC-exported USD will not have them.
        self._streams = {
            name: self._prim.GetAttribute(f"open4d:{name}")
            for name in _STREAM_TYPES
        }
        self._streams = {
            name: attribute
            for name, attribute in self._streams.items()
            if attribute and attribute.GetNumTimeSamples() > 0
        }

        stage_fps = self._stage.GetTimeCodesPerSecond() or 24.0
        self.fps = _positive_fps(stage_fps if fps is None else fps)

        container = dict(
            (self._stage.GetRootLayer().customLayerData or {}).get("open4d", {})
        )
        if "key_frame_indices" in container:
            container["key_frame_indices"] = [
                int(value) for value in container["key_frame_indices"]
            ]
        self.metadata = {
            "name": self.path.stem,
            "source": str(self.path),
            "format": self.path.suffix.lower(),
            "fps": self.fps,
            "prim": str(self._prim.GetPath()),
            "prim_type": str(self._prim.GetTypeName()),
            "up_axis": str(self._UsdGeom.GetStageUpAxis(self._stage)).lower(),
            "time_samples": len(samples),
            "per_frame_streams": sorted(self._streams),
            **container,
        }

        # Connectivity that is not time-sampled is fixed for the whole sequence,
        # which is exactly what TopologyMode.FIXED means.
        if self._counts_attr is None:
            self.topology = TopologyMode.CHANGING  # point cloud
        elif self._indices_attr and self._indices_attr.GetNumTimeSamples() > 0:
            self.topology = TopologyMode.CHANGING
        else:
            self.topology = TopologyMode.FIXED
        self.has_constant_vertex_count = None
        self.has_vertex_correspondence = None

    @property
    def frame_count(self) -> int:
        return len(self._times)

    def _time_code(self, time: float | None) -> Any:
        return (
            self._Usd.TimeCode.Default()
            if time is None
            else self._Usd.TimeCode(time)
        )

    @property
    def timestamps(self) -> tuple[float, ...]:
        stream = self._streams.get("timestamp")
        if stream is not None:
            return tuple(
                float(stream.Get(self._time_code(time))) for time in self._times
            )
        # No stored stream: USD time codes are in frames, so divide by the rate.
        return tuple(
            index / self.fps if time is None else float(time) / self.fps
            for index, time in enumerate(self._times)
        )

    def get_frame(self, index: int) -> Frame:
        if index < 0 or index >= self.frame_count:
            raise IndexError("frame index out of range")
        time = self._time_code(self._times[index])

        positions = np.asarray(self._points_attr.Get(time), dtype=np.float32)
        if positions.ndim != 2 or positions.shape[1] != 3:
            raise ValueError(
                f"{self.path} frame {index} points have shape {positions.shape}"
            )

        triangles = _NO_TRIANGLES
        if self._counts_attr is not None and self._indices_attr is not None:
            counts = self._counts_attr.Get(time)
            indices = self._indices_attr.Get(time)
            if counts is not None and indices is not None:
                triangles = _triangulate(
                    np.asarray(counts, dtype=np.int64),
                    np.asarray(indices, dtype=np.int64),
                )

        metadata: dict[str, Any] = {"time_code": self._times[index]}
        for name, attribute in self._streams.items():
            metadata[name] = attribute.Get(time)

        return Frame(
            frame_index=index,
            timestamp=self.timestamps[index],
            geometry=TriangleMesh(
                positions=positions,
                triangles=triangles,
                colors=_read_colors(
                    self._prim, len(positions), time, self._UsdGeom
                ),
            ),
            metadata=metadata,
        )


def open_usd_sequence(path: Path, fps: float | None = None) -> Sequence:
    """Open a time-sampled USD file as a core `Sequence`.

    `fps` overrides the stage's own `timeCodesPerSecond`.
    """
    return Sequence(UsdSequenceProvider(path, fps=fps))


def read_usd_frame(path: Path) -> tuple[np.ndarray, np.ndarray, np.ndarray | None]:
    """Read a single frame from a USD file, for folders of per-frame USD.

    Takes the first time sample, or the default value for a static prim.
    """
    mesh = UsdSequenceProvider(path).get_frame(0).geometry
    return mesh.positions, mesh.triangles, mesh.colors


def read_container_metadata(path: Path) -> dict[str, Any]:
    """Read `customLayerData["open4d"]` without composing a stage.

    Opening the layer alone skips stage composition, and avoids holding a layer
    whose owning stage has already been collected.
    """
    Sdf, _Usd, _UsdGeom, _Vt = _pxr()
    layer = Sdf.Layer.FindOrOpen(str(path))
    if layer is None:
        raise ValueError(f"USD could not open {path}")
    record = dict((layer.customLayerData or {}).get("open4d", {}))
    if "key_frame_indices" in record:
        record["key_frame_indices"] = [
            int(value) for value in record["key_frame_indices"]
        ]
    return record


# ----------------------------
# Writing
# ----------------------------
def write_usd_container(
    path: Path,
    frames: Iterable[Frame],
    fps: float = 30.0,
    up_axis: str = "z",
    source: str | None = None,
    source_format: str | None = None,
    generator: str | None = None,
) -> Path:
    """Pack frames into one OpenUSD container.

    Writes the geometry streams, the per-frame streams, and the sequence-level
    record described in this module's docstring. Use a `.usdc` extension for
    USD's compressed binary crate format, `.usda` to get readable text, or
    `.usdz` for a single-file package.

    Connectivity is written once when every frame shares it, and time-sampled
    only where it changes. The frames where it changes are the key frames.
    """
    fps = _positive_fps(fps)
    if not isinstance(up_axis, str) or up_axis.lower() not in {"y", "z"}:
        raise ValueError("up_axis must be 'y' or 'z' for an OpenUSD stage")
    up_axis = up_axis.lower()

    # USD prim schemas and primvars must be chosen before samples are authored.
    # Reiterable sequences are scanned without retaining their geometry; only a
    # one-shot iterator needs materialization so it can survive the write pass.
    iterator = iter(frames)
    if iterator is frames:
        frames = tuple(iterator)

    frame_count = 0
    has_triangles = False
    has_colors = False
    for index, frame in enumerate(frames):
        frame_count = index + 1
        has_triangles = has_triangles or len(frame.geometry.triangles) > 0
        has_colors = has_colors or frame.geometry.colors is not None
        if len(frame.geometry.positions) == 0:
            raise ValueError(
                f"frame {index} contains no positions; "
                "USD containers require non-empty geometry"
            )
    if frame_count == 0:
        raise ValueError("cannot write a container with no frames")

    Sdf, Usd, UsdGeom, Vt = _pxr()
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        path.unlink()

    if path.suffix.lower() == ".usdz":
        # A .usdz is a zip package, and Stage.CreateNew refuses to author one.
        # Write the crate layer first, then let UsdUtils package it.
        import tempfile

        from pxr import UsdUtils

        with tempfile.TemporaryDirectory() as scratch:
            inner = Path(scratch) / f"{path.stem}.usdc"
            write_usd_container(
                inner,
                frames,
                fps=fps,
                up_axis=up_axis,
                source=source,
                source_format=source_format,
                generator=generator,
            )
            if not UsdUtils.CreateNewUsdzPackage(str(inner), str(path)):
                raise OSError(f"USD could not package {path}")
        return path

    stage = Usd.Stage.CreateNew(str(path))
    UsdGeom.SetStageUpAxis(
        stage, UsdGeom.Tokens.z if up_axis == "z" else UsdGeom.Tokens.y
    )
    stage.SetTimeCodesPerSecond(fps)
    stage.SetFramesPerSecond(fps)

    if has_triangles:
        schema = UsdGeom.Mesh.Define(stage, PRIM_PATH)
        counts_attr = schema.CreateFaceVertexCountsAttr()
        indices_attr = schema.CreateFaceVertexIndicesAttr()
    else:
        schema = UsdGeom.Points.Define(stage, PRIM_PATH)
        counts_attr = indices_attr = None
    geometry_prim = schema.GetPrim()
    points_attr = schema.CreatePointsAttr()
    extent_attr = schema.CreateExtentAttr()
    color_primvar = None
    if has_colors:
        color_primvar = UsdGeom.PrimvarsAPI(geometry_prim).CreatePrimvar(
            "displayColor",
            Sdf.ValueTypeNames.Color3fArray,
            UsdGeom.Tokens.vertex,
        )
    streams: dict[str, Any] = {}
    for name, type_name in _STREAM_TYPES.items():
        streams[name] = geometry_prim.CreateAttribute(
            f"open4d:{name}",
            getattr(Sdf.ValueTypeNames, type_name),
            custom=True,
        )

    previous_triangles: np.ndarray | None = None
    first_triangles: np.ndarray | None = None
    key_frames: list[int] = []
    timestamps: list[float] = []
    count = 0

    for index, frame in enumerate(frames):
        mesh = frame.geometry
        positions = np.asarray(mesh.positions, dtype=np.float32)
        triangles = np.asarray(mesh.triangles, dtype=np.int32)
        time = Usd.TimeCode(index)

        points_attr.Set(Vt.Vec3fArray.FromNumpy(positions), time)
        extent_attr.Set(
            Vt.Vec3fArray.FromNumpy(
                np.stack([positions.min(axis=0), positions.max(axis=0)])
            ),
            time,
        )

        if color_primvar is not None:
            if mesh.colors is None:
                colors = np.empty((0, 3), dtype=np.float32)
            else:
                colors = np.asarray(mesh.colors)
                if np.issubdtype(colors.dtype, np.integer):
                    colors = colors / 255.0
                colors = np.ascontiguousarray(colors[:, :3], dtype=np.float32)
            color_primvar.Set(Vt.Vec3fArray.FromNumpy(colors), time)

        is_key_frame = previous_triangles is None or not np.array_equal(
            previous_triangles, triangles
        )
        if counts_attr is not None and is_key_frame:
            # Every key frame gets a time sample, frame 0 included. Writing
            # frame 0 as an unvarying default instead would be wrong: once an
            # attribute has any time samples, USD resolves a query before the
            # first sample to that sample, so frame 0 would read frame 1's
            # connectivity. Sequences that never change topology are collapsed
            # back to a single unvarying value after the loop.
            counts_attr.Set(Vt.IntArray([3] * len(triangles)), time)
            indices_attr.Set(Vt.IntArray(triangles.ravel().tolist()), time)
            if previous_triangles is None:
                first_triangles = triangles

        timestamp = float(frame.timestamp)
        streams["frameIndex"].Set(int(frame.frame_index), time)
        streams["timestamp"].Set(timestamp, time)
        streams["keyFrame"].Set(bool(is_key_frame), time)
        streams["vertexCount"].Set(int(len(positions)), time)
        streams["triangleCount"].Set(int(len(triangles)), time)

        if is_key_frame:
            key_frames.append(index)
        previous_triangles = triangles
        timestamps.append(timestamp)
        count = index + 1

    # Only frame 0 is a key frame, so connectivity never changed: replace the
    # single time sample with an unvarying value, which USD stores far more
    # compactly and which reads as TopologyMode.FIXED.
    if counts_attr is not None and len(key_frames) == 1:
        counts_attr.Clear()
        indices_attr.Clear()
        counts_attr.Set(Vt.IntArray([3] * len(first_triangles)))
        indices_attr.Set(Vt.IntArray(first_triangles.ravel().tolist()))

    stage.SetStartTimeCode(0.0)
    stage.SetEndTimeCode(float(count - 1))

    layer = stage.GetRootLayer()
    layer.customLayerData = {
        "open4d": {
            "version": CONTAINER_VERSION,
            "generator": generator or "examples/formats_usd.py",
            "created": _datetime.datetime.now(
                _datetime.timezone.utc
            ).isoformat(timespec="seconds"),
            "source": source or "",
            "source_format": source_format or "",
            "frame_count": int(count),
            "fps": float(fps),
            "duration_seconds": float(timestamps[-1] - timestamps[0]),
            "key_frame_indices": Vt.IntArray(key_frames),
        }
    }
    layer.Save()
    return path
