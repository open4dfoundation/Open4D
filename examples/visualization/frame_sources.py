"""Open a 4D sequence from whatever format it happens to be in.

    sequence = open_sequence("my_capture/")              # folder of .obj/.ply
    sequence = open_sequence("my_capture.usdc")          # time-sampled USD
    sequence = open_sequence("frame.obj")                # single static frame

Two shapes of source, dispatched on the suffix:

- **A folder of per-frame files** — one mesh file per frame, ordered by the last
  number in the filename. Read lazily: only the listing happens up front.
  Formats come from `FRAME_READERS`.
- **A single file holding the whole sequence** — a time-sampled USD file.
  Formats come from `SEQUENCE_OPENERS`.

USD appears in both: a `.usd` file can be an animated sequence on its own, or
one frame among many in a folder.

Adding a format means adding one entry to a registry. A frame reader takes a
path and returns `(positions, triangles, colors)`; a sequence opener takes a
path and an fps and returns a `Sequence`.

One convention worth knowing: `open4d.core` has no point-cloud geometry type
yet, so a frame with no faces becomes a `TriangleMesh` with zero triangles.
`len(mesh.triangles) == 0` is the test for "these are just points".
"""

from __future__ import annotations

import re
from pathlib import Path
from typing import Callable

import numpy as np

# Import first: this puts the repository on sys.path for uninstalled clones.
import _common  # noqa: F401

import formats_mesh
import formats_usd
from open4d import Frame, Sequence, TopologyMode, TriangleMesh

FrameReader = Callable[[Path], "tuple[np.ndarray, np.ndarray, np.ndarray | None]"]
SequenceOpener = Callable[..., Sequence]

# Formats that hold ONE frame per file, usable inside a frame folder.
FRAME_READERS: dict[str, FrameReader] = {
    ".obj": formats_mesh.read_obj,
    ".ply": formats_mesh.read_ply,
    **{suffix: formats_usd.read_usd_frame for suffix in formats_usd.SUFFIXES},
    **{
        suffix: formats_mesh.read_with_trimesh
        for suffix in formats_mesh.TRIMESH_SUFFIXES
    },
}

# Formats where a single file holds the WHOLE sequence.
SEQUENCE_OPENERS: dict[str, SequenceOpener] = {
    suffix: formats_usd.open_usd_sequence for suffix in formats_usd.SUFFIXES
}

# Which extra provides each format, for error messages and `--help` text.
FORMAT_EXTRAS = {
    ".obj": None,
    ".ply": None,
    ".usd": "usd",
    ".usda": "usd",
    ".usdc": "usd",
    ".usdz": "usd",
    ".off": "tools",
    ".stl": "tools",
    ".glb": "tools",
    ".gltf": "tools",
}

FRAME_SUFFIXES = tuple(sorted(FRAME_READERS))
SEQUENCE_SUFFIXES = tuple(sorted(SEQUENCE_OPENERS))


def supported_formats() -> str:
    """A human-readable summary of every format the loader accepts."""
    def row(suffix: str) -> str:
        extra = FORMAT_EXTRAS.get(suffix)
        return f"  {suffix:<7}{f'  needs the [{extra}] extra' if extra else ''}".rstrip()

    lines = ["per-frame files (a folder of these, or one on its own):"]
    lines += [row(suffix) for suffix in FRAME_SUFFIXES]
    lines.append("whole-sequence files:")
    lines += [row(suffix) for suffix in SEQUENCE_SUFFIXES]
    return "\n".join(lines)


def frame_sort_key(name: str) -> tuple[float, str]:
    """Order `frame_2` before `frame_10`.

    Sorts on the last integer in the filename, falling back to the name.
    """
    numbers = re.findall(r"\d+", name)
    return (int(numbers[-1]) if numbers else float("inf"), name)


def read_frame_file(path: Path) -> tuple[np.ndarray, np.ndarray, np.ndarray | None]:
    """Read one frame, dispatching on the file suffix."""
    suffix = path.suffix.lower()
    reader = FRAME_READERS.get(suffix)
    if reader is None:
        raise SystemExit(
            f"No reader for {suffix!r} ({path}).\n{supported_formats()}"
        )
    if suffix == ".ply":
        try:
            return reader(path)
        except ValueError:
            # Exotic PLY variants (big-endian, unusual list elements) are worth
            # a second try through trimesh rather than a hard failure.
            return formats_mesh.read_with_trimesh(path)
    return reader(path)


def read_geometry(path: Path) -> TriangleMesh:
    """Read one frame file into a validated `TriangleMesh`.

    Parse and validation failures are re-raised naming the file. Pointed at a
    real dataset, one malformed frame among hundreds is the common case, and
    "positions must have shape (N, 3)" on its own does not say which file.
    """
    try:
        positions, triangles, colors = read_frame_file(path)
        return TriangleMesh(
            positions=positions, triangles=triangles, colors=colors
        )
    except SystemExit:
        raise  # a missing-dependency exit, already actionable
    except Exception as error:
        # Deliberately not `type(error)(...)`: plenty of exceptions take more
        # than one constructor argument — UnicodeDecodeError takes five — and
        # rebuilding them swallows the real error behind a TypeError.
        raise ValueError(
            f"{path}: {type(error).__name__}: {error}"
        ) from error


def list_frame_files(directory: Path) -> list[Path]:
    """Return the per-frame files in *directory*, in frame order.

    When a folder mixes formats, the most common suffix wins; the rest are
    reported and skipped rather than silently interleaved.
    """
    files = [
        entry
        for entry in directory.iterdir()
        if entry.is_file() and entry.suffix.lower() in FRAME_READERS
    ]
    if not files:
        raise SystemExit(
            f"{directory} contains no frame files.\n{supported_formats()}"
        )

    by_suffix: dict[str, list[Path]] = {}
    for entry in files:
        by_suffix.setdefault(entry.suffix.lower(), []).append(entry)
    if len(by_suffix) > 1:
        chosen = max(by_suffix, key=lambda suffix: len(by_suffix[suffix]))
        skipped = sorted(set(by_suffix) - {chosen})
        print(
            f"note: {directory} mixes formats; reading {len(by_suffix[chosen])} "
            f"{chosen} frames and skipping {', '.join(skipped)}"
        )
        files = by_suffix[chosen]

    files.sort(key=lambda entry: frame_sort_key(entry.name))
    return files


class FolderFrameProvider:
    """Lazy `FrameProvider` over a folder holding one mesh file per frame.

    Only the directory listing is done eagerly; each file is parsed when
    `get_frame()` is called, so a long sequence never has to fit in memory.
    """

    # Independently-reconstructed frames carry no promise that connectivity or
    # vertex count is stable, so leave that undeclared rather than claim
    # something the files do not support.
    topology = TopologyMode.UNKNOWN
    has_constant_vertex_count = None
    has_vertex_correspondence = None

    def __init__(self, directory: Path | str, fps: float = 30.0) -> None:
        if fps <= 0:
            raise ValueError("fps must be greater than zero")
        self.directory = Path(directory)
        self.fps = float(fps)
        self.files = list_frame_files(self.directory)
        self.metadata = {
            "name": self.directory.name,
            "source": str(self.directory),
            "fps": self.fps,
            "num_source_files": len(self.files),
            "format": sorted({entry.suffix.lower() for entry in self.files}),
        }

    @property
    def frame_count(self) -> int:
        return len(self.files)

    @property
    def timestamps(self) -> tuple[float, ...]:
        return tuple(index / self.fps for index in range(len(self.files)))

    def get_frame(self, index: int) -> Frame:
        if index < 0 or index >= self.frame_count:
            raise IndexError("frame index out of range")
        path = self.files[index]
        return Frame(
            frame_index=index,
            timestamp=index / self.fps,
            geometry=read_geometry(path),
            metadata={"file": path.name},
        )


class SingleFrameProvider:
    """A one-frame `Sequence` from a single static mesh file."""

    topology = TopologyMode.FIXED
    has_constant_vertex_count = True
    has_vertex_correspondence = True

    def __init__(self, path: Path | str) -> None:
        self.path = Path(path)
        self.metadata = {
            "name": self.path.stem,
            "source": str(self.path),
            "format": self.path.suffix.lower(),
            "frames": 1,
        }

    frame_count = 1
    timestamps = (0.0,)

    def get_frame(self, index: int) -> Frame:
        if index != 0:
            raise IndexError("frame index out of range")
        return Frame(
            frame_index=0,
            timestamp=0.0,
            geometry=read_geometry(self.path),
            metadata={"file": self.path.name},
        )


# ----------------------------
# The one entry point
# ----------------------------
def source_kind(path: Path | str) -> str:
    """Classify a source without decoding it.

    Returns ``"folder"``, ``"sequence-file"``, or ``"single-frame"``.
    """
    path = Path(path)
    if path.is_dir():
        return "folder"
    if not path.exists():
        raise SystemExit(f"{path} does not exist")
    suffix = path.suffix.lower()
    if suffix in SEQUENCE_OPENERS:
        return "sequence-file"
    if suffix in FRAME_READERS:
        return "single-frame"
    raise SystemExit(
        f"{path} has no reader for {suffix!r}.\n{supported_formats()}"
    )


DEFAULT_FPS = 30.0


def open_sequence(path: Path | str, fps: float | None = None) -> Sequence:
    """Open a frame folder, a whole-sequence file, or a single mesh file.

    `fps` is an override. Leave it None and each source uses whatever timing it
    declares: a USD file its own stage rate, a frame folder `DEFAULT_FPS` since
    a folder declares nothing. Passing a number forces that rate everywhere —
    which is why it must default to None rather than to a real value, or the
    default would silently override every stage rate it met.
    """
    path = Path(path)
    kind = source_kind(path)

    if kind == "folder":
        return Sequence(FolderFrameProvider(path, fps=fps or DEFAULT_FPS))
    if kind == "sequence-file":
        return SEQUENCE_OPENERS[path.suffix.lower()](path, fps)
    return Sequence(SingleFrameProvider(path))


def describe_source(path: Path | str) -> str:
    """One line about a source, without parsing any geometry."""
    path = Path(path)
    kind = source_kind(path)

    if kind == "folder":
        files = list_frame_files(path)
        formats = ", ".join(sorted({entry.suffix.lower() for entry in files}))
        megabytes = sum(entry.stat().st_size for entry in files) / 1e6
        return f"folder: {len(files)} {formats} frames, {megabytes:.2f} MB on disk"

    megabytes = path.stat().st_size / 1e6
    if kind == "single-frame":
        return f"single {path.suffix.lower()} frame, {megabytes:.2f} MB on disk"

    suffix = path.suffix.lower()
    detail = suffix
    if suffix in formats_usd.SUFFIXES:
        # The container records its own frame count, so report it without
        # composing a stage or touching the geometry.
        record = formats_usd.read_container_metadata(path)
        if record.get("frame_count"):
            detail = f"{suffix} ({record['frame_count']} frames)"
    return f"sequence file: {detail}, {megabytes:.2f} MB on disk"
