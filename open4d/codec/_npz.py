"""Lossless array profiles using standard NPZ payloads inside O4D."""

from __future__ import annotations

from io import BytesIO
import json
import math
from pathlib import Path
import tempfile
from types import MappingProxyType
from zipfile import (
    BadZipFile,
    ZIP_BZIP2,
    ZIP_DEFLATED,
    ZIP_LZMA,
    ZIP_STORED,
    ZipFile,
)

import numpy as np

from open4d.core import Frame, Sequence, TopologyMode, TriangleMesh, PointCloud, GaussianCloud

from ._protocol import CodecError
from ._mesh_payload import texture_arrays, texture_fields, read_texture
from ._metadata import _json_value
from ._o4d_format import contains_codec, pack_o4d, probe_codec, unpack_o4d

_MAX_ARRAY_BYTES = 256 * 1024 * 1024
_MAX_FRAME_BYTES = 512 * 1024 * 1024
_MAX_FRAME_MEMBERS = 256
# Worst-case RLE doubles its input, plus its length prefix and the NPY header.
_MAX_RLE_MEMBER_BYTES = 2 * _MAX_ARRAY_BYTES + 8 + 4096
_FIELDS = ("positions", "triangles", "colors", "normals", "texture_coordinates")
_GEOMETRIES = {
    "mesh": (TriangleMesh, _FIELDS, {"positions", "triangles"}),
    "points": (PointCloud, ("positions", "colors", "normals"), {"positions"}),
    "gaussians": (GaussianCloud, ("positions", "scales", "rotations", "opacities", "colors"),
                  {"positions", "scales", "rotations", "opacities"}),
}


def _array_bytes(array: np.ndarray) -> bytes:
    stream = BytesIO()
    np.save(stream, array, allow_pickle=False)
    return stream.getvalue()


def _array_from_bytes(payload: bytes) -> np.ndarray:
    stream = BytesIO(payload)
    version = np.lib.format.read_magic(stream)
    readers = {(1, 0): np.lib.format.read_array_header_1_0,
               (2, 0): np.lib.format.read_array_header_2_0}
    if version not in readers:
        raise CodecError("unsupported array NPY version")
    shape, _, dtype = readers[version](stream, max_header_size=4096)
    if dtype.hasobject or math.prod(shape) * dtype.itemsize != len(payload) - stream.tell():
        raise CodecError("array NPY size/type disagrees with payload")
    return np.load(BytesIO(payload), allow_pickle=False, max_header_size=4096)


def _member_charge(archive: ZipFile, member, codec: "NumPyZipCodec") -> int:
    """Bound an RLE expansion from its header before allocating any array."""
    if not codec.rle:
        return member.file_size
    with archive.open(member) as stream:
        version = np.lib.format.read_magic(stream)
        readers = {(1, 0): np.lib.format.read_array_header_1_0,
                   (2, 0): np.lib.format.read_array_header_2_0}
        if version not in readers:
            raise CodecError("unsupported array NPY version")
        shape, _, dtype = readers[version](stream, max_header_size=4096)
        if (dtype != np.uint8 or len(shape) != 1 or shape[0] < 8
                or shape[0] != member.file_size - stream.tell()):
            raise CodecError("RLE array payload must contain encoded bytes")
        prefix = stream.read(8)
        if len(prefix) != 8:
            raise CodecError("invalid RLE payload")
        expanded = int.from_bytes(prefix, "little")
        if expanded > _MAX_ARRAY_BYTES:
            raise CodecError("RLE payload length outside limits")
        return max(member.file_size, expanded)


def _read_array(archive: ZipFile, name: str, codec: "NumPyZipCodec", budget=None) -> np.ndarray:
    try:
        member = archive.getinfo(f"{name}.npy")
        limit = _MAX_RLE_MEMBER_BYTES if codec.rle else _MAX_ARRAY_BYTES
        if not 0 < member.file_size <= limit:
            raise CodecError("array payload outside limits")
        expanded_bytes = _member_charge(archive, member, codec)
        if budget is not None:
            if expanded_bytes > budget[0]:
                raise CodecError("aggregate frame array payload outside limits")
            budget[0] -= expanded_bytes
        value = _array_from_bytes(archive.read(member))
        if codec.rle:
            value = _array_from_bytes(codec.unpack(value.tobytes()))
        return value
    except (BadZipFile, KeyError, ValueError, TypeError) as error:
        raise CodecError(f"invalid array payload {name}: {error}") from error


def _geometry(representation: str, values: dict, attributes: dict, domains=None, texture=None):
    """Build a frame geometry from stored arrays, keeping lossless bit patterns."""
    geometry_type = _GEOMETRIES[representation][0]
    options = {"attribute_domains": domains or {}, "texture": texture} if representation == "mesh" else {}
    geometry = geometry_type(**values, attributes=attributes, **options)
    if geometry_type is GaussianCloud:
        # Canonical GaussianCloud quaternions are already normalized.
        # Preserve their exact float bit patterns through a lossless codec.
        rotations = values["rotations"]
        norms = np.linalg.norm(rotations, axis=1)
        if (rotations.dtype == geometry.rotations.dtype
                and np.allclose(norms, 1, rtol=0, atol=4 * np.finfo(rotations.dtype).eps)):
            object.__setattr__(geometry, "rotations", rotations)
    return geometry


class _ZipProvider:
    def __init__(
        self, temporary, native: Path, manifest: dict, codec: "NumPyZipCodec"
    ) -> None:
        self.temporary, self.native = temporary, native
        self.codec = codec
        self.frames = manifest["frames"]
        self.metadata = MappingProxyType(manifest.get("metadata", {}))
        try:
            self.topology = TopologyMode(manifest.get("topology", "unknown"))
        except ValueError as error:
            raise CodecError(f"artifact has invalid topology: {error}") from error
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
        if index < 0:
            raise IndexError("frame index out of range")
        try:
            record = self.frames[index]
        except IndexError as error:
            raise IndexError("frame index out of range") from error
        arrays = record["arrays"]
        try:
            archive = ZipFile(self.native / f"frame_{index:06d}.npz")
        except BadZipFile as error:
            raise CodecError(f"invalid array payload frame {index}: {error}") from error
        with archive:
            budget = [_MAX_FRAME_BYTES]
            values = {
                name: _read_array(archive, path, self.codec, budget)
                for name, path in arrays.items()
                if name != "attributes"
            }
            attributes = {
                name: _read_array(archive, path, self.codec, budget)
                for name, path in arrays.get("attributes", {}).items()
            }
            texture = read_texture(record.get("texture"),
                                   lambda key: _read_array(archive, key, self.codec, budget))
        geometry = _geometry(record.get("representation", "mesh"), values, attributes,
                             record.get("attribute_domains"), texture)
        return Frame(
            frame_index=record["frame_index"],
            timestamp=record["timestamp"],
            geometry=geometry,
            metadata=record.get("metadata", {}),
        )

    def close(self) -> None:
        self.temporary.cleanup()


class NumPyZipCodec:
    """Losslessly carry mesh, point and Gaussian frame arrays in O4D."""

    suffixes = (".o4d",)
    backend = "python"
    lossless = True
    preserves = (*_FIELDS, "scales", "rotations", "opacities", "attributes", "attribute_domains", "texture")
    representation = "geometry_sequence"

    def __init__(
        self,
        identifier: str = "npz",
        *,
        compression: int = ZIP_DEFLATED,
        compression_level: int | None = 6,
        rle: bool = False,
    ) -> None:
        self.id = identifier
        self.compression = compression
        self.compression_level = compression_level
        self.rle = rle

    def pack(self, payload: bytes) -> bytes:
        if not self.rle or not payload:
            return payload
        values = np.frombuffer(payload, dtype=np.uint8)
        changes = np.flatnonzero(values[1:] != values[:-1]) + 1
        starts = np.concatenate(([0], changes))
        ends = np.concatenate((changes, [len(values)]))
        encoded = bytearray(len(payload).to_bytes(8, "little"))
        for value, length in zip(values[starts], ends - starts, strict=True):
            while length > 255:
                encoded.extend((255, int(value)))
                length -= 255
            encoded.extend((int(length), int(value)))
        return bytes(encoded)

    def unpack(self, payload: bytes) -> bytes:
        if not self.rle or not payload:
            return payload
        if len(payload) < 8 or (len(payload) - 8) % 2:
            raise CodecError("invalid RLE payload")
        expected = int.from_bytes(payload[:8], "little")
        pairs = np.frombuffer(payload[8:], dtype=np.uint8).reshape(-1, 2)
        if (expected > _MAX_ARRAY_BYTES or np.any(pairs[:, 0] == 0)
                or int(pairs[:, 0].sum(dtype=np.int64)) != expected):
            raise CodecError("RLE payload length does not match its bounded header")
        return np.repeat(pairs[:, 1], pairs[:, 0]).tobytes()

    def _member(self, value: np.ndarray, label: str, budget=None) -> bytes:
        # Enforce the decoder's bound here so encode never publishes an
        # artifact that its own decoder would reject.
        payload = _array_bytes(value)
        if len(payload) > _MAX_ARRAY_BYTES:
            raise CodecError(
                f"{self.id} array {label} is {len(payload)} bytes; the limit is {_MAX_ARRAY_BYTES}"
            )
        original_bytes = len(payload)
        if budget is not None and original_bytes > budget[0]:
            raise CodecError("aggregate frame array payload outside limits")
        payload = self.pack(payload)
        if self.rle:
            payload = _array_bytes(np.frombuffer(payload, dtype=np.uint8))
        if budget is not None:
            charge = max(original_bytes, len(payload))
            if charge > budget[0]:
                raise CodecError("aggregate frame array payload outside limits")
            budget[0] -= charge
        return payload

    def can_decode(self, source: Path) -> bool:
        return contains_codec(source, self.id)

    def encode(
        self,
        sequence: Sequence,
        destination: Path,
        *,
        overwrite: bool = False,
        compression_level: int | None = None,
    ) -> Path:
        if not isinstance(sequence, Sequence):
            raise TypeError("sequence must be an open4d.Sequence")
        level = self.compression_level if compression_level is None else compression_level
        if level is not None and not 0 <= level <= 9:
            raise ValueError("compression_level must be in [0, 9]")
        destination = Path(destination).absolute()
        if destination.suffix.lower() != ".o4d":
            raise ValueError("array destination must have a .o4d extension")
        if destination.exists() and not overwrite:
            raise FileExistsError(f"artifact already exists: {destination}")
        destination.parent.mkdir(parents=True, exist_ok=True)
        manifest = {
            "version": 1, "codec": self.id,
            "native": {"profile": f"{self.id}/3", "rle": self.rle},
            "metadata": _json_value(sequence.metadata, "sequence"),
            "topology": sequence.topology.value,
            "has_constant_vertex_count": sequence.has_constant_vertex_count,
            "has_vertex_correspondence": sequence.has_vertex_correspondence,
            "allow_nonmonotonic_timestamps": sequence.allow_nonmonotonic_timestamps,
            "frames": [],
        }
        with tempfile.TemporaryDirectory(prefix="open4d-arrays-") as directory:
            native = Path(directory)
            for ordinal, frame in enumerate(sequence):
                representation = frame.geometry.representation.value
                if representation not in _GEOMETRIES:
                    raise TypeError(f"array codecs do not support {representation!r} frames")
                fields = _GEOMETRIES[representation][1]
                member_count = sum(getattr(frame.geometry, name) is not None for name in fields)
                texture, pixels = texture_arrays(getattr(frame.geometry, "texture", None))
                if member_count + len(frame.geometry.attributes) + len(pixels) > _MAX_FRAME_MEMBERS:
                    raise CodecError("frame array member count outside limits")
                budget = [_MAX_FRAME_BYTES]
                arrays = {}
                with ZipFile(native / f"frame_{ordinal:06d}.npz", "w", compression=self.compression,
                             compresslevel=level) as archive:
                    for name in fields:
                        value = getattr(frame.geometry, name)
                        if value is not None:
                            archive.writestr(f"{name}.npy", self._member(value, f"frame {ordinal} {name}", budget))
                            arrays[name] = name
                    attributes = {}
                    for number, (name, value) in enumerate(frame.geometry.attributes.items()):
                        key = f"attribute_{number:04d}"
                        archive.writestr(f"{key}.npy", self._member(value, f"frame {ordinal} attribute {name!r}", budget))
                        attributes[name] = key
                    arrays["attributes"] = attributes
                    for key, value in pixels.items():
                        archive.writestr(f"{key}.npy", self._member(value, key, budget))
                manifest["frames"].append({
                    "frame_index": frame.frame_index, "timestamp": frame.timestamp,
                    "representation": representation,
                    "metadata": _json_value(frame.metadata, f"frame {ordinal}"), "arrays": arrays,
                    **({"attribute_domains": dict(frame.geometry.attribute_domains), "texture": texture}
                       if representation == "mesh" else {}),
                })
            (native / "metadata.json").write_text(json.dumps(manifest, allow_nan=False), encoding="utf-8")
            pack_o4d(native, destination, overwrite=overwrite)
        return destination

    def decode(self, source: Path) -> Sequence:
        source = Path(source).absolute()
        if source.suffix.lower() != ".o4d":
            raise CodecError("array decoding requires .o4d; re-encode older private artifacts")
        if probe_codec(source) != self.id:
            raise CodecError(f"O4D does not contain {self.id}")
        temporary = tempfile.TemporaryDirectory(prefix="open4d-arrays-decode-")
        try:
            native = Path(temporary.name) / "native"
            unpack_o4d(source, native)
            manifest = json.loads((native / "metadata.json").read_text(encoding="utf-8"))
            decoder = NumPyZipCodec(self.id, rle=manifest["native"]["rle"])
            for ordinal, frame in enumerate(manifest["frames"]):
                representation = frame.get("representation", "mesh")
                if representation not in _GEOMETRIES:
                    raise CodecError("invalid array frame representation")
                if manifest["native"]["profile"] == f"{self.id}/1" and representation != "mesh":
                    raise CodecError("array profile version 1 supports mesh frames only")
                _, fields, required = _GEOMETRIES[representation]
                arrays = frame.get("arrays")
                if (not isinstance(arrays, dict) or not required <= set(arrays)
                        or not set(arrays) <= {*fields, "attributes"}
                        or any(value != name for name, value in arrays.items() if name != "attributes")):
                    raise CodecError("invalid mesh array field index")
                attributes = arrays.get("attributes", {})
                if (not isinstance(attributes, dict) or not all(isinstance(name, str) for name in attributes)
                        or list(attributes.values()) != [f"attribute_{i:04d}" for i in range(len(attributes))]):
                    raise CodecError("invalid mesh attribute field index")
                expected = {f"{key}.npy" for key in arrays if key != "attributes"}
                expected.update(f"{key}.npy" for key in attributes.values())
                expected.update(f"{key}.npy" for key in texture_fields(frame.get("texture")))
                if representation != "mesh" and frame.get("texture") is not None:
                    raise CodecError("only mesh frames carry textures")
                with ZipFile(native / f"frame_{ordinal:06d}.npz") as archive:
                    members = archive.namelist()
                    if len(members) > _MAX_FRAME_MEMBERS:
                        raise CodecError("frame array member count outside limits")
                    if sum(_member_charge(archive, item, decoder) for item in archive.infolist()) > _MAX_FRAME_BYTES:
                        raise CodecError("aggregate frame array payload outside limits")
                    if len(members) != len(set(members)) or set(members) != expected:
                        raise CodecError("unexpected NPZ frame arrays")
            return Sequence(_ZipProvider(temporary, native, manifest, decoder))
        except BaseException:
            temporary.cleanup()
            raise


REFERENCE_CODECS = (
    NumPyZipCodec(),
    NumPyZipCodec("raw", compression=ZIP_STORED, compression_level=None),
    NumPyZipCodec("deflate", compression=ZIP_DEFLATED, compression_level=6),
    NumPyZipCodec("bzip2", compression=ZIP_BZIP2, compression_level=9),
    NumPyZipCodec("lzma", compression=ZIP_LZMA, compression_level=None),
    NumPyZipCodec("rle", compression=ZIP_STORED, compression_level=None, rle=True),
)
