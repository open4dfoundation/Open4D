"""Recognizing the three on-disk shapes a multi-view video scene arrives in.

The two upstreams disagree about layout. QUEEN reads a scene whose frames live
inside per-camera videos or per-camera image directories (DyNeRF, Google
Immersive); 3DGStream wants one directory per timestep, each containing that
timestep's views, with COLMAP cameras copied in from frame 0. Detecting which one
is present is what lets a single `gs-tools train` accept either method.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from pathlib import Path


class Layout(str, Enum):
    """The layouts `gs-tools data prepare` understands."""

    DYNERF = "dynerf"
    IMMERSIVE = "immersive"
    #: 3DGStream's per-timestep form: <scene>/frame000001/<views>
    COLMAP_FRAMES = "colmap-frames"
    UNKNOWN = "unknown"


@dataclass(frozen=True)
class Scene:
    """A detected scene: what it is, and the frames found in it."""

    root: Path
    layout: Layout
    frame_count: int
    #: Per-timestep directories, sorted, for COLMAP_FRAMES; empty otherwise.
    frame_dirs: tuple[Path, ...] = ()
    #: The COLMAP reconstruction both methods need for camera poses.
    colmap: Path | None = None

    @property
    def prepared(self) -> bool:
        """Whether a trainer can be pointed at this scene as it stands."""
        return self.layout is not Layout.UNKNOWN and self.colmap is not None


def _colmap_dir(root: Path) -> Path | None:
    """COLMAP output, in either the sparse/0 or the flat form 3DGS accepts."""
    for candidate in (root / "sparse" / "0", root / "sparse", root / "colmap" / "sparse" / "0"):
        if (candidate / "cameras.bin").exists() or (candidate / "cameras.txt").exists():
            return candidate
    return None


def detect(root: Path) -> Scene:
    """Identify a scene directory without modifying it."""
    root = Path(root).expanduser().resolve()
    colmap = _colmap_dir(root)

    frame_dirs = tuple(
        sorted(p for p in root.glob("frame*") if p.is_dir() and p.name[5:].isdigit())
    )
    if frame_dirs:
        return Scene(root, Layout.COLMAP_FRAMES, len(frame_dirs), frame_dirs, colmap)

    # DyNeRF ships one mp4 per camera at the scene root; Immersive ships the same
    # thing plus a models.json describing the rig. The distinction matters because
    # Immersive's cameras are fisheye and need undistortion before training.
    videos = sorted(root.glob("cam*.mp4"))
    if (root / "models.json").exists():
        return Scene(root, Layout.IMMERSIVE, len(videos), colmap=colmap)
    if videos:
        return Scene(root, Layout.DYNERF, len(videos), colmap=colmap)

    # Already-extracted DyNeRF: per-camera image directories rather than videos.
    cam_dirs = sorted(p for p in root.glob("cam*") if p.is_dir())
    if cam_dirs:
        return Scene(root, Layout.DYNERF, len(cam_dirs), colmap=colmap)

    return Scene(root, Layout.UNKNOWN, 0, colmap=colmap)


def describe(scene: Scene) -> str:
    """One-line summary for the CLI."""
    parts = [f"layout={scene.layout.value}", f"frames={scene.frame_count}"]
    parts.append(f"colmap={scene.colmap.relative_to(scene.root) if scene.colmap else 'MISSING'}")
    return "  ".join(parts)
