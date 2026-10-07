"""Whole browser clips in standalone O4D with native frame payloads.

The ``frames/1`` profile carries PLY, Draco, splat or image bytes verbatim.
O4D owns the manifest, timestamps, record framing and SHA-256 integrity.
A manifest prefix is enough to locate any frame's chunk records, so downloads
and HTTP byte ranges retain stable offsets without a second private format.
"""
from __future__ import annotations

import dataclasses
import hashlib
from io import BytesIO
import json
import math
import struct
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence as TypingSequence

from open4d._files import publish_directory, publish_file
from open4d.codec import _o4d_format as o4d
from open4d.codec._protocol import CodecError
from . import representations

MAGIC = o4d._MAGIC
VERSION = 1
_SUFFIXES = frozenset(("ply", "drc", "splat", "jpg", "jpeg", "png"))
_MAX_SAFE_INTEGER = 2**53 - 1


@dataclass(frozen=True)
class Entry:
    """First O4D payload record, native byte count and native SHA-256."""

    offset: int
    length: int
    sha256: str


@dataclass(frozen=True)
class Sequence:
    """A validated O4D frame manifest, usable before its payload arrives."""

    suffix: str
    entries: tuple[Entry, ...]
    end_offset: int
    manifest_hash: bytes

    @property
    def frames(self) -> int:
        return len(self.entries)

    @property
    def bytes(self) -> int:
        return sum(entry.length for entry in self.entries)


def _fps(value):
    if type(value) not in (int, float) or not math.isfinite(value) or value <= 0:
        raise ValueError("fps must be positive and finite")
    return value


def _header(descriptor, manifest_hash, start):
    if descriptor["codec"] != "frames":
        raise ValueError("O4D is not a browser frame-payload profile")
    entries, offset = [], start
    for record in descriptor["files"]:
        length = record["size"]
        entries.append(Entry(offset, length, record["sha256"]))
        offset += length + 18 * ((length + o4d._CHUNK - 1) // o4d._CHUNK)
        if offset + 50 > _MAX_SAFE_INTEGER:
            raise ValueError("O4D frame offsets exceed the browser integer limit")
    return Sequence(descriptor["native"]["suffix"], tuple(entries), offset, manifest_hash)


def pack(frames: TypingSequence[Path | str], destination: Path | str, *,
         fps: float = 30, representation: str | None = None,
         overwrite: bool = False) -> Sequence:
    """Write native whole frames into one .o4d without recompression."""
    destination = Path(destination)
    if destination.suffix.lower() != ".o4d":
        raise ValueError("destination must have a .o4d extension")
    if destination.exists() and not overwrite:
        raise FileExistsError(destination)
    if not isinstance(overwrite, bool):
        raise TypeError("overwrite must be bool")
    fps = _fps(fps)
    paths = [Path(path) for path in frames]
    if not paths:
        raise ValueError("a sequence needs at least one frame")
    if len(paths) > 65536:
        raise ValueError("O4D frame count exceeds limits")
    suffixes = {path.suffix.lower().lstrip(".") for path in paths}
    if len(suffixes) != 1:
        raise ValueError(f"frames must share one suffix; got {', '.join(sorted(suffixes))}")
    suffix = suffixes.pop()
    if suffix not in _SUFFIXES:
        raise ValueError(f"unsupported browser frame suffix {suffix!r}")
    if representation is None:
        representation = "pixels" if suffix in ("jpg", "jpeg", "png") else "gaussians" if suffix == "splat" else "mesh"
    native = dict(profile="frames/1", suffix=suffix, representation=representation)
    metadata = dict(version=1, codec="frames", native=native, metadata={},
                    frames=[dict(frame_index=i, timestamp=i / fps, metadata={})
                            for i in range(len(paths))])
    descriptor = dict(schema="vmesh/1", codec="frames", native_version=1,
                      representation="frame_payloads", dependency_mode="independent",
                      frame_count=len(paths), native=native, sequence=metadata, files=[])
    for index, path in enumerate(paths):
        if path.is_symlink() or not path.is_file():
            raise FileNotFoundError(f"missing or linked frame: {path}")
        digest, size = hashlib.sha256(), 0
        with path.open("rb") as stream:
            while chunk := stream.read(o4d._CHUNK):
                digest.update(chunk)
                size += len(chunk)
        descriptor["files"].append(dict(id=index, name=f"frame_{index:06d}.{suffix}",
                                        role="frame-payload", size=size,
                                        sha256=digest.hexdigest()))
    manifest = json.dumps(descriptor, separators=(",", ":"), allow_nan=False).encode()
    try:
        o4d._descriptor(manifest)
    except CodecError as error:
        raise ValueError(str(error)) from error
    if len(manifest) > o4d._MAX_JSON:
        raise ValueError("O4D manifest exceeds size limit")
    manifest_hash = hashlib.sha256(manifest).digest()
    header = _header(descriptor, manifest_hash, 26 + len(manifest))
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=f".{destination.name}-", dir=destination.parent) as directory:
        temporary = Path(directory) / "stream"
        with temporary.open("xb") as output:
            output.write(MAGIC)
            o4d._write_record(output, 0, 0, 0, manifest)
            for index, (path, entry) in enumerate(zip(paths, header.entries)):
                digest, offset = hashlib.sha256(), 0
                with path.open("rb") as stream:
                    while chunk := stream.read(o4d._CHUNK):
                        digest.update(chunk)
                        o4d._write_record(output, 1, index, offset, chunk)
                        offset += len(chunk)
                if offset != entry.length or digest.hexdigest() != entry.sha256:
                    raise ValueError(f"frame changed during packing: {path}")
            o4d._write_record(output, 2, 0, 0, manifest_hash)
        publish_file(temporary, destination, overwrite=overwrite)
    return header


def read_header(data: bytes) -> Sequence:
    """Validate a complete O4D manifest from a partial or complete download."""
    stream = BytesIO(data)
    try:
        descriptor, digest = o4d._start(stream)
    except CodecError as error:
        raise ValueError(str(error)) from error
    return _header(descriptor, digest, stream.tell())


def frame(data: bytes, index: int, header: Sequence | None = None) -> bytes:
    """Recover and hash-check one frame's O4D chunk records."""
    header = header or read_header(data)
    if type(index) is not int or not 0 <= index < header.frames:
        raise IndexError("frame index out of range")
    entry = header.entries[index]
    stream, output, digest = BytesIO(data), bytearray(), hashlib.sha256()
    stream.seek(entry.offset)
    try:
        while len(output) < entry.length:
            kind, file_id, offset, chunk = o4d._read_record(stream)
            length = min(o4d._CHUNK, entry.length - len(output))
            if (kind, file_id, offset, len(chunk)) != (1, index, len(output), length):
                raise ValueError("invalid O4D frame chunk order or length")
            digest.update(chunk)
            output.extend(chunk)
    except CodecError as error:
        raise ValueError(str(error)) from error
    if digest.hexdigest() != entry.sha256:
        raise ValueError("O4D frame SHA-256 mismatch")
    return bytes(output)


def unpack(path: Path | str, destination: Path | str) -> list[Path]:
    """Validate the complete O4D before publishing recovered native frames."""
    destination = Path(destination)
    if destination.exists():
        raise FileExistsError(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    try:
        descriptor = o4d.inspect_o4d(path)
        if descriptor["codec"] != "frames":
            raise ValueError("O4D is not a browser frame-payload profile")
        with tempfile.TemporaryDirectory(prefix=f".{destination.name}-", dir=destination.parent) as directory:
            native, output = Path(directory) / "native", Path(directory) / "frames"
            o4d.unpack_o4d(path, native)
            output.mkdir()
            names = [record["name"] for record in descriptor["files"]]
            for name in names:
                (native / name).replace(output / name)
            publish_directory(output, destination)
        return [destination / name for name in names]
    except CodecError as error:
        raise ValueError(str(error)) from error


def convert_legacy(source: Path | str, destination: Path | str, *,
                   fps: float = 30, representation: str | None = None,
                   overwrite: bool = False) -> Path:
    """Explicitly migrate a bounded retired O4DSEQ clip to real O4D records."""
    source, destination = Path(source), Path(destination)
    if destination.suffix.lower() != ".o4d":
        raise ValueError("destination must have a .o4d extension")
    if not isinstance(overwrite, bool):
        raise TypeError("overwrite must be bool")
    if destination.exists() and not overwrite:
        raise FileExistsError(destination)
    fps = _fps(fps)
    with source.open("rb") as stream:
        preamble = stream.read(20)
        if len(preamble) != 20:
            raise ValueError("truncated legacy sequence header")
        magic, version, count, suffix_length = struct.unpack("<8sIII", preamble)
        if magic != b"O4DSEQ\x00\x00" or version != 1:
            raise ValueError("unsupported legacy sequence magic/version")
        if not 0 < count <= 65536 or not 0 < suffix_length <= 8:
            raise ValueError("legacy sequence header exceeds limits")
        try:
            suffix = stream.read(suffix_length).decode("ascii")
        except UnicodeError as error:
            raise ValueError("invalid legacy sequence suffix") from error
        if suffix not in _SUFFIXES:
            raise ValueError("unsupported legacy frame suffix")
        table = stream.read(8 * count)
        if len(table) != 8 * count:
            raise ValueError("truncated legacy sequence table")
        offset = 20 + suffix_length + 8 * count
        records = []
        for index in range(count):
            position, length = struct.unpack_from("<II", table, 8 * index)
            if position != offset or length == 0:
                raise ValueError("invalid legacy frame offsets or lengths")
            records.append((position, length))
            offset += length
        if offset != source.stat().st_size:
            raise ValueError("legacy sequence is truncated or has trailing data")
        with tempfile.TemporaryDirectory(prefix="o4d-frame-migration-") as directory:
            paths = []
            for index, (position, length) in enumerate(records):
                path = Path(directory) / f"frame_{index:06d}.{suffix}"
                with path.open("xb") as output:
                    remaining = length
                    while remaining:
                        chunk = stream.read(min(o4d._CHUNK, remaining))
                        if not chunk:
                            raise ValueError("truncated legacy frame payload")
                        output.write(chunk)
                        remaining -= len(chunk)
                paths.append(path)
            pack(paths, destination, fps=fps, representation=representation, overwrite=overwrite)
    return destination


def _clip_paths(root, frames):
    relative = [Path(frame) for frame in frames]
    if any(path.is_absolute() or ".." in path.parts or "\\" in str(path) for path in relative):
        raise ValueError("clip frames must use safe relative paths")
    paths = [root / path for path in relative]
    if any(not path.resolve().is_relative_to(root.resolve()) for path in paths):
        raise ValueError("clip frames must stay inside the bundle")
    return paths


def pack_clip(bundle_dir, clip, *, keep_frames: bool = False, fps: float = 30) -> dict:
    """Pack one clip's frames into a container beside the bundle root.

    Returns the mapping a `bundle.Clip` records as its ``sequence``. The frame
    files are removed unless ``keep_frames``: leaving both doubles the bundle
    on disk for no benefit, since a client given a container never asks for the
    pieces.
    """
    root = Path(bundle_dir)
    if not clip.name or clip.name in (".", "..") or any(c in clip.name for c in ("/", "\\", "\x00")):
        raise ValueError("clip name must be a safe filename")
    paths = _clip_paths(root, clip.frames)
    relative = f"{clip.name}.o4d"
    header = pack(paths, root / relative, fps=fps, representation=clip.representation,
                  overwrite=True)
    if not keep_frames:
        directories = {Path(frame).parts[0] for frame in clip.frames}
        for frame in clip.frames:
            (root / frame).unlink(missing_ok=True)
        for name in directories:
            directory = root / name
            if directory.is_dir() and not any(directory.iterdir()):
                directory.rmdir()
    packed = {
        "url": relative,
        "frames": header.frames,
        "bytes": (root / relative).stat().st_size,
        "suffix": header.suffix,
    }
    # The container is served as octet-stream, so a client decoding a frame out
    # of it has no response header to read the frame's type from. Recorded here
    # from the registry rather than mapped again in the client, which would be
    # a second table to drift.
    media_type = representations.media_types().get(f".{header.suffix}")
    if media_type:
        packed["media_type"] = media_type
    return packed


def pack_bundle(
    bundle_dir: Path | str,
    *,
    names: TypingSequence[str] | None = None,
    keep_frames: bool = False,
) -> list[tuple[str, dict]]:
    """Pack a bundle's clips into containers and rewrite its manifest.

    Live clips are skipped rather than refused: a bundle is usually a mix, and
    a whole-bundle command that failed on the first stream would be unusable on
    exactly the bundles that have both. Clips already packed are skipped too,
    so running this twice is not an error. Retired packed clips are explicitly
    converted and their URLs updated; originals are removed after the manifest
    commits unless ``keep_frames`` is set.
    """
    from . import bundle as _bundle

    root = Path(bundle_dir)
    index = _bundle.read(root)
    if not index:
        raise FileNotFoundError(f"{root} has no manifest")

    clips = [_bundle.Clip(**entry) for entry in index.get("clips", [])]
    wanted = set(names) if names else None
    if wanted:
        missing = wanted - {clip.name for clip in clips}
        if missing:
            raise KeyError(f"no clip named {', '.join(sorted(missing))}")

    packed, changed, retired = [], [], []
    for clip in clips:
        skip = (
            (wanted is not None and clip.name not in wanted)
            or clip.stream is not None
            or (clip.sequence is not None and not str(clip.sequence.get("url", "")).endswith(".seq"))
            or not clip.frames
        )
        if skip:
            changed.append(clip)
            continue
        # Do not remove inputs until every clip is packed and the manifest
        # has committed. A later codec or filesystem failure must be retryable.
        if clip.sequence is not None:
            _clip_paths(root, clip.frames)
            old = Path(clip.sequence["url"])
            if old.is_absolute() or ".." in old.parts or "\\" in str(old):
                raise ValueError("legacy sequence must use a safe relative path")
            source = root / old
            if source.is_symlink() or not source.resolve().is_relative_to(root.resolve()):
                raise ValueError("legacy sequence must stay inside the bundle")
            relative = str(old.with_suffix(".o4d"))
            target = root / relative
            convert_legacy(source, target, fps=index.get("fps", 30),
                           representation=clip.representation, overwrite=True)
            with target.open("rb") as stream:
                descriptor, digest = o4d._start(stream)
                header = _header(descriptor, digest, stream.tell())
            if header.frames != len(clip.frames):
                raise ValueError("legacy sequence disagrees with its logical frame list")
            entry = dict(clip.sequence, url=relative, frames=header.frames,
                         bytes=target.stat().st_size, suffix=header.suffix)
            media_type = representations.media_types().get(f".{header.suffix}")
            if media_type:
                entry["media_type"] = media_type
            retired.append(source)
        else:
            entry = pack_clip(root, clip, keep_frames=True, fps=index.get("fps", 30))
        changed.append(dataclasses.replace(clip, sequence=entry))
        packed.append((clip.name, entry))

    if packed:
        _bundle.write(
            root,
            title=index.get("title", root.name),
            source=index.get("source", str(root)),
            clips=changed,
            fps=index.get("fps", 30),
            scenes=index.get("scenes") or {},
            detail=index.get("detail"),
        )
        if not keep_frames:
            from .transfer import frame_paths

            retained = set(frame_paths({"clips": [dataclasses.asdict(c) for c in changed]}))
            packed_names = {name for name, _ in packed}
            removable = {path for clip in clips if clip.name in packed_names
                         for path in clip.frames} - retained
            for path in removable:
                (root / path).unlink(missing_ok=True)
            for path in set(retired):
                if path.relative_to(root).as_posix() not in retained:
                    path.unlink()
            for directory in {root / Path(path).parts[0] for path in removable}:
                if directory.is_dir() and not any(directory.iterdir()):
                    directory.rmdir()
    return packed


def main(argv=None) -> int:
    import argparse

    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("bundle", type=Path, help="bundle directory to pack")
    parser.add_argument(
        "--clip", action="append", dest="clips", metavar="NAME",
        help="pack only this clip; repeatable (default: every packable clip)",
    )
    parser.add_argument(
        "--keep-frames", action="store_true",
        help="leave the frame files in place as well as the container",
    )
    args = parser.parse_args(argv)

    packed = pack_bundle(
        args.bundle, names=args.clips, keep_frames=args.keep_frames
    )
    if not packed:
        print("nothing to pack")
        return 0
    for name, entry in packed:
        print(
            f"{name:36s} {entry['frames']:4d} frames"
            f"  {entry['bytes'] / 1e6:8.1f} MB  {entry['url']}"
        )
    total = sum(entry["bytes"] for _, entry in packed)
    print(f"{len(packed)} clip(s), {total / 1e6:.1f} MB, one request each")
    return 0


class _MeshFrames:
    """Keep validated native payloads alive for lazy canonical mesh decoding."""

    def __init__(self, native, manifest, temporary):
        from types import MappingProxyType
        from open4d.core import TopologyMode
        self.native, self.manifest, self.temporary = native, manifest, temporary
        self.records = manifest["sequence"]["frames"]
        self.metadata = MappingProxyType(manifest["sequence"].get("metadata", {}))
        self.topology = TopologyMode(manifest["sequence"].get("topology", "unknown"))
        self.has_constant_vertex_count = manifest["sequence"].get("has_constant_vertex_count")
        self.has_vertex_correspondence = manifest["sequence"].get("has_vertex_correspondence")
        self.allow_nonmonotonic_timestamps = manifest["sequence"].get("allow_nonmonotonic_timestamps", False)

    @property
    def frame_count(self):
        return len(self.records)

    @property
    def timestamps(self):
        return tuple(record["timestamp"] for record in self.records)

    def get_frame(self, index):
        from open4d.core import Frame, TriangleMesh
        if not 0 <= index < self.frame_count:
            raise IndexError("frame index out of range")
        record = self.records[index]
        path = self.native / self.manifest["files"][index]["name"]
        if self.manifest["native"]["suffix"] == "ply":
            from open4d.io._mesh import read_ply
            positions, triangles, colors = read_ply(path)
            geometry = TriangleMesh(positions, triangles, colors=colors)
        else:
            from open4d.codec._draco import _backend
            decoded = _backend().decode(path.read_bytes())
            colors = decoded.colors.astype("float32") / 255 if decoded.colors is not None else None
            geometry = TriangleMesh(decoded.points, decoded.faces, colors=colors,
                                    normals=decoded.normals, texture_coordinates=decoded.tex_coord)
        return Frame(record["frame_index"], record["timestamp"], geometry, record.get("metadata", {}))

    def close(self):
        self.temporary.cleanup()


def open_frames(path: Path | str):
    """Open mesh payloads lazily, or retain other browser payloads natively."""
    from open4d.core import Sequence as MeshSequence
    from open4d.native import NativeSequence
    manifest = o4d.inspect_o4d(path)
    if manifest["codec"] != "frames":
        raise CodecError("O4D is not a browser frame-payload profile")
    if manifest["native"]["representation"] != "mesh":
        return NativeSequence(path)
    temporary = tempfile.TemporaryDirectory(prefix="o4d-browser-mesh-")
    try:
        native = Path(temporary.name) / "native"
        o4d.unpack_o4d(path, native)
        return MeshSequence(_MeshFrames(native, manifest, temporary))
    except BaseException:
        temporary.cleanup()
        raise


if __name__ == "__main__":
    raise SystemExit(main())
