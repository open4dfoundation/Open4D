#!/usr/bin/env python
"""Take prepared frames a method exported, and put them in a bundle.

Some methods cannot be driven from this package at all. ReRF's entropy coder is
a prebuilt Python 3.8 binary, so `rerf_stream` runs on 3.8 while everything here
needs 3.10 -- neither side can import the other. Rather than force one
interpreter on both, the exporting side writes its frames plus a ``clips.json``
saying what they are, and this reads that.

``clips.json`` is deliberately smaller than `bundle`'s own manifest: a method
should have to state what it rendered, not learn this repository's clip schema.
It carries a scene, a representation, and per clip a name, a method, a camera
index, an ordered frame list and whatever notes the renderer wants shown::

    {"format": "rerf-clips", "version": 1, "scene": "basketball",
     "representation": "pixels",
     "clips": [{"name": ..., "method": ..., "camera": 0,
                "frames": ["<name>/frame_0000.jpg", ...],
                "notes": [...], "detail": {...}}]}

    python -m streamer.adopt ~/rerf-clips --bundle ~/open4d-view
"""
from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path

from . import bundle

INDEX_NAME = "clips.json"

#: Formats this understands. A version bump means a shape change, and refusing
#: an unknown one beats importing half of it.
SUPPORTED = {("rerf-clips", 1)}


def read_export(directory: Path | str) -> dict:
    """The sidecar an exporter wrote, checked enough to fail usefully."""
    path = Path(directory).expanduser().resolve() / INDEX_NAME
    if not path.is_file():
        raise FileNotFoundError(
            f"{path} is missing; run the method's exporter first (for ReRF, "
            "`python -m rerf_stream.export`)"
        )
    payload = json.loads(path.read_text())
    key = (payload.get("format"), payload.get("version"))
    if key not in SUPPORTED:
        raise ValueError(
            f"{path}: {key} is not a format this understands "
            f"({', '.join(sorted(str(k) for k in SUPPORTED))})"
        )
    if not payload.get("clips"):
        raise ValueError(f"{path} lists no clips")
    return payload


def adopt(
    directory: Path | str,
    bundle_dir: Path | str,
    *,
    replace: bool = False,
    copy: bool = True,
) -> list:
    """Copy an export's frames into ``bundle_dir`` and add its clips.

    Frames are copied rather than referenced. A bundle is a directory that can
    be served, moved or fetched whole -- ``transfer.fetch`` walks the paths the
    manifest names -- so a clip pointing outside it would be a bundle that
    works on one machine only.

    Every frame the sidecar names is checked to exist before anything is
    written. A clip whose frames are half there renders a pane that plays for
    two seconds and then 404s, which is a worse failure than not importing it.
    """
    directory = Path(directory).expanduser().resolve()
    bundle_dir = Path(bundle_dir).expanduser().resolve()
    payload = read_export(directory)

    def declared(entry):
        """Every frame path a clip entry names, across all its renditions."""
        yield from entry["frames"]
        for rung in entry.get("variants") or []:
            yield from rung.get("frames") or ()

    missing = [
        relative
        for entry in payload["clips"]
        for relative in declared(entry)
        if not (directory / relative).is_file()
    ]
    if missing:
        raise FileNotFoundError(
            f"{len(missing)} frame(s) the sidecar names are not on disk, e.g. "
            f"{missing[0]}; the export did not finish"
        )

    clips = []
    for entry in payload["clips"]:
        if copy:
            for relative in declared(entry):
                target = bundle_dir / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(directory / relative, target)
        clips.append(
            bundle.Clip(
                name=entry["name"],
                representation=payload.get("representation", "pixels"),
                scene=payload.get("scene"),
                method=entry.get("method"),
                camera=entry.get("camera"),
                frames=list(entry["frames"]),
                # Carried through when the export measured them. A geometry
                # clip that arrives without its counts and bounds shows a blank
                # size in the pane table and gives the viewer nothing to frame
                # the camera against -- both of which read as the clip being
                # broken rather than under-described.
                counts=list(entry.get("counts") or []),
                bounds_min=entry.get("bounds_min"),
                bounds_max=entry.get("bounds_max"),
                variants=list(entry.get("variants") or []),
                notes=list(entry.get("notes") or []),
                detail=dict(entry.get("detail") or {}),
            )
        )

    # The scene's camera rig, if the export knows it and the bundle has no real
    # one. Without a rig a viewer cannot offer station selection for this scene,
    # so its panes are shown but not comparable to each other by pose.
    #
    # A rig that already has poses is left alone: it may have come from a
    # geometry method, and that is the authority, since its output is what has
    # to line up in 3D. But a scene entry carrying *no* poses is not an
    # authority -- it is a placeholder, and filling it is the whole point.
    rig = payload.get("rig")
    scene = payload.get("scene")
    if rig and scene:
        index = bundle.read(bundle_dir)
        scenes = dict(index.get("scenes") or {})
        if not (scenes.get(scene) or {}).get("poses"):
            scenes[scene] = dict(rig, scene=scene)
            bundle.write(
                bundle_dir,
                title=index.get("title", Path(bundle_dir).name),
                source=index.get("source", str(bundle_dir)),
                clips=[bundle.Clip(**entry) for entry in index.get("clips", [])],
                fps=index.get("fps", 30),
                scenes=scenes,
                detail=index.get("detail"),
            )

    bundle.add(bundle_dir, clips, replace=replace)
    return clips


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("directory", help="an export directory holding clips.json")
    parser.add_argument("--bundle", required=True, help="the bundle to add them to")
    parser.add_argument("--replace", action="store_true",
                        help="overwrite clips of the same name, for a re-export")
    parser.add_argument("--no-copy", action="store_true",
                        help="add the clips without copying frames; only useful when "
                             "the export was written inside the bundle already")
    args = parser.parse_args(argv)

    clips = adopt(
        args.directory, args.bundle, replace=args.replace, copy=not args.no_copy
    )
    total = sum(len(clip.frames) for clip in clips)
    print(f"added {len(clips)} clips ({total} frames) to {args.bundle}")
    for clip in clips:
        print(f"  {clip.name:<34} {clip.method:<12} camera {clip.camera}")
    print("\nrestart the bundle server to pick these up")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
