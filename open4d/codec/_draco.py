"""In-process Google Draco sequence codec."""

from __future__ import annotations

from importlib import import_module
import json
from pathlib import Path
import tempfile
from types import MappingProxyType
from zipfile import BadZipFile, ZIP_STORED, ZipFile

import numpy as np

from open4d.core import Frame, Sequence, TopologyMode, TriangleMesh

from ._npz import _json_value
from ._protocol import CodecError

_SCHEMA = "open4d.draco-sequence/v1"


def _backend():
    try:
        return import_module("DracoPy")
    except ImportError as error:
        raise CodecError(
            "Draco needs the optional binding; install open4d[draco]"
        ) from error


def _encoder_arrays(mesh: TriangleMesh):
    """Adapt canonical arrays to DracoPy, splitting vertices at UV seams."""
    positions, triangles = mesh.positions, mesh.triangles
    colors = mesh.colors
    normals = mesh.normals
    texture_coordinates = mesh.texture_coordinates
    if texture_coordinates is not None and texture_coordinates.ndim == 3:
        source_indices = triangles.reshape(-1)
        positions = positions[source_indices]
        triangles = np.arange(len(source_indices), dtype=np.uint32).reshape(-1, 3)
        colors = None if colors is None else colors[source_indices]
        normals = None if normals is None else normals[source_indices]
        texture_coordinates = texture_coordinates.reshape(-1, 2)
    if colors is not None:
        colors = np.rint(np.clip(colors, 0, 1) * 255).astype(np.uint8)
    if normals is not None:
        normals = np.asarray(normals, dtype=np.float64)
    if texture_coordinates is not None:
        texture_coordinates = np.asarray(texture_coordinates, dtype=np.float64)
    return positions, triangles, colors, normals, texture_coordinates


class _DracoProvider:
    def __init__(self, archive: ZipFile, manifest: dict) -> None:
        self.archive = archive
        self.frames = manifest["frames"]
        self.metadata = MappingProxyType(manifest.get("metadata", {}))
        self.topology = TopologyMode(manifest.get("topology", "unknown"))
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
        return tuple(frame["timestamp"] for frame in self.frames)

    def get_frame(self, index: int) -> Frame:
        if index < 0 or index >= len(self.frames):
            raise IndexError("frame index out of range")
        record = self.frames[index]
        try:
            decoded = _backend().decode(self.archive.read(record["member"]))
        except Exception as error:
            raise CodecError(f"could not decode Draco frame {index}: {error}") from error
        colors = decoded.colors
        if colors is not None:
            colors = colors.astype(np.float32) / 255.0
        mesh = TriangleMesh(
            positions=decoded.points,
            triangles=decoded.faces,
            colors=colors,
            normals=decoded.normals,
            texture_coordinates=decoded.tex_coord,
        )
        return Frame(
            record["frame_index"], record["timestamp"], mesh,
            record.get("metadata", {}),
        )

    def close(self) -> None:
        self.archive.close()


class DracoCodec:
    """Store each mesh frame as a real Google Draco bitstream."""

    id = "draco"
    suffixes = (".d4d",)
    backend = "python-binding"
    lossless = False
    preserves = ("positions", "triangles", "colors", "normals", "texture_coordinates")

    def can_decode(self, source: Path) -> bool:
        try:
            with ZipFile(source) as archive:
                manifest = json.loads(archive.read("manifest.json"))
            return manifest.get("schema") == _SCHEMA
        except (OSError, BadZipFile, KeyError, json.JSONDecodeError):
            return False

    def encode(
        self,
        sequence: Sequence,
        destination: Path,
        *,
        overwrite: bool = False,
        quantization_bits: int = 14,
        compression_level: int = 7,
    ) -> Path:
        backend = _backend()
        if not isinstance(sequence, Sequence):
            raise TypeError("sequence must be an open4d.Sequence")
        destination = Path(destination).absolute()
        if destination.exists() and not overwrite:
            raise FileExistsError(f"artifact already exists: {destination}")
        destination.parent.mkdir(parents=True, exist_ok=True)
        manifest = {
            "schema": _SCHEMA,
            "codec": self.id,
            "metadata": _json_value(sequence.metadata, "sequence"),
            "topology": sequence.topology.value,
            "has_constant_vertex_count": sequence.has_constant_vertex_count,
            "has_vertex_correspondence": sequence.has_vertex_correspondence,
            "allow_nonmonotonic_timestamps": sequence.allow_nonmonotonic_timestamps,
            "frames": [],
        }
        with tempfile.NamedTemporaryFile(
            prefix=f".{destination.name}.", suffix=".tmp",
            dir=destination.parent, delete=False,
        ) as stream:
            temporary = Path(stream.name)
        try:
            with ZipFile(temporary, "w", compression=ZIP_STORED) as archive:
                for ordinal, frame in enumerate(sequence):
                    mesh = frame.geometry
                    if mesh.attributes:
                        raise CodecError("Draco custom attributes are not yet supported")
                    positions, triangles, colors, normals, texture_coordinates = (
                        _encoder_arrays(mesh)
                    )
                    member = f"frames/{ordinal:06d}.drc"
                    archive.writestr(member, backend.encode(
                        positions,
                        triangles,
                        colors=colors,
                        normals=normals,
                        tex_coord=texture_coordinates,
                        quantization_bits=quantization_bits,
                        compression_level=compression_level,
                        preserve_order=True,
                    ))
                    manifest["frames"].append({
                        "frame_index": frame.frame_index,
                        "timestamp": frame.timestamp,
                        "metadata": _json_value(frame.metadata, f"frame {ordinal}"),
                        "member": member,
                    })
                archive.writestr("manifest.json", json.dumps(
                    manifest, separators=(",", ":"), sort_keys=True,
                ))
            temporary.replace(destination)
        except Exception:
            temporary.unlink(missing_ok=True)
            raise
        return destination

    def decode(self, source: Path) -> Sequence:
        source = Path(source).absolute()
        if not source.is_file():
            raise FileNotFoundError(f"codec artifact does not exist: {source}")
        archive = None
        try:
            archive = ZipFile(source)
            manifest = json.loads(archive.read("manifest.json"))
            if manifest.get("schema") != _SCHEMA:
                raise CodecError("unsupported Draco artifact schema")
            if not isinstance(manifest.get("frames"), list):
                raise CodecError("Draco artifact manifest has no frame list")
            return Sequence(_DracoProvider(archive, manifest))
        except Exception as error:
            if archive is not None:
                archive.close()
            if isinstance(error, CodecError):
                raise
            if not isinstance(error, (BadZipFile, KeyError, json.JSONDecodeError)):
                raise
            raise CodecError(f"invalid Draco artifact {source}: {error}") from error


DRACO_CODEC = DracoCodec()
