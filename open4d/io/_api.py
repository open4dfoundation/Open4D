"""Public sequence loading over heterogeneous per-frame mesh storage."""

from __future__ import annotations

from dataclasses import dataclass
import math
from numbers import Real
import operator
import os
from pathlib import Path
import re
from types import MappingProxyType
from typing import Callable, Mapping

import numpy as np

from open4d.core import Frame, Sequence, TopologyMode, TriangleMesh

from . import _mesh
from ._errors import (
    AmbiguousFormatError,
    DecodeError,
    MissingDependencyError,
    SourceNotFoundError,
    UnsupportedFeatureError,
    UnsupportedFormatError,
)

_DEFAULT_FPS = 30.0

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


@dataclass(frozen=True)
class FormatInfo:
    """A per-frame format understood by the installed Open4D package."""

    id: str
    suffixes: tuple[str, ...]
    dependency_extra: str | None = None


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
    """Return the per-frame formats supported by the public loader."""
    return tuple(
        FormatInfo(identifier, (suffix,), _FORMAT_DEPENDENCIES[suffix])
        for identifier, suffix in sorted(_FORMAT_IDS.items())
    )


def _supported_formats() -> str:
    """Return supported formats as concise CLI help text."""
    lines = ["per-frame mesh files (one file or a directory of frames):"]
    for info in available_formats():
        extra = (
            f"  needs the [{info.dependency_extra}] extra"
            if info.dependency_extra
            else ""
        )
        lines.append(f"  {info.suffixes[0]:<7}{extra}".rstrip())
    return "\n".join(lines)


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
) -> tuple[Path, str, tuple[Path, ...], str]:
    # Providers outlive this call, so freeze relative sources before lazy reads.
    path = Path(source).absolute()
    if not path.exists():
        raise SourceNotFoundError(f"Sequence source does not exist: {path}")
    suffix = _suffix_for(format)
    if path.is_dir():
        files = _frame_files(path, suffix)
        return path, "directory", files, files[0].suffix.lower()
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
    return path, "file", (path,), selected


def inspect_sequence(
    source: str | os.PathLike[str], *, format: str | None = None
) -> SequenceInfo:
    """Inspect a local mesh file or frame directory without decoding geometry."""
    path, storage, files, suffix = _resolve(source, format)
    is_directory = storage == "directory"
    return SequenceInfo(
        source=path,
        storage=storage,
        format=suffix.lstrip("."),
        frame_count=len(files),
        geometry_kind="triangle_mesh",
        fps=_DEFAULT_FPS if is_directory else None,
        timing_source="default" if is_directory else "static",
        topology=TopologyMode.UNKNOWN if is_directory else TopologyMode.FIXED,
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
        if options:
            names = ", ".join(sorted(str(name) for name in options))
            raise UnsupportedFeatureError(
                f"This reader has no options; received: {names}"
            )
    path, storage, files, suffix = _resolve(source, format)
    if storage == "directory":
        return Sequence(_FolderProvider(path, files, _fps(fps)))
    if fps is not None:
        _fps(fps)  # Validate consistently even though one static frame has no rate.
    return Sequence(_SingleFrameProvider(path, suffix))
