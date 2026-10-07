"""Lossless array profiles using standard NPZ payloads inside VMESH."""

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

from open4d.core import Frame, Sequence, TopologyMode, TriangleMesh

from ._protocol import CodecError
from ._metadata import _json_value
from ._vmesh_format import contains_codec, pack_vmesh, probe_codec, unpack_vmesh

_MAX_ARRAY_BYTES = 256 * 1024 * 1024
# Worst-case RLE doubles its input, plus its length prefix and the NPY header.
_MAX_RLE_MEMBER_BYTES = 2 * _MAX_ARRAY_BYTES + 8 + 4096
_FIELDS = ("positions", "triangles", "colors", "normals", "texture_coordinates")


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


def _read_array(archive: ZipFile, name: str, codec: "NumPyZipCodec") -> np.ndarray:
    try:
        member = archive.getinfo(f"{name}.npy")
        limit = _MAX_RLE_MEMBER_BYTES if codec.rle else _MAX_ARRAY_BYTES
        if not 0 < member.file_size <= limit:
            raise CodecError("array payload outside limits")
        value = _array_from_bytes(archive.read(member))
        if codec.rle:
            if value.dtype != np.uint8 or value.ndim != 1:
                raise CodecError("RLE array payload must contain encoded bytes")
            value = _array_from_bytes(codec.unpack(value.tobytes()))
        return value
    except (BadZipFile, KeyError, ValueError, TypeError) as error:
        raise CodecError(f"invalid array payload {name}: {error}") from error


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
            values = {
                name: _read_array(archive, path, self.codec)
                for name, path in arrays.items()
                if name != "attributes"
            }
            attributes = {
                name: _read_array(archive, path, self.codec)
                for name, path in arrays.get("attributes", {}).items()
            }
        return Frame(
            frame_index=record["frame_index"],
            timestamp=record["timestamp"],
            geometry=TriangleMesh(**values, attributes=attributes),
            metadata=record.get("metadata", {}),
        )

    def close(self) -> None:
        self.temporary.cleanup()


class NumPyZipCodec:
    """Losslessly carry standard NumPy frame arrays in standalone VMESH."""

    suffixes = (".vmesh",)
    backend = "python"
    lossless = True
    preserves = (*_FIELDS, "attributes")

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

    def _member(self, value: np.ndarray, label: str) -> bytes:
        # Enforce the decoder's bound here so encode never publishes an
        # artifact that its own decoder would reject.
        payload = _array_bytes(value)
        if len(payload) > _MAX_ARRAY_BYTES:
            raise CodecError(
                f"{self.id} array {label} is {len(payload)} bytes; the limit is {_MAX_ARRAY_BYTES}"
            )
        payload = self.pack(payload)
        if self.rle:
            payload = _array_bytes(np.frombuffer(payload, dtype=np.uint8))
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
        if destination.suffix.lower() != ".vmesh":
            raise ValueError("array destination must have a .vmesh extension")
        if destination.exists() and not overwrite:
            raise FileExistsError(f"artifact already exists: {destination}")
        destination.parent.mkdir(parents=True, exist_ok=True)
        manifest = {
            "version": 1, "codec": self.id,
            "native": {"profile": f"{self.id}/1", "rle": self.rle},
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
                arrays = {}
                with ZipFile(native / f"frame_{ordinal:06d}.npz", "w", compression=self.compression,
                             compresslevel=level) as archive:
                    for name in _FIELDS:
                        value = getattr(frame.geometry, name)
                        if value is not None:
                            archive.writestr(f"{name}.npy", self._member(value, f"frame {ordinal} {name}"))
                            arrays[name] = name
                    attributes = {}
                    for number, (name, value) in enumerate(frame.geometry.attributes.items()):
                        key = f"attribute_{number:04d}"
                        archive.writestr(f"{key}.npy", self._member(value, f"frame {ordinal} attribute {name!r}"))
                        attributes[name] = key
                    arrays["attributes"] = attributes
                manifest["frames"].append({
                    "frame_index": frame.frame_index, "timestamp": frame.timestamp,
                    "metadata": _json_value(frame.metadata, f"frame {ordinal}"), "arrays": arrays,
                })
            (native / "metadata.json").write_text(json.dumps(manifest, allow_nan=False), encoding="utf-8")
            pack_vmesh(native, destination, overwrite=overwrite)
        return destination

    def decode(self, source: Path) -> Sequence:
        source = Path(source).absolute()
        if source.suffix.lower() != ".vmesh":
            raise CodecError("array decoding requires .vmesh; re-encode older private artifacts")
        if probe_codec(source) != self.id:
            raise CodecError(f"VMESH does not contain {self.id}")
        temporary = tempfile.TemporaryDirectory(prefix="open4d-arrays-decode-")
        try:
            native = Path(temporary.name) / "native"
            unpack_vmesh(source, native)
            manifest = json.loads((native / "metadata.json").read_text(encoding="utf-8"))
            for ordinal, frame in enumerate(manifest["frames"]):
                arrays = frame.get("arrays")
                if (not isinstance(arrays, dict) or not {"positions", "triangles"} <= set(arrays)
                        or not set(arrays) <= {*_FIELDS, "attributes"}
                        or any(value != name for name, value in arrays.items() if name != "attributes")):
                    raise CodecError("invalid mesh array field index")
                attributes = arrays.get("attributes", {})
                if (not isinstance(attributes, dict) or not all(isinstance(name, str) for name in attributes)
                        or list(attributes.values()) != [f"attribute_{i:04d}" for i in range(len(attributes))]):
                    raise CodecError("invalid mesh attribute field index")
                expected = {f"{key}.npy" for key in arrays if key != "attributes"}
                expected.update(f"{key}.npy" for key in attributes.values())
                with ZipFile(native / f"frame_{ordinal:06d}.npz") as archive:
                    members = archive.namelist()
                    if len(members) != len(set(members)) or set(members) != expected:
                        raise CodecError("unexpected NPZ frame arrays")
            decoder = NumPyZipCodec(self.id, rle=manifest["native"]["rle"])
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
