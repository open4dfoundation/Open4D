"""Lossless reference sequence codec using NumPy arrays inside ZIP."""

from __future__ import annotations

from collections.abc import Mapping
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

_SCHEMA = "open4d.numpy-zip/v1"
_FIELDS = ("positions", "triangles", "colors", "normals", "texture_coordinates")


def _json_value(value, name: str):
    if value is None or isinstance(value, (str, int, bool)):
        return value
    if isinstance(value, float):
        if not math.isfinite(value):
            raise CodecError(f"{name} metadata numbers must be finite")
        return value
    if isinstance(value, np.generic):
        return _json_value(value.item(), name)
    if isinstance(value, Path):
        return str(value)
    if isinstance(value, Mapping):
        if any(not isinstance(key, str) for key in value):
            raise CodecError(f"{name} metadata keys must be strings")
        return {key: _json_value(item, f"{name}.{key}") for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [_json_value(item, name) for item in value]
    raise CodecError(f"{name} metadata value {type(value).__name__} is not serializable")


def _array_bytes(array: np.ndarray) -> bytes:
    stream = BytesIO()
    np.save(stream, array, allow_pickle=False)
    return stream.getvalue()


def _read_array(archive: ZipFile, name: str, codec: "NumPyZipCodec") -> np.ndarray:
    try:
        payload = archive.read(name)
    except KeyError as error:
        raise CodecError(f"artifact is missing {name}") from error
    return np.load(BytesIO(codec.unpack(payload)), allow_pickle=False)


class _ZipProvider:
    def __init__(
        self, source: Path, archive: ZipFile, manifest: dict, codec: "NumPyZipCodec"
    ) -> None:
        self.source = source
        self.archive = archive
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
        values = {
            name: _read_array(self.archive, path, self.codec)
            for name, path in arrays.items()
            if name != "attributes"
        }
        attributes = {
            name: _read_array(self.archive, path, self.codec)
            for name, path in arrays.get("attributes", {}).items()
        }
        return Frame(
            frame_index=record["frame_index"],
            timestamp=record["timestamp"],
            geometry=TriangleMesh(**values, attributes=attributes),
            metadata=record.get("metadata", {}),
        )

    def close(self) -> None:
        self.archive.close()


class NumPyZipCodec:
    """Losslessly store canonical Open4D arrays in a portable `.o4d` ZIP."""

    suffixes = (".o4d",)
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
        decoded = np.repeat(pairs[:, 1], pairs[:, 0]).tobytes()
        if len(decoded) != expected:
            raise CodecError("RLE payload length does not match its header")
        return decoded

    def can_decode(self, source: Path) -> bool:
        try:
            with ZipFile(source, "r") as archive:
                manifest = json.loads(archive.read("manifest.json"))
                return isinstance(manifest, Mapping) and manifest.get("codec") == self.id
        except (OSError, BadZipFile, KeyError, json.JSONDecodeError):
            return False

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
            with ZipFile(
                temporary, "w", compression=self.compression,
                compresslevel=level,
            ) as archive:
                for ordinal, frame in enumerate(sequence):
                    prefix = f"frames/{ordinal:06d}"
                    arrays = {}
                    for name in _FIELDS:
                        value = getattr(frame.geometry, name)
                        if value is not None:
                            member = f"{prefix}/{name}.npy"
                            archive.writestr(member, self.pack(_array_bytes(value)))
                            arrays[name] = member
                    attributes = {}
                    for number, (name, value) in enumerate(frame.geometry.attributes.items()):
                        member = f"{prefix}/attribute_{number:04d}.npy"
                        archive.writestr(member, self.pack(_array_bytes(value)))
                        attributes[name] = member
                    arrays["attributes"] = attributes
                    manifest["frames"].append(
                        {
                            "frame_index": frame.frame_index,
                            "timestamp": frame.timestamp,
                            "metadata": _json_value(frame.metadata, f"frame {ordinal}"),
                            "arrays": arrays,
                        }
                    )
                archive.writestr(
                    "manifest.json",
                    json.dumps(manifest, separators=(",", ":"), sort_keys=True),
                )
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
            archive = ZipFile(source, "r")
            manifest = json.loads(archive.read("manifest.json"))
            if not isinstance(manifest, Mapping):
                raise CodecError("artifact manifest root must be an object")
            if manifest.get("schema") != _SCHEMA:
                raise CodecError(
                    f"unsupported artifact schema {manifest.get('schema')!r}"
                )
            if not isinstance(manifest.get("frames"), list):
                raise CodecError("artifact manifest has no frame list")
            if manifest.get("codec") != self.id:
                raise CodecError(
                    f"artifact uses codec {manifest.get('codec')!r}, not {self.id!r}"
                )
            return Sequence(_ZipProvider(source, archive, manifest, self))
        except Exception as error:
            if archive is not None:
                archive.close()
            if isinstance(error, CodecError):
                raise
            if not isinstance(error, (BadZipFile, KeyError, json.JSONDecodeError)):
                raise
            raise CodecError(f"invalid Open4D artifact {source}: {error}") from error


REFERENCE_CODECS = (
    NumPyZipCodec(),
    NumPyZipCodec("raw", compression=ZIP_STORED, compression_level=None),
    NumPyZipCodec("deflate", compression=ZIP_DEFLATED, compression_level=6),
    NumPyZipCodec("bzip2", compression=ZIP_BZIP2, compression_level=9),
    NumPyZipCodec("lzma", compression=ZIP_LZMA, compression_level=None),
    NumPyZipCodec("rle", compression=ZIP_STORED, compression_level=None, rle=True),
)
