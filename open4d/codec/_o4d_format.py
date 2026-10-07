"""Standalone O4D container for native codec payloads.

O4D owns its framing, sequence metadata and codec profiles. No V3C units,
private SEI messages or serialized Open4D objects are used. Native payloads
are copied verbatim and inspection never deserializes them.
"""

from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
import struct
import tempfile

from open4d._files import publish_directory, publish_file

from ._metadata import _validate_manifest
from ._protocol import CodecError
from ._native_profiles import MAX_FRAMES, NEURAL_CODECS, PROFILES, layout as _layout

# These serialized identifiers predate the O4D public rename. Keep them stable
# so existing files need only an extension change, without payload conversion.
_SCHEMA = "vmesh/1"
_CHUNK = 1024 * 1024
_MAX_JSON = 16 * 1024 * 1024
_RECORD = struct.Struct(">BBIQ")  # version, kind, file ID, byte offset
_U32 = struct.Struct(">I")
_MAGIC = b"VMESH\x00\x01\x00"
_MAX_UNIT = _MAX_JSON + _RECORD.size


def _error(message):
    return CodecError(f"invalid O4D: {message}")


def _read(stream, size):
    data = stream.read(size)
    if len(data) != size:
        raise _error("truncated stream")
    return data


def _json(data):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError(f"duplicate JSON key {key!r}")
            result[key] = value
        return result

    def constant(value):
        raise ValueError(f"non-finite JSON value {value}")

    def real(value):
        number = float(value)
        if not math.isfinite(number):
            raise ValueError("non-finite JSON number")
        return number

    try:
        return json.loads(data, object_pairs_hook=pairs, parse_constant=constant, parse_float=real)
    except (ValueError, UnicodeError, RecursionError) as error:
        raise _error(f"invalid JSON: {error}") from error


def _sequence_metadata(data, codec):
    value = _json(data)
    try:
        if not isinstance(value, dict) or type(value.get("version")) is not int or value["version"] != 1:
            raise ValueError("unsupported native metadata version")
        _validate_manifest(value, schema=None, codec=codec)
        if "schema" in value:
            raise ValueError("O4D metadata cannot contain an application schema")
        if not 0 < len(value["frames"]) <= MAX_FRAMES:
            raise ValueError("frame count outside limits")
        _layout(codec, len(value["frames"]), value.get("native"))
    except (ValueError, TypeError, KeyError) as error:
        raise _error(f"invalid sequence metadata: {error}") from error
    return value


def _descriptor(data):
    value = _json(data)
    if not isinstance(value, dict) or value.get("schema") != _SCHEMA:
        raise _error("unsupported application schema/version")
    codec, count, files = value.get("codec"), value.get("frame_count"), value.get("files")
    if not isinstance(codec, str) or codec not in PROFILES:
        raise _error(f"unsupported native codec {codec!r}")
    if (value.get("native_version") != 1 or type(value.get("native_version")) is not int
            or value.get("representation") != PROFILES[codec][0]
            or value.get("dependency_mode") != PROFILES[codec][1]):
        raise _error("unsupported native representation/dependencies")
    if type(count) is not int or not 1 <= count <= MAX_FRAMES:
        raise _error("invalid frame count")
    expected = _layout(codec, count, value.get("native"))[1:]
    if not isinstance(files, list) or len(files) != len(expected):
        raise _error("invalid native file list")
    for index, (record, (name, role)) in enumerate(zip(files, expected)):
        if (not isinstance(record, dict) or type(record.get("id")) is not int
                or record["id"] != index or record.get("name") != name or record.get("role") != role):
            raise _error("unexpected native filename, role or file ID")
        size, digest = record.get("size"), record.get("sha256")
        if type(size) is not int or not 0 < size < 2**64:
            raise _error("invalid native payload size")
        if not isinstance(digest, str) or len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
            raise _error("invalid SHA-256 digest")
    sequence = value.get("sequence")
    if not isinstance(sequence, dict) or "schema" in sequence:
        raise _error("invalid O4D sequence metadata")
    parsed = _sequence_metadata(json.dumps(sequence, allow_nan=False).encode(), codec)
    if len(parsed["frames"]) != count or parsed.get("native") != value.get("native"):
        raise _error("sequence metadata disagrees with codec profile")
    return value


def _write_record(stream, kind, file_id, offset, data):
    unit = _RECORD.pack(1, kind, file_id, offset) + data
    stream.write(_U32.pack(len(unit)))
    stream.write(unit)


def _read_record(stream):
    size = _U32.unpack(_read(stream, 4))[0]
    if not _RECORD.size <= size <= _MAX_UNIT:
        raise _error("record size outside limits")
    version, kind, file_id, offset = _RECORD.unpack(_read(stream, _RECORD.size))
    if version != 1 or kind not in (0, 1, 2):
        raise _error("unsupported record version/type")
    length = size - _RECORD.size
    maximum = (_MAX_JSON, _CHUNK, 32)[kind]
    if not 0 < length <= maximum:
        raise _error("record payload size outside limits")
    return kind, file_id, offset, _read(stream, length)


def _start(stream):
    if _read(stream, len(_MAGIC)) != _MAGIC:
        raise _error("unsupported magic/version")
    kind, file_id, offset, data = _read_record(stream)
    if (kind, file_id, offset) != (0, 0, 0) or len(data) > _MAX_JSON:
        raise _error("missing or oversized application manifest")
    return _descriptor(data), hashlib.sha256(data).digest()


def probe_codec(source: str | Path) -> str | None:
    """Identify standalone O4D without loading native libraries.

    None means another format, including raw MPEG V-DMC. Recognizable corrupt
    O4D and the retired private O4D/V3C wrapper fail before native decoding.
    """
    with Path(source).open("rb") as stream:
        prefix = stream.read(128)
        if not prefix.startswith(_MAGIC):
            if prefix.startswith(b"PK\x03\x04"):
                raise _error("archive input requires explicit migration")
            if prefix.startswith(b"VMESH") or prefix[1:8] == _MAGIC[1:]:
                raise _error("damaged magic/version")
            if bytes.fromhex("e23b8c4791354e3490ad421c6c57b63a") in prefix or b"O4D\x01" in prefix:
                raise _error("retired O4D/V3C wrapper; repack the original native directory")
            return None
        stream.seek(0)
        descriptor, _ = _start(stream)
        return descriptor["codec"]


def contains_codec(source: str | Path, codec: str) -> bool:
    """Boolean probe for ``Codec.can_decode``; never raises for foreign input.

    Callers that need the reason a file is rejected use probe_codec instead.
    """
    if Path(source).suffix.lower() != ".o4d":
        return False
    try:
        return probe_codec(source) == codec
    except (CodecError, OSError):
        return False


def is_mesh_profile(source: str | Path) -> bool:
    """Classify O4D for mesh-only tools without loading a decoder."""
    codec = probe_codec(source)
    if codec in NEURAL_CODECS:
        return False
    if codec == "frames":
        with Path(source).open("rb") as stream:
            descriptor, _ = _start(stream)
        return descriptor["native"]["representation"] == "mesh"
    return True


def _consume(source, destination=None):
    with Path(source).open("rb") as stream:
        descriptor, manifest_hash = _start(stream)
        for record in descriptor["files"]:
            digest, offset = hashlib.sha256(), 0
            target = (destination / record["name"]).open("xb") if destination is not None else None
            try:
                while offset < record["size"]:
                    kind, file_id, position, data = _read_record(stream)
                    if ((kind, file_id, position) != (1, record["id"], offset)
                            or not 0 < len(data) <= min(_CHUNK, record["size"] - offset)):
                        raise _error("missing, reordered or oversized native payload chunk")
                    if descriptor["codec"] == "frames" and len(data) != min(_CHUNK, record["size"] - offset):
                        raise _error("frames profile requires fixed-size payload chunks")
                    digest.update(data)
                    if target is not None:
                        target.write(data)
                    offset += len(data)
            finally:
                if target is not None:
                    target.close()
            if digest.hexdigest() != record["sha256"]:
                raise _error(f"SHA-256 mismatch for {record['name']}")
        kind, file_id, offset, data = _read_record(stream)
        if (kind, file_id, offset, data) != (2, 0, 0, manifest_hash) or stream.read(1):
            raise _error("invalid end record or trailing data")
        if destination is not None:
            # Adapter input is reconstructed from O4D metadata. It is not a
            # carried Open4D file or object in the container.
            (destination / "metadata.json").write_text(
                json.dumps(descriptor["sequence"], allow_nan=False), encoding="utf-8")
        return descriptor


def inspect_o4d(source: str | Path) -> dict:
    """Validate a standalone O4D and return its manifest and sequence metadata.

    Streams and verifies all payload hashes without extracting/deserializing
    native data or requiring the research codecs' optional dependencies.
    """
    return _consume(source)


def pack_o4d(source: str | Path, destination: str | Path, *, overwrite: bool = False) -> Path:
    """Pack a native codec directory into one .o4d.

    Required native files are preserved byte for byte. Scratch files and
    decoded geometry are excluded. This does not recompress the native data.
    """
    if not isinstance(overwrite, bool):
        raise TypeError("overwrite must be bool")
    source, destination = Path(source).absolute(), Path(destination).absolute()
    if destination.suffix.lower() != ".o4d":
        raise ValueError("destination must have a .o4d extension")
    if destination.exists() and not overwrite:
        raise FileExistsError(destination)
    if destination.is_dir():
        raise IsADirectoryError(destination)
    metadata = source / "metadata.json"
    if metadata.is_symlink() or not metadata.is_file() or metadata.stat().st_size > _MAX_JSON:
        raise _error("missing, linked or oversized metadata.json")
    data = metadata.read_bytes()
    parsed = _json(data)
    codec = parsed.get("codec") if isinstance(parsed, dict) else None
    if not isinstance(codec, str) or codec not in PROFILES:
        raise _error(f"unsupported native codec {codec!r}")
    sequence = _sequence_metadata(data, codec)
    descriptor = dict(schema=_SCHEMA, codec=codec, native_version=1,
                      representation=PROFILES[codec][0], frame_count=len(sequence["frames"]),
                      dependency_mode=PROFILES[codec][1], sequence=sequence, files=[])
    if "native" in sequence:
        descriptor["native"] = sequence["native"]
    for index, (name, role) in enumerate(_layout(codec, len(sequence["frames"]), sequence.get("native"))[1:]):
        path = source / name
        if path.is_symlink() or not path.is_file():
            raise _error(f"missing or linked native payload {name}")
        digest, size = hashlib.sha256(), 0
        with path.open("rb") as stream:
            while chunk := stream.read(_CHUNK):
                digest.update(chunk)
                size += len(chunk)
        descriptor["files"].append(dict(id=index, name=name, role=role, size=size, sha256=digest.hexdigest()))
    manifest = json.dumps(descriptor, separators=(",", ":"), allow_nan=False).encode("utf-8")
    if len(manifest) > _MAX_JSON:
        raise _error("application manifest exceeds size limit")
    _descriptor(manifest)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=f".{destination.name}-", dir=destination.parent) as directory:
        temporary = Path(directory) / "stream"
        with temporary.open("xb") as output:
            output.write(_MAGIC)
            _write_record(output, 0, 0, 0, manifest)
            for record in descriptor["files"]:
                digest, offset = hashlib.sha256(), 0
                with (source / record["name"]).open("rb") as stream:
                    while chunk := stream.read(_CHUNK):
                        digest.update(chunk)
                        _write_record(output, 1, record["id"], offset, chunk)
                        offset += len(chunk)
                if offset != record["size"] or digest.hexdigest() != record["sha256"]:
                    raise _error(f"{record['name']} changed during packing")
            _write_record(output, 2, 0, 0, hashlib.sha256(manifest).digest())
        publish_file(temporary, destination, overwrite=overwrite)
    return destination


def unpack_o4d(source: str | Path, destination: str | Path) -> Path:
    """Validate and recover a native directory without decoding geometry.

    The destination must not exist. Partial or corrupt input is never published.
    """
    destination = Path(destination).absolute()
    if destination.exists():
        raise FileExistsError(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=f".{destination.name}-", dir=destination.parent) as directory:
        result = Path(directory) / "native"
        result.mkdir()
        _consume(source, result)
        publish_directory(result, destination)
    return destination
