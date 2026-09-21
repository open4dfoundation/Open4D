"""A whole clip as one file.

A clip is a directory of frames, and fetching it a frame at a time is how this
started: thirty requests for thirty frames. On loopback that is free, which is
why it survived. Over a link it is thirty round trips before anything plays --
600 ms of pure latency on a 20 ms connection -- and it is thirty chances for
one frame to arrive late and stall a clip that was otherwise complete.

For **on demand** none of that buys anything. A viewer with a free camera has
to hold the whole sequence in memory anyway, because a frame it has thrown away
cannot be redrawn from a new angle. So the unit of transfer should be the
sequence, and there is no reason to have asked for it in pieces.

Hence a container: a header naming the frames, then their bytes end to end. One
request, one response, one progress bar. Deliberately not a zip or a tar --
both would need a decoder in the client and neither buys anything here, since
the frames are already compressed and an archive's own compression would only
spend CPU to save nothing.

    header  magic "O4DSEQ\\0\\0" | version | frame count | suffix
            then per frame: offset, length -- both uint32 little-endian
    body    each frame's bytes, in playback order, unpadded

The offsets are absolute within the file, so a client that has the header can
slice any frame out without walking the ones before it -- which is what lets a
partially arrived download start playing from the beginning while the rest
lands.
"""
from __future__ import annotations

import dataclasses
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

from . import representations

MAGIC = b"O4DSEQ\x00\x00"
VERSION = 1
#: ``magic + version + count + suffix length``, before the suffix itself.
_PREAMBLE = struct.calcsize("<8sIII")


@dataclass(frozen=True)
class Entry:
    """Where one frame sits inside the container."""

    offset: int
    length: int


@dataclass(frozen=True)
class Sequence:
    """A container's header: what is in it and where."""

    suffix: str
    entries: tuple[Entry, ...]

    @property
    def frames(self) -> int:
        return len(self.entries)

    @property
    def bytes(self) -> int:
        return sum(entry.length for entry in self.entries)


def pack(frames: Sequence[Path | str], destination: Path | str) -> Sequence:
    """Write ``frames`` into one container at ``destination``.

    Every frame must share a suffix. A container holding two formats would need
    the client to switch decoders mid-sequence, and a clip whose frames are not
    all one codec is a clip that should have been two.
    """
    paths = [Path(f) for f in frames]
    if not paths:
        raise ValueError("a sequence needs at least one frame")
    suffixes = {path.suffix.lower().lstrip(".") for path in paths}
    if len(suffixes) != 1:
        raise ValueError(
            f"frames must share one suffix; got {', '.join(sorted(suffixes))}"
        )
    suffix = suffixes.pop()
    missing = [str(path) for path in paths if not path.is_file()]
    if missing:
        raise FileNotFoundError(f"{len(missing)} frame(s) missing, e.g. {missing[0]}")

    encoded = suffix.encode()
    header = _PREAMBLE + len(encoded) + 8 * len(paths)
    entries, offset = [], header
    for path in paths:
        length = path.stat().st_size
        entries.append(Entry(offset=offset, length=length))
        offset += length

    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with open(destination, "wb") as out:
        out.write(struct.pack("<8sIII", MAGIC, VERSION, len(paths), len(encoded)))
        out.write(encoded)
        for entry in entries:
            out.write(struct.pack("<II", entry.offset, entry.length))
        for path in paths:
            out.write(path.read_bytes())
    return Sequence(suffix=suffix, entries=tuple(entries))


def read_header(data: bytes) -> Sequence:
    """The header of a container, from its first bytes.

    Takes bytes rather than a path so a client that has only the start of a
    download can already know what is coming -- which is what a progress bar
    counting frames needs.
    """
    if len(data) < _PREAMBLE:
        raise ValueError("not enough bytes for a sequence header")
    magic, version, count, suffix_length = struct.unpack_from("<8sIII", data)
    if magic != MAGIC:
        raise ValueError(f"not a sequence container: magic is {magic!r}")
    if version != VERSION:
        raise ValueError(f"sequence version {version} is not {VERSION}")
    at = _PREAMBLE
    suffix = data[at:at + suffix_length].decode()
    at += suffix_length
    needed = at + 8 * count
    if len(data) < needed:
        raise ValueError(f"header needs {needed} bytes, got {len(data)}")
    entries = tuple(
        Entry(*struct.unpack_from("<II", data, at + 8 * index))
        for index in range(count)
    )
    return Sequence(suffix=suffix, entries=entries)


def frame(data: bytes, index: int, header: Sequence | None = None) -> bytes:
    """One frame's bytes out of a container."""
    header = header or read_header(data)
    entry = header.entries[index]
    return data[entry.offset:entry.offset + entry.length]


def unpack(path: Path | str, destination: Path | str) -> list[Path]:
    """Write a container back out as numbered frame files.

    The inverse, for checking a container round-trips and for anyone who wants
    the frames on disk again.
    """
    data = Path(path).read_bytes()
    header = read_header(data)
    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    written = []
    for index in range(header.frames):
        target = destination / f"frame_{index:04d}.{header.suffix}"
        target.write_bytes(frame(data, index, header))
        written.append(target)
    return written


def pack_clip(bundle_dir, clip, *, keep_frames: bool = False) -> dict:
    """Pack one clip's frames into a container beside the bundle root.

    Returns the mapping a `bundle.Clip` records as its ``sequence``. The frame
    files are removed unless ``keep_frames``: leaving both doubles the bundle
    on disk for no benefit, since a client given a container never asks for the
    pieces.
    """
    root = Path(bundle_dir)
    relative = f"{clip.name}.seq"
    header = pack([root / frame for frame in clip.frames], root / relative)
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
    names: Sequence[str] | None = None,
    keep_frames: bool = False,
) -> list[tuple[str, dict]]:
    """Pack a bundle's clips into containers and rewrite its manifest.

    Live clips are skipped rather than refused: a bundle is usually a mix, and
    a whole-bundle command that failed on the first stream would be unusable on
    exactly the bundles that have both. Clips already packed are skipped too,
    so running this twice is not an error.
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

    packed, changed = [], []
    for clip in clips:
        skip = (
            (wanted is not None and clip.name not in wanted)
            or clip.stream is not None
            or clip.sequence is not None
            or not clip.frames
        )
        if skip:
            changed.append(clip)
            continue
        entry = pack_clip(root, clip, keep_frames=keep_frames)
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


if __name__ == "__main__":
    raise SystemExit(main())
