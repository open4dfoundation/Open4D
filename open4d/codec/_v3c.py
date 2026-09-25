"""Experimental O4D native-codec carriage in V3C sample streams.

This deliberately implements one bounded application format, not a general
V3C parser or an ISO-conforming V-DMC geometry decoder. Usage examples are
in examples/vmesh/.
Native temporal payloads are carried; independent decoded frame sequences are
not an admitted profile.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import struct
import tempfile
import uuid

from open4d._files import publish_directory, publish_file

from ._npz import _validate_manifest
from ._protocol import CodecError
from ._native_profiles import PROFILES, layout as _layout

_UUID = uuid.UUID("e23b8c47-9135-4e34-90ad-421c6c57b63a").bytes
_SCHEMA = "open4d.vmesh.native/1"
_CHUNK = 1024 * 1024
_MAX_JSON = 16 * 1024 * 1024
_MAX_UNIT = _MAX_JSON + _MAX_JSON // 255 + 128
_RECORD = struct.Struct(">BBIQ")  # version, kind, file ID, byte offset
_U32 = struct.Struct(">I")
_AD = b"\x08\x00\x00\x00"
_NAL = b"\x56\x01"  # atlas PREFIX_NSEI (43), layer 0, temporal_id_plus1 1


def _bootstrap() -> bytes:
    """VPS syntax from MPEG V-DMC v14, with an unspecified (type 0) extension."""
    bits = []

    def put(value, width):
        bits.append(f"{value:0{width}b}")

    def ue(value):
        encoded = f"{value + 1:b}"
        bits.append("0" * (len(encoded) - 1) + encoded)

    # PTL: codec group 1, toolset 0, unconstrained reconstruction. These are
    # syntax fields, not a claim of conformance to a standardized toolset.
    for value, width in ((0, 1), (1, 7), (0, 8), (255, 8), (0, 1), (0, 8),
                         (0, 7), (15, 4), (0xfff, 12), (0, 8), (0, 6),
                         (0, 1), (0, 1)):
        put(value, width)
    for value, width in ((0, 4), (0, 8), (0, 6), (0, 6)):
        put(value, width)  # VPS 0, reserved, one atlas, atlas ID 0
    ue(1)  # placeholder width/height; no atlas pictures or video components
    ue(1)
    put(0, 4)  # one map
    put(0, 4)  # auxiliary, occupancy, geometry, attribute video absent
    put(1, 1)
    put(1, 8)  # one extension
    extension = _UUID + b"O4D\x01"
    ue(3 + len(extension) - 1)
    put(0, 8)  # VPS_EXT_UNSPECIFIED
    put(len(extension), 16)
    for byte in extension:
        put(byte, 8)
    bits.append("1")
    binary = "".join(bits)
    binary += "0" * (-len(binary) % 8)
    vps = b"\x00" * 4 + int(binary, 2).to_bytes(len(binary) // 8, "big")
    return b"\x60" + _U32.pack(len(vps)) + vps


_BOOTSTRAP = _bootstrap()


def _error(message):
    return CodecError(f"invalid O4D .vmesh: {message}")


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

    try:
        return json.loads(data, object_pairs_hook=pairs, parse_constant=constant)
    except (ValueError, UnicodeError, RecursionError) as error:
        raise _error(f"invalid JSON: {error}") from error


def _sequence_metadata(data, codec):
    value = _json(data)
    try:
        if not isinstance(value, dict) or type(value.get("version")) is not int or value["version"] != 1:
            raise ValueError("unsupported native metadata version")
        _validate_manifest(value, schema=None, codec=codec)
        if not value["frames"]:
            raise ValueError("empty frame list")
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
    if type(count) is not int or not 1 <= count <= _MAX_JSON // 16:
        raise _error("invalid frame count")
    expected = _layout(codec, count, value.get("native"))
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
    if files[0]["size"] > _MAX_JSON:
        raise _error("sequence metadata exceeds size limit")
    return value


def _write_record(stream, kind, file_id, offset, data):
    payload = _UUID + _RECORD.pack(1, kind, file_id, offset) + data
    size = len(payload)
    # Atlas SEI user_data_unregistered is payload type 4 (not video SEI 5).
    sei = b"\x04" + b"\xff" * (size // 255) + bytes([size % 255]) + payload + b"\x80"
    nal = _NAL + sei
    unit = _AD + b"\x60" + _U32.pack(len(nal)) + nal
    stream.write(_U32.pack(len(unit)))
    stream.write(unit)


def _read_record(stream):
    size = _U32.unpack(_read(stream, 4))[0]
    if not 45 <= size <= _MAX_UNIT:
        raise _error("V3C unit size outside limits")
    unit = _read(stream, size)
    if unit[:5] != _AD + b"\x60" or _U32.unpack(unit[5:9])[0] != size - 9:
        raise _error("invalid atlas unit or NAL sample length")
    if unit[9:12] != _NAL + b"\x04":
        raise _error("expected atlas prefix NSEI user_data_unregistered")
    pos, payload_size = 12, 0
    while pos < size:
        byte = unit[pos]
        payload_size += byte
        pos += 1
        if byte != 255:
            break
    if payload_size < 16 + _RECORD.size or pos + payload_size + 1 != size or unit[-1] != 0x80:
        raise _error("invalid SEI payload length or trailing bits")
    if unit[pos:pos + 16] != _UUID:
        raise _error("unexpected application UUID")
    version, kind, file_id, offset = _RECORD.unpack_from(unit, pos + 16)
    if version != 1 or kind not in (0, 1, 2):
        raise _error("unsupported record version/type")
    return kind, file_id, offset, unit[pos + 16 + _RECORD.size:-1]


def _start(stream):
    if _read(stream, len(_BOOTSTRAP)) != _BOOTSTRAP:
        raise _error("unsupported VPS/bootstrap")
    kind, file_id, offset, data = _read_record(stream)
    if (kind, file_id, offset) != (0, 0, 0) or len(data) > _MAX_JSON:
        raise _error("missing or oversized application manifest")
    return _descriptor(data), hashlib.sha256(data).digest()


def probe_codec(source: str | Path) -> str | None:
    """Identify this application format without loading native libraries.

    None means an ordinary/other V3C stream; recognized malformed O4D streams
    raise CodecError. This probe does not validate the native payload hashes.
    """
    with Path(source).open("rb") as stream:
        prefix = stream.read(len(_BOOTSTRAP))
        if prefix != _BOOTSTRAP:
            if _UUID in prefix or b"O4D\x01" in prefix:
                raise _error("damaged VPS/bootstrap")
            return None
        stream.seek(0)
        descriptor, _ = _start(stream)
        return descriptor["codec"]


def _consume(source, destination=None):
    with Path(source).open("rb") as stream:
        descriptor, manifest_hash = _start(stream)
        sequence_data = bytearray()
        for record in descriptor["files"]:
            digest, offset = hashlib.sha256(), 0
            target = (destination / record["name"]).open("xb") if destination is not None else None
            try:
                while offset < record["size"]:
                    kind, file_id, position, data = _read_record(stream)
                    if ((kind, file_id, position) != (1, record["id"], offset)
                            or not 0 < len(data) <= min(_CHUNK, record["size"] - offset)):
                        raise _error("missing, reordered or oversized native payload chunk")
                    digest.update(data)
                    if target is not None:
                        target.write(data)
                    if record["id"] == 0:
                        sequence_data.extend(data)
                    offset += len(data)
            finally:
                if target is not None:
                    target.close()
            if digest.hexdigest() != record["sha256"]:
                raise _error(f"SHA-256 mismatch for {record['name']}")
        kind, file_id, offset, data = _read_record(stream)
        if (kind, file_id, offset, data) != (2, 0, 0, manifest_hash) or stream.read(1):
            raise _error("invalid end record or trailing data")
        sequence = _sequence_metadata(sequence_data, descriptor["codec"])
        if len(sequence["frames"]) != descriptor["frame_count"]:
            raise _error("frame count disagrees with sequence metadata")
        if sequence.get("native") != descriptor.get("native"):
            raise _error("native profile disagrees with sequence metadata")
        return {**descriptor, "sequence": sequence}


def inspect_vmesh(source: str | Path) -> dict:
    """Validate an O4D .vmesh and return its manifest and sequence metadata.

    Streams and verifies all payload hashes without extracting/deserializing
    native data or requiring the research codecs' optional dependencies.
    """
    return _consume(source)


def pack_vmesh(source: str | Path, destination: str | Path, *, overwrite: bool = False) -> Path:
    """Pack an O4D native directory or legacy N4MC .n4d into one .vmesh.

    Required native files are preserved byte for byte. Scratch files and
    decoded geometry are excluded. This does not recompress the native data.
    """
    if not isinstance(overwrite, bool):
        raise TypeError("overwrite must be bool")
    source, destination = Path(source).absolute(), Path(destination).absolute()
    if destination.suffix.lower() != ".vmesh":
        raise ValueError("destination must have a .vmesh extension")
    if destination.exists() and not overwrite:
        raise FileExistsError(destination)
    if destination.is_dir():
        raise IsADirectoryError(destination)
    if source.suffix.lower() == ".n4d" and source.is_file():
        if source.is_symlink():
            raise _error("linked N4MC source archive")
        from ._n4mc import _extract_n4d, _write_native_metadata
        with tempfile.TemporaryDirectory(prefix="open4d-n4mc-repack-") as directory:
            native = Path(directory)
            metadata = _extract_n4d(source, native)
            _write_native_metadata(metadata, native)
            return pack_vmesh(native, destination, overwrite=overwrite)
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
                      dependency_mode=PROFILES[codec][1], files=[])
    if "native" in sequence:
        descriptor["native"] = sequence["native"]
    for index, (name, role) in enumerate(_layout(codec, len(sequence["frames"]), sequence.get("native"))):
        path = source / name
        if path.is_symlink() or not path.is_file():
            raise _error(f"missing or linked native payload {name}")
        digest, size = hashlib.sha256(), 0
        with path.open("rb") as stream:
            while chunk := stream.read(_CHUNK):
                digest.update(chunk)
                size += len(chunk)
        # Preserve the exact metadata we validated even if the source changed.
        if index == 0 and digest.digest() != hashlib.sha256(data).digest():
            raise _error("metadata changed during packing")
        descriptor["files"].append(dict(id=index, name=name, role=role, size=size, sha256=digest.hexdigest()))
    manifest = json.dumps(descriptor, separators=(",", ":"), allow_nan=False).encode("utf-8")
    if len(manifest) > _MAX_JSON:
        raise _error("application manifest exceeds size limit")
    _descriptor(manifest)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=f".{destination.name}-", dir=destination.parent) as directory:
        temporary = Path(directory) / "stream"
        with temporary.open("xb") as output:
            output.write(_BOOTSTRAP)
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


def unpack_vmesh(source: str | Path, destination: str | Path) -> Path:
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
