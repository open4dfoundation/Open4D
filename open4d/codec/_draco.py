"""In-process Google Draco sequence codec."""

from __future__ import annotations

from importlib import import_module
import json
from pathlib import Path
import tempfile
from types import MappingProxyType

import numpy as np

from open4d.core import Frame, Sequence, TopologyMode, TriangleMesh

from ._metadata import _json_value
from ._vmesh_format import contains_codec, pack_vmesh, probe_codec, unpack_vmesh
from ._protocol import CodecError



def _backend():
    try:
        return import_module("DracoPy")
    except ImportError as error:
        raise CodecError(
            "Draco needs the optional binding; install 'DracoPy>=2,<3'"
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
    def __init__(self, temporary, native: Path, manifest: dict) -> None:
        self.temporary, self.native = temporary, native
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
            decoded = _backend().decode((self.native / f"frame_{index:06d}.drc").read_bytes())
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
        self.temporary.cleanup()


class DracoCodec:
    """Store each mesh frame as a real Google Draco bitstream."""

    id = "draco"
    suffixes = (".vmesh",)
    backend = "python-binding"
    lossless = False
    preserves = ("positions", "triangles", "colors", "normals", "texture_coordinates")

    def can_decode(self, source: Path) -> bool:
        return contains_codec(source, self.id)

    def encode(
        self,
        sequence: Sequence,
        destination: Path,
        *,
        overwrite: bool = False,
        quantization_bits: int = 14,
        compression_level: int = 7,
    ) -> Path:
        if not isinstance(sequence, Sequence):
            raise TypeError("sequence must be an open4d.Sequence")
        destination = Path(destination).absolute()
        if destination.suffix.lower() != ".vmesh":
            raise ValueError("Draco destination must have a .vmesh extension")
        if destination.exists() and not overwrite:
            raise FileExistsError(f"artifact already exists: {destination}")
        backend = _backend()
        destination.parent.mkdir(parents=True, exist_ok=True)
        manifest = {
            "version": 1, "codec": self.id, "native": {"profile": "draco/1"},
            "metadata": _json_value(sequence.metadata, "sequence"),
            "topology": sequence.topology.value,
            "has_constant_vertex_count": sequence.has_constant_vertex_count,
            "has_vertex_correspondence": sequence.has_vertex_correspondence,
            "allow_nonmonotonic_timestamps": sequence.allow_nonmonotonic_timestamps,
            "frames": [],
        }
        encoded_vertex_counts = []
        split_uv_corners = False
        with tempfile.TemporaryDirectory(prefix="open4d-draco-") as directory:
            native = Path(directory)
            for ordinal, frame in enumerate(sequence):
                mesh = frame.geometry
                if mesh.attributes:
                    raise CodecError("Draco custom attributes are not yet supported")
                positions, triangles, colors, normals, texture_coordinates = (
                    _encoder_arrays(mesh)
                )
                encoded_vertex_counts.append(len(positions))
                split_uv_corners |= (
                    mesh.texture_coordinates is not None
                    and mesh.texture_coordinates.ndim == 3
                )
                (native / f"frame_{ordinal:06d}.drc").write_bytes(backend.encode(
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
                })
            if split_uv_corners:
                manifest["topology"] = TopologyMode.UNKNOWN.value
                manifest["has_constant_vertex_count"] = (
                    len(set(encoded_vertex_counts)) == 1
                )
                manifest["has_vertex_correspondence"] = None
            (native / "metadata.json").write_text(json.dumps(manifest, allow_nan=False), encoding="utf-8")
            pack_vmesh(native, destination, overwrite=overwrite)
        return destination

    def decode(self, source: Path) -> Sequence:
        source = Path(source).absolute()
        if source.suffix.lower() != ".vmesh":
            raise CodecError("Draco decoding requires .vmesh; re-encode older private artifacts")
        if probe_codec(source) != self.id:
            raise CodecError("VMESH does not contain Draco")
        temporary = tempfile.TemporaryDirectory(prefix="open4d-draco-decode-")
        try:
            native = Path(temporary.name) / "native"
            unpack_vmesh(source, native)
            manifest = json.loads((native / "metadata.json").read_text(encoding="utf-8"))
            return Sequence(_DracoProvider(temporary, native, manifest))
        except BaseException:
            temporary.cleanup()
            raise


DRACO_CODEC = DracoCodec()
