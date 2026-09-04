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
    3DGS PLY or ``.splat`` per frame (see ``gs_tools.io``). Free camera in the
    viewer.
``mesh``
    ``.ply`` or Draco per frame, which is what `streamer.export` writes for any
    sequence `open4d.load` reads. Free camera.
``points``
    The same two formats, drawn as points rather than a surface. Free camera.
``pixels``
    Pre-rendered images per frame, or a live MJPEG stream (`streamer.live`), for
    a representation that cannot be decoded in the consumer's process at all.
    The camera is whatever the renderer used, and that is recorded in ``notes``.

Which suffixes actually travel for each of these is `streamer.codecs`, not this
module: a bundle records what a frame *is*, and the codec registry records what
it is *in*.

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


@dataclass(frozen=True)
class Variant:
    """One quality level of a clip: the same content at a different rate.

    A clip is "this method's output, for this subject, at this station". A
    variant is one of the ways to *get* it. They live inside the clip rather
    than as extra clips because a consumer has to be able to change its mind
    between them mid-playback -- three sibling clips would be three panes, and
    nothing could switch.

    ``bytes`` and ``quality`` are **measured, not predicted.** A research
    system that has to choose before encoding must model them; a bundle holds
    content that already exists, so there is nothing to model. Recording a
    prediction here would be strictly worse data than the file sizes on disk
    and a score against the reference.

    Every variant carries the same number of frames as the clip's default, for
    the same reason a video's renditions share a timeline: something switching
    at frame *n* has to land on frame *n*, not jump in time.
    """

    #: Rung id, e.g. "high". Unique within a clip, and stable across scenes so
    #: a consumer can ask for the same rung of everything.
    name: str
    #: This rung's frames, in playback order, relative to the bundle root.
    frames: list[str] = field(default_factory=list)
    #: Total bytes of those frames, measured. Divided by the clip's duration
    #: this is the rung's bitrate, which is what a chooser spends.
    bytes: int = 0
    #: What it looks like, measured against the reference: ``{"psnr": ...,
    #: "ssim": ...}``. Empty when nothing has scored it yet, which is honest --
    #: an unscored rung should not be presented as if its quality were known.
    quality: dict[str, float] = field(default_factory=dict)
    #: How this rung was made: resolution, codec settings, whatever a reader
    #: needs to reproduce it.
    detail: dict[str, Any] = field(default_factory=dict)

    def as_dict(self) -> dict[str, Any]:
        return dataclasses.asdict(self)

    @property
    def bytes_per_frame(self) -> float:
        return self.bytes / len(self.frames) if self.frames else 0.0

    def bitrate(self, fps: int) -> float:
        """Bits per second at ``fps``. What a chooser is actually spending."""
        return self.bytes_per_frame * 8 * fps


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
    #: Quality levels this clip is also available at, as `Variant` mappings.
    #: Empty for a clip with one rendition, which is every clip written before
    #: this field existed.
    #:
    #: ``frames`` above stays the clip's *default* rendition rather than moving
    #: into here, so a reader that knows nothing about variants plays the clip
    #: correctly and needs no changes. That is why adding this did not bump
    #: `VERSION`: it is additive, and an old reader is not wrong, just
    #: unadaptive.
    variants: list[dict[str, Any]] = field(default_factory=list)
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
        if clip.stream.get("origin") not in ("rendered", "replay"):
            raise ValueError(
                f"{clip.name}: stream needs an origin of 'rendered' or 'replay' "
                "— the transport does not say whether pixels are being computed "
                "now or replayed, and presenting a replay as live is a claim the "
                "software does not support"
            )
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

    if clip.variants:
        if clip.stream is not None:
            raise ValueError(
                f"{clip.name}: a live clip cannot have variants -- there is no "
                "frame list to offer at another quality"
            )
        seen = set()
        for entry in clip.variants:
            if not isinstance(entry, Mapping) or not entry.get("name"):
                raise ValueError(f"{clip.name}: every variant needs a name")
            name = entry["name"]
            if name in seen:
                raise ValueError(
                    f"{clip.name}: two variants are both named {name!r}; a "
                    "consumer asking for that rung would get whichever came first"
                )
            seen.add(name)
            frames = entry.get("frames") or []
            if not frames:
                raise ValueError(f"{clip.name}: variant {name!r} has no frames")
            if len(frames) != len(clip.frames):
                raise ValueError(
                    f"{clip.name}: variant {name!r} has {len(frames)} frames "
                    f"against the clip's {len(clip.frames)}. Renditions share a "
                    "timeline, so switching at frame n has to land on frame n"
                )
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


def variants_of(clip: Clip | Mapping[str, Any]) -> tuple[Variant, ...]:
    """A clip's quality levels, as `Variant` objects, cheapest first.

    Ordered by measured bytes so a caller walking the list is walking the rate
    ladder, rather than whatever order a producer happened to write.
    """
    raw = clip.variants if isinstance(clip, Clip) else clip.get("variants") or []
    found = [
        Variant(
            name=entry["name"],
            frames=list(entry.get("frames") or ()),
            bytes=int(entry.get("bytes") or 0),
            quality=dict(entry.get("quality") or {}),
            detail=dict(entry.get("detail") or {}),
        )
        for entry in raw
    ]
    return tuple(sorted(found, key=lambda variant: (variant.bytes, variant.name)))


def variant(clip: Clip | Mapping[str, Any], name: str) -> Variant:
    """One named rung of a clip, or a KeyError naming what it does offer."""
    available = variants_of(clip)
    for found in available:
        if found.name == name:
            return found
    offered = ", ".join(item.name for item in available)
    raise KeyError(
        f"no variant named {name!r}; this clip "
        + (f"offers {offered}" if available else "has one rendition")
    )


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


def add(
    out_dir: Path | str,
    clips: "Clip | list[Clip]",
    *,
    replace: bool = False,
) -> Path:
    """Add clips to a bundle that already exists, and rewrite its index.

    The workflow this exists for: content is produced after the bundle was
    written -- a renderer starts, or an exporter finishes a second method --
    and re-exporting the whole bundle to include it would re-copy every frame
    of everything already there.

    ``clips`` may be one clip or many, and many is not a convenience: a
    16-clip export would otherwise rewrite the manifest 16 times, and a reader
    that loaded it midway through would see a partial bundle.

    Every clip is validated on the way out, not just the new ones. A rewrite is
    a write, and writing back a manifest whose older clips no longer satisfy
    the invariants would be a silent downgrade.

    Note for a running server: `streamer.server.serve` reads live upstreams
    once, at startup, so it must be restarted before it will proxy a newly
    added live clip. Deliberate -- re-reading per request would make every
    frame fetch depend on a file a producer may be halfway through writing.
    """
    root = Path(out_dir)
    index = read(root)
    if not index:
        raise FileNotFoundError(f"{root} has no {INDEX_NAME}")

    incoming = [clips] if isinstance(clips, Clip) else list(clips)
    names = [clip.name for clip in incoming]
    duplicated = {name for name in names if names.count(name) > 1}
    if duplicated:
        raise ValueError(
            f"two incoming clips are both named {', '.join(sorted(duplicated))}; "
            "one would overwrite the other's entry"
        )

    existing = [Clip(**entry) for entry in index.get("clips", [])]
    taken = sorted({clip.name for clip in existing} & set(names))
    if taken and not replace:
        raise ValueError(
            f"{root} already has a clip named {', '.join(taken)}; pass "
            "replace=True to swap them, which is what re-exporting needs"
        )
    kept = [clip for clip in existing if clip.name not in set(names)]

    return write(
        root,
        title=index.get("title", root.name),
        source=index.get("source", str(root)),
        clips=kept + incoming,
        fps=index.get("fps", 30),
        scenes=index.get("scenes") or {},
        detail=index.get("detail"),
    )


def read(out_dir: Path | str) -> dict[str, Any]:
    """The bundle index, or an empty dict if the directory has none."""
    path = Path(out_dir) / INDEX_NAME
    if not path.is_file():
        return {}
    return json.loads(path.read_text())
