"""The index file that makes a directory of frames viewable.

An exporter normalises unrelated output containers into one shape: a directory
of per-frame files plus a ``view.json`` describing them. The client in
``streamer.client`` reads only this, so it never has to know that one clip came
out of a Vega hash grid and the next out of ReRF's volume renderer.

This module is the contract *between* the client and the server and belongs to
neither, which is why it sits above both.

A bundle carries *clips* rather than a single frame list because the outputs it
wraps are naturally plural -- a Vega catalog holds nine independently encoded
objects, a ReRF run holds a bitstream and a white-background variant of it -- and
flattening them would lose which frame belongs to what. Every clip declares its
**representation** -- what a decoded frame *is* -- using the vocabulary of
`open4d.core.Representation`, so that this manifest and Open4D's own sequence
model name the same things the same way:

``gaussians``
    3DGS PLY per frame (see ``gs_tools.io.ply``). Free camera in the viewer.
``pixels``
    Pre-rendered images per frame, for a representation that cannot be decoded
    in the consumer's process at all. The camera is whatever the renderer used,
    and that is recorded in ``notes``.

``mesh`` and ``points`` are part of the same vocabulary and are what a mesh or
point-cloud exporter would write; the bundled viewer has no renderer for them
yet and says so rather than showing an empty pane.

Paths in ``frames`` are relative to the bundle root, so the whole directory can
be moved or served over HTTP unchanged.
"""

from __future__ import annotations

import dataclasses
import json
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Mapping

from open4d.core import Dependency, DependencyMode

from ._revision import open4d_revision

INDEX_NAME = "view.json"

#: Bumped when the shape changes in a way the bundled viewer must notice.
#: v2 replaced a clip's ``kind`` ("splats"/"images") with ``representation``,
#: sharing `open4d.core.Representation`'s vocabulary. The viewer still reads v1
#: by migrating the old field on load.
VERSION = 2


@dataclass
class Clip:
    """One playable sequence inside a bundle."""

    name: str
    #: An `open4d.core.Representation` value: "gaussians", "pixels", "mesh" or
    #: "points". What a decoded frame is, which is what decides the renderer and
    #: whether a free camera is meaningful -- deliberately not the codec that
    #: produced it, so one renderer serves every codec of a given shape.
    representation: str
    #: The subject this reconstructs, shared across methods. What lets the viewer
    #: put two methods of the same thing side by side rather than listing them.
    scene: str | None = None
    #: Which method produced it: "vega", "rerf", "captured", "queen", ...
    method: str | None = None
    #: For a fixed-camera clip, the rig station it was rendered (or captured)
    #: from -- an index into the scene's `poses`. None for a splat clip, which
    #: can be rendered from anywhere and so belongs to no single station.
    camera: int | None = None
    #: Frame files, in playback order, relative to the bundle root.
    frames: list[str] = field(default_factory=list)
    #: Gaussians per frame, for "gaussians"; empty for "pixels".
    counts: list[int] = field(default_factory=list)
    #: Axis-aligned world bounds over the whole clip, for the initial camera.
    bounds_min: list[float] | None = None
    bounds_max: list[float] | None = None
    #: What a viewer of this clip needs to be told -- a baked colour direction,
    #: a fixed render camera, a quality caveat. Shown in the viewer's UI.
    notes: list[str] = field(default_factory=list)
    #: A live transport instead of a frame list: ``{"url": ..., "protocol":
    #: "mjpeg"}``. Set for a clip that is rendered as it is watched rather than
    #: read off disk, in which case ``frames`` is empty and there is nothing to
    #: scrub. See `streamer.live`.
    stream: dict[str, Any] | None = None
    #: How this clip's frames depend on one another, in the vocabulary of
    #: `open4d.core.Dependency`: ``{"mode": ..., "key_frames": [...]}``. Absent
    #: means independent, which is what a directory of whole frames is and what
    #: every exporter in this repository currently writes -- they decode before
    #: they write. A producer that serves a bitstream *as* the frames, rather
    #: than decoding it first, is what this field exists for.
    dependency: dict[str, Any] | None = None
    #: Anything method-specific worth keeping; not interpreted by the viewer.
    detail: dict[str, Any] = field(default_factory=dict)


#: Live transports the packaged client can play.
STREAM_PROTOCOLS = ("mjpeg",)


def validate(clip: Clip) -> Clip:
    """Check the invariants a clip has to satisfy, and return it.

    Called by :func:`write`, because the failures here are otherwise silent: a
    clip with neither frames nor a stream renders an empty pane, and a stream
    with a protocol the client does not know renders nothing at all -- in both
    cases with no error anywhere a producer would see it.
    """
    if clip.stream is not None:
        if not isinstance(clip.stream, Mapping) or not clip.stream.get("url"):
            raise ValueError(f"{clip.name}: stream needs a url")
        protocol = clip.stream.get("protocol")
        if protocol not in STREAM_PROTOCOLS:
            raise ValueError(
                f"{clip.name}: stream protocol {protocol!r} is not one the client "
                f"plays ({', '.join(STREAM_PROTOCOLS)})"
            )
        if clip.frames:
            raise ValueError(
                f"{clip.name}: a live clip has no frame list -- it is rendered as "
                "it is watched, so there is nothing to scrub"
            )
    elif not clip.frames:
        raise ValueError(f"{clip.name}: needs either frames or a stream")
    return clip


def dependency_field(dependency: Dependency | None) -> dict[str, Any] | None:
    """A `Dependency` as the manifest stores it, or None when it is the default.

    Independent is omitted rather than written out, so a manifest only carries
    the field when it says something: a reader treating absent as independent
    and a writer omitting the default cannot disagree.
    """
    if dependency is None or dependency.mode is DependencyMode.INDEPENDENT:
        return None
    return {
        "mode": dependency.mode.value,
        "key_frames": list(dependency.key_frames),
    }


def dependency_of(clip: Clip | Mapping[str, Any]) -> Dependency:
    """The `Dependency` a clip declares, defaulting to independent.

    Accepts a `Clip` or the plain mapping a manifest holds, so a consumer that
    read ``view.json`` back does not have to rebuild the dataclass first.
    """
    raw = clip.dependency if isinstance(clip, Clip) else clip.get("dependency")
    if not raw:
        return Dependency()
    mode = DependencyMode(raw["mode"])
    keys = tuple(raw.get("key_frames") or ())
    return Dependency(mode=mode, key_frames=keys if mode is DependencyMode.GOP else ())


def frame_dir(out_dir: Path | str, name: str) -> Path:
    """A fresh directory for one clip's frames, under ``out_dir``.

    Suffixed if the name is taken, because a bundle can be built from several
    sources at once and two of them may name a clip the same way -- and the
    failure mode of not checking is one clip silently overwriting another's
    frames while both remain listed in the index.
    """
    out_dir = Path(out_dir)
    candidate = out_dir / name
    suffix = 2
    while candidate.exists() and any(candidate.iterdir()):
        candidate = out_dir / f"{name}-{suffix}"
        suffix += 1
    candidate.mkdir(parents=True, exist_ok=True)
    return candidate


def write(
    out_dir: Path | str,
    *,
    title: str,
    source: Path | str,
    clips: list[Clip],
    fps: int = 30,
    scenes: dict[str, Any] | None = None,
    detail: dict[str, Any] | None = None,
) -> Path:
    """Write ``view.json`` for a bundle whose frame files are already in place.

    ``scenes`` maps a scene name to its capture rig (see `gs_tools.cameras`).
    That is what makes a shared camera possible: a splat clip can be rendered at
    a rig station, and an image clip captured or rendered at that same station
    can be shown beside it.
    """
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    for clip in clips:
        validate(clip)
    index = {
        "version": VERSION,
        "title": title,
        "source": str(source),
        "fps": fps,
        "created": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "revision": {"open4d": open4d_revision()},
        "clips": [dataclasses.asdict(clip) for clip in clips],
        "scenes": scenes or {},
    }
    if detail:
        index["detail"] = detail
    path = out_dir / INDEX_NAME
    path.write_text(json.dumps(index, indent=2) + "\n")
    return path


def read(out_dir: Path | str) -> dict[str, Any]:
    """The bundle index, or an empty dict if the directory has none."""
    path = Path(out_dir) / INDEX_NAME
    if not path.is_file():
        return {}
    return json.loads(path.read_text())
