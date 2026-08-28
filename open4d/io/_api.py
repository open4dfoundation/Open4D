"""Public sequence loading over containers and per-frame mesh storage."""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass
import json
import math
from numbers import Real
import operator
import os
from pathlib import Path
import re
import shutil
import tempfile
from types import MappingProxyType
from typing import Callable

import numpy as np

from open4d.core import Frame, Sequence, TopologyMode, TriangleMesh

from . import _mesh, _usd
from ._errors import (
    AmbiguousFormatError,
    DecodeError,
    EncodeError,
    MissingDependencyError,
    SequenceIOError,
    SourceNotFoundError,
    UnsupportedFeatureError,
    UnsupportedFormatError,
)

_DEFAULT_FPS = 30.0
_MANIFEST_NAME = "open4d.sequence.json"
_MANIFEST_SCHEMA = "open4d.sequence-manifest/v1"

FrameReader = Callable[[Path], tuple[np.ndarray, np.ndarray, np.ndarray | None]]
_FRAME_READERS: dict[str, FrameReader] = {
    ".obj": _mesh.read_obj,
    ".ply": _mesh.read_ply,
    **{suffix: _mesh.read_with_trimesh for suffix in _mesh.TRIMESH_SUFFIXES},
}
_FORMAT_IDS = {suffix.removeprefix("."): suffix for suffix in _FRAME_READERS}
_FORMAT_DEPENDENCIES = {
    suffix: (None if suffix in _mesh.BUILTIN_SUFFIXES else "tools")
    for suffix in _FRAME_READERS
}
_OUTPUT_FIELDS = {
    ".obj": frozenset(("positions", "triangles")),
    ".ply": frozenset(("positions", "triangles", "colors")),
    ".off": frozenset(("positions", "triangles", "colors")),
    ".stl": frozenset(("positions", "triangles")),
    ".glb": frozenset(("positions", "triangles", "colors")),
    ".gltf": frozenset(("positions", "triangles", "colors")),
}


@dataclass(frozen=True)
class FormatInfo:
    """A storage format understood by the installed Open4D package."""

    id: str
    suffixes: tuple[str, ...]
    dependency_extra: str | None = None
    readable: bool = True
    writable: bool = True


@dataclass(frozen=True)
class SequenceInfo:
    """Metadata available without decoding sequence geometry."""

    source: Path
    storage: str
    format: str
    frame_count: int
    geometry_kind: str
    fps: float | None
    timing_source: str
    topology: TopologyMode


def available_formats() -> tuple[FormatInfo, ...]:
    """Return the geometry and sequence formats supported by the public loader."""
    mesh_formats = tuple(
        FormatInfo(identifier, (suffix,), _FORMAT_DEPENDENCIES[suffix])
        for identifier, suffix in sorted(_FORMAT_IDS.items())
    )
    return mesh_formats + (
        FormatInfo("usd", _usd.USD_SUFFIXES, "usd"),
    )


def _supported_formats() -> str:
    """Return supported formats as concise CLI help text."""
    lines = ["per-frame mesh files (one file or a directory of frames):"]
    for info in available_formats():
        if info.id == "usd":
            continue
        extra = (
            f"  needs the [{info.dependency_extra}] extra"
            if info.dependency_extra
            else ""
        )
        lines.append(f"  {info.suffixes[0]:<7}{extra}".rstrip())
    lines.append("sequence containers:")
    lines.append("  .usd/.usda/.usdc/.usdz  needs the [usd] extra")
    return "\n".join(lines)


def _is_usd_request(path: Path, format: str | None) -> bool:
    if path.suffix.lower() in _usd.USD_SUFFIXES and not path.is_dir():
        if format is None:
            return True
        if not isinstance(format, str) or not format.strip():
            raise TypeError("format must be a non-empty string or None")
        requested = format.strip().lower().lstrip(".")
        if requested not in {"usd", path.suffix.lower().lstrip(".")}:
            raise UnsupportedFormatError(
                f"requested format {format!r} does not match {path.suffix!r}"
            )
        return True
    if isinstance(format, str) and format.strip().lower().lstrip(".") in {
        "usd", "usda", "usdc", "usdz"
    }:
        return True
    return False


def _suffix_for(format: str | None) -> str | None:
    if format is None:
        return None
    if not isinstance(format, str) or not format.strip():
        raise TypeError("format must be a non-empty string or None")
    identifier = format.strip().lower().lstrip(".")
    try:
        return _FORMAT_IDS[identifier]
    except KeyError:
        raise UnsupportedFormatError(
            f"Unsupported format {format!r}.\n{_supported_formats()}"
        ) from None


def _fps(value: float | None) -> float:
    if value is None:
        return _DEFAULT_FPS
    if not isinstance(value, Real) or isinstance(value, bool):
        raise TypeError("fps must be a real number or None")
    result = float(value)
    if not math.isfinite(result) or result <= 0:
        raise ValueError("fps must be finite and greater than zero")
    return result


def _frame_sort_key(path: Path) -> tuple[float, str]:
    numbers = re.findall(r"\d+", path.stem)
    return (int(numbers[-1]) if numbers else math.inf, path.name)


def _source_index(path: Path, ordinal: int) -> int:
    numbers = re.findall(r"\d+", path.stem)
    return int(numbers[-1]) if numbers else ordinal


def _frame_files(directory: Path, suffix: str | None) -> tuple[Path, ...]:
    by_suffix: dict[str, list[Path]] = {}
    for entry in directory.iterdir():
        entry_suffix = entry.suffix.lower()
        if entry.is_file() and entry_suffix in _FRAME_READERS:
            by_suffix.setdefault(entry_suffix, []).append(entry)

    if suffix is not None:
        files = by_suffix.get(suffix, [])
        if not files:
            raise UnsupportedFormatError(
                f"{directory} contains no {suffix} frame files"
            )
    elif not by_suffix:
        raise UnsupportedFormatError(
            f"{directory} contains no supported frame files.\n{_supported_formats()}"
        )
    elif len(by_suffix) > 1:
        formats = ", ".join(sorted(by_suffix))
        raise AmbiguousFormatError(
            f"{directory} mixes frame formats ({formats}); pass format= to "
            "select one explicitly"
        )
    else:
        files = next(iter(by_suffix.values()))
    return tuple(sorted(files, key=_frame_sort_key))


def _json_value(value, name: str):
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
        return {key: _json_value(item, f"{name}.{key}") for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [_json_value(item, name) for item in value]
    raise EncodeError(f"{name} metadata value {type(value).__name__} is not serializable")


def _read_manifest(
    directory: Path, selected_suffix: str | None
) -> tuple[dict, tuple[Path, ...], str] | None:
    manifest_path = directory / _MANIFEST_NAME
    if not manifest_path.is_file():
        return None
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        if not isinstance(manifest, Mapping):
            raise TypeError("manifest root must be an object")
        if manifest.get("schema") != _MANIFEST_SCHEMA:
            raise ValueError(f"unsupported schema {manifest.get('schema')!r}")
        suffix = _suffix_for(manifest["format"])
        if selected_suffix is not None and selected_suffix != suffix:
            raise UnsupportedFormatError(
                f"manifest format {suffix!r} does not match requested format "
                f"{selected_suffix!r}"
            )
        records = manifest["frames"]
        if not isinstance(records, list):
            raise TypeError("frames must be a list")
        files = []
        root = directory.resolve()
        for ordinal, record in enumerate(records):
            if not isinstance(record, dict):
                raise TypeError(f"frame {ordinal} must be an object")
            index, timestamp, uri = record["index"], record["timestamp"], record["uri"]
            if not isinstance(index, int) or isinstance(index, bool) or index < 0:
                raise ValueError(f"frame {ordinal} has an invalid index")
            if not isinstance(timestamp, Real) or isinstance(timestamp, bool) or not math.isfinite(timestamp):
                raise ValueError(f"frame {ordinal} has an invalid timestamp")
            if not isinstance(record.get("metadata", {}), dict):
                raise TypeError(f"frame {ordinal} metadata must be an object")
            if not isinstance(uri, str) or not uri or Path(uri).is_absolute():
                raise ValueError(f"frame {ordinal} has an invalid uri")
            frame_path = (directory / uri).resolve()
            if root not in frame_path.parents or frame_path.suffix.lower() != suffix:
                raise ValueError(f"frame {ordinal} uri escapes the source or changes format")
            if not frame_path.is_file():
                raise FileNotFoundError(uri)
            files.append(frame_path)
        if not isinstance(manifest.get("metadata", {}), dict):
            raise TypeError("metadata must be an object")
        manifest["topology"] = TopologyMode(manifest.get("topology", "unknown"))
        for name in ("has_constant_vertex_count", "has_vertex_correspondence"):
            if manifest.get(name) is not None and not isinstance(manifest[name], bool):
                raise TypeError(f"{name} must be bool or null")
        if not isinstance(manifest.get("allow_nonmonotonic_timestamps", False), bool):
            raise TypeError("allow_nonmonotonic_timestamps must be bool")
        return manifest, tuple(files), suffix
    except UnsupportedFormatError:
        raise
    except (KeyError, TypeError, ValueError, OSError, json.JSONDecodeError) as error:
        raise DecodeError(f"Invalid sequence manifest {manifest_path}: {error}") from error


def _read_geometry(path: Path, suffix: str | None = None) -> TriangleMesh:
    suffix = suffix or path.suffix.lower()
    try:
        try:
            positions, triangles, colors = _FRAME_READERS[suffix](path)
        except _mesh.UnsupportedPlyVariant:
            positions, triangles, colors = _mesh.read_with_trimesh(path)
        return TriangleMesh(positions=positions, triangles=triangles, colors=colors)
    except ImportError as error:
        raise MissingDependencyError(str(error)) from error
    except Exception as error:
        raise DecodeError(
            f"Could not decode {path}: {type(error).__name__}: {error}"
        ) from error


class _FolderProvider:
    topology = TopologyMode.UNKNOWN
    has_constant_vertex_count = None
    has_vertex_correspondence = None

    def __init__(self, directory: Path, files: tuple[Path, ...], fps: float) -> None:
        self.directory = directory
        self.files = files
        self.fps = fps
        self.metadata = MappingProxyType(
            {
                "name": directory.name,
                "source": str(directory),
                "format": files[0].suffix.lower(),
            }
        )

    @property
    def frame_count(self) -> int:
        return len(self.files)

    @property
    def timestamps(self) -> tuple[float, ...]:
        return tuple(index / self.fps for index in range(self.frame_count))

    def get_frame(self, index: int) -> Frame:
        try:
            ordinal = operator.index(index)
        except TypeError as error:
            raise TypeError("frame index must be an integer") from error
        if ordinal < 0 or ordinal >= self.frame_count:
            raise IndexError("frame index out of range")
        path = self.files[ordinal]
        return Frame(
            frame_index=_source_index(path, ordinal),
            timestamp=ordinal / self.fps,
            geometry=_read_geometry(path),
            metadata={"file": path.name},
        )


class _ManifestFolderProvider:
    def __init__(self, directory: Path, files: tuple[Path, ...], manifest: dict) -> None:
        self.directory = directory
        self.files = files
        self.frames = tuple(manifest["frames"])
        self.metadata = MappingProxyType(dict(manifest.get("metadata", {})))
        self.topology = manifest["topology"]
        self.has_constant_vertex_count = manifest.get("has_constant_vertex_count")
        self.has_vertex_correspondence = manifest.get("has_vertex_correspondence")
        self.allow_nonmonotonic_timestamps = manifest.get(
            "allow_nonmonotonic_timestamps", False
        )

    @property
    def frame_count(self) -> int:
        return len(self.frames)

    @property
    def timestamps(self) -> tuple[float, ...]:
        return tuple(float(record["timestamp"]) for record in self.frames)

    def get_frame(self, index: int) -> Frame:
        if index < 0 or index >= self.frame_count:
            raise IndexError("frame index out of range")
        record = self.frames[index]
        return Frame(
            frame_index=record["index"],
            timestamp=record["timestamp"],
            geometry=_read_geometry(self.files[index]),
            metadata=record.get("metadata", {}),
        )


class _SingleFrameProvider:
    frame_count = 1
    timestamps = (0.0,)
    topology = TopologyMode.FIXED
    has_constant_vertex_count = True
    has_vertex_correspondence = True

    def __init__(self, path: Path, suffix: str) -> None:
        self.path = path
        self.suffix = suffix
        self.metadata = MappingProxyType(
            {"name": path.stem, "source": str(path), "format": suffix}
        )

    def get_frame(self, index: int) -> Frame:
        if index != 0:
            raise IndexError("frame index out of range")
        return Frame(
            frame_index=0,
            timestamp=0.0,
            geometry=_read_geometry(self.path, self.suffix),
            metadata={"file": self.path.name},
        )


def _resolve(
    source: str | os.PathLike[str], format: str | None
) -> tuple[Path, str, tuple[Path, ...], str, dict | None]:
    # Providers outlive this call, so freeze relative sources before lazy reads.
    path = Path(source).absolute()
    if not path.exists():
        raise SourceNotFoundError(f"Sequence source does not exist: {path}")
    suffix = _suffix_for(format)
    if path.is_dir():
        manifested = _read_manifest(path, suffix)
        if manifested is not None:
            manifest, files, manifest_suffix = manifested
            return path, "directory", files, manifest_suffix, manifest
        files = _frame_files(path, suffix)
        return path, "directory", files, files[0].suffix.lower(), None
    if not path.is_file():
        raise UnsupportedFormatError(
            f"Sequence source is not a file or directory: {path}"
        )
    actual_suffix = path.suffix.lower()
    selected = suffix or actual_suffix
    if selected not in _FRAME_READERS:
        raise UnsupportedFormatError(
            f"No reader for {actual_suffix or '<no suffix>'!r}: {path}\n"
            f"{_supported_formats()}"
        )
    return path, "file", (path,), selected, None


def inspect_sequence(
    source: str | os.PathLike[str], *, format: str | None = None
) -> SequenceInfo:
    """Inspect a local mesh file or frame directory without decoding geometry."""
    candidate = Path(source).absolute()
    if not candidate.exists():
        raise SourceNotFoundError(f"Sequence source does not exist: {candidate}")
    if _is_usd_request(candidate, format):
        details = _usd.inspect_usd_sequence(candidate)
        return SequenceInfo(
            source=candidate,
            storage="container",
            format="usd",
            frame_count=details["frame_count"],
            geometry_kind="triangle_mesh",
            fps=details["fps"],
            timing_source="container",
            topology=details["topology"],
        )
    path, storage, files, suffix, manifest = _resolve(source, format)
    is_directory = storage == "directory"
    timestamps = tuple(record["timestamp"] for record in manifest["frames"]) if manifest else ()
    duration = abs(timestamps[-1] - timestamps[0]) if len(timestamps) > 1 else 0
    return SequenceInfo(
        source=path,
        storage=storage,
        format=suffix.lstrip("."),
        frame_count=len(files),
        geometry_kind="triangle_mesh",
        fps=(len(timestamps) - 1) / duration if duration else (
            _DEFAULT_FPS if is_directory and manifest is None else None
        ),
        timing_source="manifest" if manifest else ("default" if is_directory else "static"),
        topology=manifest["topology"] if manifest else (
            TopologyMode.UNKNOWN if is_directory else TopologyMode.FIXED
        ),
    )


def open_sequence(
    source: str | os.PathLike[str],
    *,
    format: str | None = None,
    fps: float | None = None,
    options: Mapping[str, object] | None = None,
) -> Sequence:
    """Open a local mesh file or frame directory as a lazy sequence."""
    if options is not None:
        if not isinstance(options, Mapping):
            raise TypeError("options must be a mapping or None")
    candidate = Path(source).absolute()
    if not candidate.exists():
        raise SourceNotFoundError(f"Sequence source does not exist: {candidate}")
    if _is_usd_request(candidate, format):
        return _usd.open_usd_sequence(
            candidate, fps=fps, options=options
        )
    if options:
        names = ", ".join(sorted(str(name) for name in options))
        raise UnsupportedFeatureError(
            f"This reader has no options; received: {names}"
        )
    path, storage, files, suffix, manifest = _resolve(source, format)
    if storage == "directory":
        if manifest is not None:
            if fps is not None:
                raise UnsupportedFeatureError("fps cannot override manifest timestamps")
            return Sequence(_ManifestFolderProvider(path, files, manifest))
        return Sequence(_FolderProvider(path, files, _fps(fps)))
    if fps is not None:
        _fps(fps)  # Validate consistently even though one static frame has no rate.
    return Sequence(_SingleFrameProvider(path, suffix))


def _write_frame(
    path: Path, frame: Frame, suffix: str, *, allow_lossy: bool = False
) -> Path:
    mesh = frame.geometry
    present = {"positions", "triangles"}
    present.update(
        name for name in ("colors", "normals", "texture_coordinates")
        if getattr(mesh, name) is not None
    )
    present.update(mesh.attributes)
    unsupported = sorted(present - _OUTPUT_FIELDS[suffix])
    if unsupported:
        raise UnsupportedFeatureError(
            f"{suffix} cannot preserve: {', '.join(unsupported)}"
        )
    if mesh.colors is not None and suffix in {".off", ".glb", ".gltf"} and not allow_lossy:
        raise UnsupportedFeatureError(
            f"{suffix} color export through Trimesh is lossy; "
            "pass allow_lossy=True to export it"
        )
    try:
        if suffix == ".obj":
            return _mesh.write_obj(path, mesh.positions, mesh.triangles)
        if suffix == ".ply":
            return _mesh.write_ply(
                path, mesh.positions, mesh.triangles, mesh.colors
            )
        return _mesh.write_with_trimesh(
            path, mesh.positions, mesh.triangles, mesh.colors
        )
    except ImportError as error:
        raise MissingDependencyError(str(error)) from error
    except SequenceIOError:
        raise
    except Exception as error:
        raise EncodeError(
            f"Could not encode {path}: {type(error).__name__}: {error}"
        ) from error


def write_sequence(
    sequence: Sequence,
    destination: str | os.PathLike[str],
    *,
    format: str | None = None,
    overwrite: bool = False,
    allow_lossy: bool = False,
    options: Mapping[str, object] | None = None,
) -> Path:
    """Write a sequence container or explicitly export per-frame mesh files."""
    if not isinstance(sequence, Sequence):
        raise TypeError("sequence must be an open4d.Sequence")
    if not isinstance(allow_lossy, bool):
        raise TypeError("allow_lossy must be bool")
    destination = Path(destination).absolute()
    if options is not None and not isinstance(options, Mapping):
        raise TypeError("options must be a mapping or None")
    values = dict(options or {})
    usd_format = (
        isinstance(format, str)
        and format.strip().lower().lstrip(".") in {"usd", "usda", "usdc", "usdz"}
    )
    if destination.suffix.lower() in _usd.USD_SUFFIXES or usd_format:
        if destination.suffix.lower() not in _usd.USD_SUFFIXES:
            raise UnsupportedFormatError(
                "OpenUSD sequence output requires a .usd, .usda, .usdc, or .usdz suffix"
            )
        requested = format.strip().lower().lstrip(".") if usd_format else None
        if requested not in {None, "usd", destination.suffix.lower().lstrip(".")}:
            raise UnsupportedFormatError(
                f"destination suffix {destination.suffix!r} does not match "
                f"format {format!r}"
            )
        unknown = set(values) - {"fps", "up_axis"}
        if unknown:
            raise UnsupportedFeatureError(
                f"Unknown OpenUSD writer options: {', '.join(sorted(unknown))}"
            )
        return _usd.write_usd_sequence(
            sequence, destination, overwrite=overwrite, **values
        )
    if values:
        raise UnsupportedFeatureError(
            f"This writer has no options; received: {', '.join(sorted(values))}"
        )
    suffix = _suffix_for(format)
    file_output = destination.suffix.lower() in _FRAME_READERS
    if suffix is None:
        suffix = destination.suffix.lower() if file_output else ".ply"
    if file_output and destination.suffix.lower() != suffix:
        raise UnsupportedFormatError(
            f"destination suffix {destination.suffix!r} does not match format {suffix!r}"
        )
    if not len(sequence):
        raise UnsupportedFeatureError("empty sequences cannot be exported")
    if file_output and len(sequence) != 1:
        raise UnsupportedFeatureError(
            "a multi-frame sequence needs a destination directory"
        )
    if file_output and not allow_lossy:
        raise UnsupportedFeatureError(
            "single mesh files cannot preserve temporal identity, frame or sequence "
            "metadata, or topology declarations; pass allow_lossy=True to export geometry"
        )
    if destination.exists() and not overwrite:
        raise FileExistsError(f"destination already exists: {destination}")

    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=f".{destination.name}.", dir=destination.parent))
    try:
        if file_output:
            generated = temporary / destination.name
            _write_frame(generated, sequence[0], suffix, allow_lossy=allow_lossy)
            if destination.exists():
                destination.unlink()
            shutil.move(generated, destination)
        else:
            manifest = {
                "schema": _MANIFEST_SCHEMA,
                "geometry_kind": "triangle_mesh",
                "format": suffix.lstrip("."),
                "metadata": _json_value(sequence.metadata, "sequence"),
                "topology": sequence.topology.value,
                "has_constant_vertex_count": sequence.has_constant_vertex_count,
                "has_vertex_correspondence": sequence.has_vertex_correspondence,
                "allow_nonmonotonic_timestamps": sequence.allow_nonmonotonic_timestamps,
                "frames": [],
            }
            for ordinal, frame in enumerate(sequence):
                name = f"frame_{ordinal:06d}{suffix}"
                _write_frame(
                    temporary / name, frame, suffix, allow_lossy=allow_lossy
                )
                manifest["frames"].append({
                    "index": frame.frame_index,
                    "timestamp": frame.timestamp,
                    "uri": name,
                    "metadata": _json_value(frame.metadata, f"frame {ordinal}"),
                })
            (temporary / _MANIFEST_NAME).write_text(
                json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
            )
            if destination.exists():
                shutil.rmtree(destination)
            temporary.replace(destination)
            temporary = None
    finally:
        if temporary is not None:
            shutil.rmtree(temporary, ignore_errors=True)
    return destination
