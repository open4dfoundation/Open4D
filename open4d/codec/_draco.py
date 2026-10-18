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
from ._o4d_format import contains_codec, pack_o4d, probe_codec, unpack_o4d
from ._protocol import CodecError
from ._mesh_payload import texture_arrays, read_texture
from ._npz import _read_array, _array_bytes, NumPyZipCodec
from zipfile import ZipFile, ZIP_DEFLATED



def _backend():
    try:
        return import_module("DracoPy")
    except ImportError as error:
        raise CodecError(
            "Draco needs the optional binding; install 'DracoPy>=2,<3'"
        ) from error


def _encoder_arrays(mesh: TriangleMesh, *, split_corners=False):
    """Adapt canonical arrays to DracoPy, splitting vertices at UV seams."""
    positions, triangles = mesh.positions, mesh.triangles
    colors = mesh.colors
    normals = mesh.normals
    texture_coordinates = mesh.texture_coordinates
    if split_corners or (texture_coordinates is not None and texture_coordinates.ndim == 3):
        source_indices = triangles.reshape(-1)
        positions = positions[source_indices]
        triangles = np.arange(len(source_indices), dtype=np.uint32).reshape(-1, 3)
        colors = None if colors is None else colors[source_indices]
        normals = None if normals is None else normals[source_indices]
        if texture_coordinates is not None:
            texture_coordinates = (texture_coordinates.reshape(-1, 2) if texture_coordinates.ndim == 3
                                   else texture_coordinates[source_indices])
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
        attributes, domains = {}, {}
        for name, spec in record.get("attributes", {}).items():
            attribute = decoded.get_attribute_by_name("open4d:" + name)
            if attribute is None:
                raise CodecError(f"Draco payload is missing attribute {name!r}")
            values = np.asarray(attribute["data"])
            dtype = np.dtype(spec["dtype"])
            if dtype not in (np.dtype("float32"), np.dtype("int32"), np.dtype("uint32"), np.dtype("bool")):
                raise CodecError("invalid Draco attribute dtype")
            if dtype == np.dtype("int32"):
                values = values.astype(np.uint32).view(np.int32)
            else:
                values = values.astype(dtype)
            domain = spec["domain"]
            if domain == "corner":
                values = values[np.asarray(decoded.faces)].reshape(-1, *spec["shape"])
            elif domain == "face":
                corners = values[np.asarray(decoded.faces)]
                if not np.all(corners == corners[:, :1]):
                    raise CodecError("Draco face attribute has inconsistent corners")
                values = corners[:, 0].reshape(-1, *spec["shape"])
            elif domain == "vertex":
                values = values.reshape(len(decoded.points), *spec["shape"])
            else:
                raise CodecError("invalid Draco attribute domain")
            attributes[name], domains[name] = values, domain
        texture = None
        if record.get("texture") is not None:
            with ZipFile(self.native / f"frame_{index:06d}.npz") as archive:
                budget = [512 * 1024 * 1024]
                def read(key):
                    if key == "texture_material_ids":
                        found = decoded.get_attribute_by_name("open4d-internal:material")
                        if found is None:
                            raise CodecError("Draco material indices are missing")
                        values = np.asarray(found["data"])[np.asarray(decoded.faces)]
                        if not np.all(values == values[:, :1]):
                            raise CodecError("Draco material indices disagree within a face")
                        return values[:, 0].reshape(-1).astype(np.int32)
                    return _read_array(archive, key, NumPyZipCodec(), budget)
                texture = read_texture(record["texture"], read)
        mesh = TriangleMesh(
            positions=decoded.points,
            triangles=decoded.faces,
            colors=colors,
            normals=decoded.normals,
            texture_coordinates=decoded.tex_coord,
            attributes=attributes, attribute_domains=domains, texture=texture,
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
    suffixes = (".o4d",)
    backend = "python-binding"
    lossless = False
    preserves = ("positions", "triangles", "colors", "normals", "texture_coordinates", "attributes", "texture")

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
        if destination.suffix.lower() != ".o4d":
            raise ValueError("Draco destination must have a .o4d extension")
        if destination.exists() and not overwrite:
            raise FileExistsError(f"artifact already exists: {destination}")
        backend = _backend()
        destination.parent.mkdir(parents=True, exist_ok=True)
        extended = any(frame.geometry.attributes or frame.geometry.texture is not None for frame in sequence)
        manifest = {
            "version": 1, "codec": self.id, "native": {"profile": "draco/2" if extended else "draco/1"},
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
                split = (any(domain != "vertex" for domain in mesh.attribute_domains.values())
                         or (mesh.texture is not None and mesh.texture.material_ids is not None)
                         or (mesh.texture_coordinates is not None and mesh.texture_coordinates.ndim == 3))
                positions, triangles, colors, normals, texture_coordinates = (
                    _encoder_arrays(mesh, split_corners=split)
                )
                indices = mesh.triangles.reshape(-1)
                generic, specifications = {}, {}
                for name, array in mesh.attributes.items():
                    domain = mesh.attribute_domains[name]
                    shape = list(array.shape[1:])
                    if int(np.prod(shape)) > 4:
                        raise CodecError("Draco custom attributes support at most four components per element")
                    values = (np.repeat(array, 3, axis=0) if domain == "face" else
                              array[indices] if domain == "vertex" and split else array)
                    values = values.reshape(len(positions), -1)
                    if values.dtype.kind == "i":
                        values = values.astype(np.int32).view(np.uint32)
                    elif values.dtype.kind == "b":
                        values = values.astype(np.uint8)
                    generic["open4d:" + name] = values
                    specifications[name] = {"domain": domain, "shape": shape, "dtype": array.dtype.name}
                texture, pixels = texture_arrays(mesh.texture)
                if mesh.texture is not None and mesh.texture.material_ids is not None:
                    generic["open4d-internal:material"] = np.repeat(mesh.texture.material_ids, 3).astype(np.uint32).reshape(-1, 1)
                    pixels.pop("texture_material_ids")
                if extended:
                    with ZipFile(native / f"frame_{ordinal:06d}.npz", "w", compression=ZIP_DEFLATED) as archive:
                        budget = [512 * 1024 * 1024]
                        for key, value in pixels.items():
                            archive.writestr(f"{key}.npy", NumPyZipCodec()._member(value, key, budget))
                encoded_vertex_counts.append(len(positions))
                split_uv_corners |= (
                    split
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
                    generic_attributes=generic or None,
                ))
                manifest["frames"].append({
                    "frame_index": frame.frame_index,
                    "timestamp": frame.timestamp,
                    "metadata": _json_value(frame.metadata, f"frame {ordinal}"),
                    "attributes": specifications, "texture": texture,
                })
            if split_uv_corners:
                manifest["topology"] = TopologyMode.UNKNOWN.value
                manifest["has_constant_vertex_count"] = (
                    len(set(encoded_vertex_counts)) == 1
                )
                manifest["has_vertex_correspondence"] = None
            (native / "metadata.json").write_text(json.dumps(manifest, allow_nan=False), encoding="utf-8")
            pack_o4d(native, destination, overwrite=overwrite)
        return destination

    def decode(self, source: Path) -> Sequence:
        source = Path(source).absolute()
        if source.suffix.lower() != ".o4d":
            raise CodecError("Draco decoding requires .o4d; re-encode older private artifacts")
        if probe_codec(source) != self.id:
            raise CodecError("O4D does not contain Draco")
        temporary = tempfile.TemporaryDirectory(prefix="open4d-draco-decode-")
        try:
            native = Path(temporary.name) / "native"
            unpack_o4d(source, native)
            manifest = json.loads((native / "metadata.json").read_text(encoding="utf-8"))
            return Sequence(_DracoProvider(temporary, native, manifest))
        except BaseException:
            temporary.cleanup()
            raise


DRACO_CODEC = DracoCodec()
