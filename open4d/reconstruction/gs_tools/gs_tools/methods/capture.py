"""The captured images, as a method in their own right.

Every other adapter here reconstructs; this one just reads the photographs back
out of the corpus. It is what turns a comparison from "which of these two
renders do you prefer" into "which is closer to what the camera saw", so it is
worth having even though it computes nothing.

One clip per rig station, because a captured image only exists where a camera
was. That is also why `gs_tools.cameras` makes the rig the canonical path: at
those eight poses, and only there, every method can be lined up against the
truth.

A caveat that matters for how far these numbers can be pushed: for the runs in
this repository *all eight cameras were training views* for both Vega and ReRF
(`nevo_corpus.json` records no held-out view, and Vega's photometric refinement
fits all eight). So a comparison here measures reconstruction, not
generalisation. A real held-out view means re-preparing the corpus with a
holdout and retraining.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from .. import cameras
from streamer import bundle

name = "captured"

#: Where the ORBIT Gaussian-training corpus lives on the machine that built it.
DEFAULT_CORPUS = Path("/media/frozzzen/DataDrive/ORBIT_datasets_gaussian")


@dataclass
class CaptureOptions:
    """What to pull out of the corpus."""

    #: Object names, when the input is a whole corpus; empty means all of them.
    objects: tuple[str, ...] = ()
    #: Rig view ids to export; empty means every station.
    views: tuple[int, ...] = ()
    #: How many frames; None means all of them.
    frames: int | None = None
    #: Longest edge of the exported image. The corpus is 4096x3072, which is
    #: both far more than a comparison pane shows and ~1.5 MB a frame.
    max_width: int = 1024
    quality: int = 90
    fps: int = 30
    extra: dict[str, Any] = field(default_factory=dict)


def scenes_in(corpus: Path | str) -> list[str]:
    """Object names in an ORBIT corpus, from its own index."""
    corpus = Path(corpus).expanduser().resolve()
    index = corpus / "dataset.json"
    if index.is_file():
        return [entry["name"] for entry in json.loads(index.read_text()).get("objects", [])]
    return sorted(p.name for p in corpus.iterdir() if p.is_dir())


def frame_dirs(scene_dir: Path) -> list[Path]:
    """The corpus's per-frame directories, in time order.

    Named `frame_000001` upward -- one-based, unlike every index this module
    hands out, so the offset is resolved here and nowhere else.
    """
    return sorted(
        (p for p in scene_dir.glob("frame_*") if p.is_dir() and p.name[6:].isdigit()),
        key=lambda p: int(p.name[6:]),
    )


def build_clips(
    source: Path | str, out_dir: Path | str, options: CaptureOptions | None = None
) -> tuple[str, list[bundle.Clip], dict[str, Any]]:
    """Copy captured views into ``out_dir``, one clip per scene per station.

    ``source`` is either one object's directory or a whole corpus, in which case
    every object in it is exported (or the subset in ``options.objects``).
    """
    options = options or CaptureOptions()
    source = Path(source).expanduser().resolve()
    out_dir = Path(out_dir).expanduser().resolve()

    if (source / "dataset.json").is_file():
        wanted_objects = set(options.objects) if options.objects else None
        scenes = [
            source / entry
            for entry in scenes_in(source)
            if wanted_objects is None or entry in wanted_objects
        ]
        if not scenes:
            raise ValueError(
                f"{source} has none of {', '.join(sorted(options.objects))}; it has "
                + ", ".join(scenes_in(source))
            )
        clips: list[bundle.Clip] = []
        for scene in scenes:
            _, produced, _ = _build_scene(scene, out_dir, options)
            clips += produced
        title = "Captured — " + ", ".join(scene.name for scene in scenes)
        return title, clips, {"baseline": name, "corpus": str(source)}

    return _build_scene(source, out_dir, options)


def _build_scene(
    scene_dir: Path, out_dir: Path, options: CaptureOptions
) -> tuple[str, list[bundle.Clip], dict[str, Any]]:
    """One object's captured views, one clip per rig station."""
    from PIL import Image

    scene_dir = Path(scene_dir)
    out_dir = Path(out_dir)
    rig = cameras.read_orbit_rig(scene_dir)
    frames = frame_dirs(scene_dir)
    if not frames:
        raise FileNotFoundError(f"{scene_dir} holds no frame_NNNNNN directories")
    if options.frames is not None:
        frames = frames[: options.frames]

    wanted = set(options.views) if options.views else {pose.view_id for pose in rig.poses}
    clips: list[bundle.Clip] = []
    for pose in rig.poses:
        if pose.view_id not in wanted:
            continue
        frames_at = bundle.frame_dir(out_dir, f"{rig.scene}-captured-view{pose.view_id:02d}")
        written: list[str] = []
        for index, frame in enumerate(frames):
            source = frame / "images" / f"view_{pose.view_id:02d}.png"
            if not source.is_file():
                raise FileNotFoundError(f"missing captured view: {source}")
            image = Image.open(source).convert("RGB")
            if image.width > options.max_width:
                height = round(image.height * options.max_width / image.width)
                image = image.resize((options.max_width, height), Image.LANCZOS)
            destination = frames_at / f"frame_{index:04d}.jpg"
            image.save(destination, quality=options.quality)
            written.append(str(destination.relative_to(out_dir)))
        clips.append(
            bundle.Clip(
                name=frames_at.name,
                representation="pixels",
                scene=rig.scene,
                method=name,
                camera=pose.view_id,
                frames=written,
                notes=[
                    f"captured by rig camera {pose.view_id}, downscaled to "
                    f"{options.max_width}px — this is the reference, not a reconstruction",
                    "a training view for both Vega and ReRF: this measures "
                    "reconstruction, not generalisation",
                ],
                detail={"source": str(scene_dir), "view_id": pose.view_id},
            )
        )
        print(f"      {frames_at.name}: {len(written)} frames", flush=True)

    return f"Captured — {rig.scene}", clips, {"baseline": name, "corpus": str(scene_dir.parent)}


def rigs_for(corpus: Path | str, scenes: list[str]) -> dict[str, Any]:
    """Each named scene's rig, for the bundle's `scenes` map.

    Looked up here rather than by each exporter: Vega and ReRF know which subject
    they reconstruct but not which cameras captured it, and the whole point of a
    shared camera is that the answer does not depend on who is asking.
    """
    corpus = Path(corpus).expanduser().resolve()
    found: dict[str, Any] = {}
    for scene in scenes:
        directory = corpus / scene
        if not directory.is_dir():
            continue
        try:
            found[scene] = cameras.read_orbit_rig(directory).as_dict()
        except (FileNotFoundError, KeyError, ValueError):
            continue
    return found


def export(source: Path | str, out_dir: Path | str, options: CaptureOptions | None = None) -> Path:
    """Export captured views into a viewable bundle at ``out_dir``."""
    options = options or CaptureOptions()
    out_dir = Path(out_dir).expanduser().resolve()
    source = Path(source).expanduser().resolve()
    title, clips, detail = build_clips(source, out_dir, options)
    corpus = source if (source / "dataset.json").is_file() else source.parent
    scenes = sorted({clip.scene for clip in clips if clip.scene})
    bundle.write(
        out_dir, title=title, source=source, clips=clips, fps=options.fps,
        scenes=rigs_for(corpus, scenes), detail=detail,
    )
    return out_dir
