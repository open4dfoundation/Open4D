#!/usr/bin/env python
"""Render a bitstream to prepared image clips, for side-by-side comparison.

`serve` streams ReRF live, which is the honest demonstration but a moving
target: it renders whatever camera the loop is on, at whatever rate the GPU
manages. A comparison needs the opposite -- the same viewpoints as every other
method, every frame present, scrubbable, and identical on every reload. So this
writes them to disk once.

One decode serves every camera. The decode is sequential and costs ~25 ms;
a ray-march costs ~90 ms per view. So the loop is *decode a frame, then render
all the views of it*, which for 8 views and 30 frames is 30 decodes rather than
240 -- about 25 seconds of work instead of two minutes.

**Rendered at the corpus's own resolution and intrinsics**, which is a
deliberate change from what upstream's render script produces. Upstream's NHR
loader resizes the training images to 1920x1080 and scales the focal length by
the *height* ratio, so a 4:3 corpus comes out letterboxed into 16:9 -- the pose
is right and the framing is not, and the previously exported clips carried a
note apologising for it. Reading the camera straight from ``cams_<n>.json``
gives 1280x960 here, matching the photographs the renders are compared against.

**Re-rendering is reproducible but not bit-identical.** The ray-march sums
along each ray with CUDA reductions, whose accumulation order is not fixed, so
two runs of the same frame at the same camera can differ in the last level of a
few pixels. Measured by re-exporting ``g_basketball``: 73 of 480 frames
differed, by at most 2 levels of 255 on 18-185 of 3.7 million samples --
0.003%, which moves PSNR by far less than 0.01 dB. Worth knowing before
diffing two exports and concluding something changed.

Two environments, one handoff. This runs on Python 3.8 because ReRF's entropy
coder does; `streamer` needs 3.10. So this writes the frames plus a
``clips.json`` describing them, and the bundle side reads that. Neither imports
the other.

    python -m rerf_stream.export \\
        --config <run>/config.py --compression-path <run>/rerf \\
        --out ~/rerf-clips --scene basketball --name g_basketball

Then, in the 3.10 environment::

    python -m streamer.adopt ~/rerf-clips --bundle ~/open4d-view
"""
from __future__ import annotations

import argparse
import json
import shutil
import time
from pathlib import Path

import numpy as np

from .bitstream import BitstreamPlayer
from .serve import RUNGS
from .cameras import capture_rig, captured_image

#: What each kind of clip needs its viewer told, beyond the shared notes.
KIND_NOTES = {
    "colour": [],
    "depth": [
        "relative depth in ray-march steps, near bright — not a distance in "
        "world units",
    ],
    "captured": [
        "the photograph from this rig camera, composited onto the same "
        "background as the render — this is the reference, not a reconstruction",
        "a training view: this measures reconstruction, not generalisation",
    ],
}

#: JPEG quality for the written frames. High, because these are the reference
#: renders a method is judged by -- compression artefacts here would be read as
#: reconstruction artefacts.
QUALITY = 94


def _resample(image: np.ndarray, scale: float) -> np.ndarray:
    """``image`` at a fraction of its size, for a lower rung.

    Resampled rather than re-marched. A second ray-march at a lower resolution
    would sample the volume differently and produce a slightly different
    picture -- fine as an image, wrong as a *rendition*, because two renditions
    have to be the same content for switching between them to be seamless
    rather than a visible cut.
    """
    from PIL import Image

    if scale == 1.0:
        return image
    height, width = image.shape[:2]
    size = (max(16, int(round(width * scale))), max(16, int(round(height * scale))))
    array = (np.clip(image, 0.0, 1.0) * 255.0).astype(np.uint8)
    resized = Image.fromarray(array).resize(size, Image.LANCZOS)
    return np.asarray(resized, np.float32) / 255.0


def write_jpeg(path: Path, image: np.ndarray, quality: int = QUALITY) -> int:
    """Write ``image`` in [0, 1] as JPEG, returning bytes written."""
    from PIL import Image

    array = (np.clip(image, 0.0, 1.0) * 255.0).astype(np.uint8)
    if array.ndim == 2:
        array = np.repeat(array[..., None], 3, axis=2)
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(array).save(path, format="JPEG", quality=quality)
    return path.stat().st_size


def run(args) -> int:
    player = BitstreamPlayer(
        args.config,
        args.compression_path,
        pca=not args.no_pca,
        pca_channels=tuple(int(c) for c in args.pca_chs.split(",")),
        group_size=args.group_size or None,
    )
    rungs = (
        {"default": (args.scale, args.quality)} if not args.rungs
        else {name: RUNGS[name] for name in args.rungs.split(",")}
    )
    unknown = set(rungs) - set(RUNGS) - {"default"}
    if unknown:
        raise SystemExit(
            f"--rungs {','.join(sorted(unknown))}: known rungs are "
            f"{', '.join(sorted(RUNGS))}"
        )
    # The best rung is the clip's default rendition, so a reader that knows
    # nothing about variants sees the method at its best rather than at
    # whatever happened to be listed first.
    default = max(rungs, key=lambda name: rungs[name][0])
    cameras = [camera.scaled(args.scale) for camera in player.cameras()]
    views = (
        [int(v) for v in args.views.split(",")] if args.views
        else list(range(len(cameras)))
    )
    for view in views:
        if view >= len(cameras):
            raise SystemExit(
                f"--views {view}: this corpus has {len(cameras)} cameras "
                f"(0-{len(cameras) - 1})"
            )
    frames = player.frames if args.frames <= 0 else player.frames[: args.frames]

    out = Path(args.out).expanduser().resolve()
    if out.exists() and args.overwrite:
        shutil.rmtree(out)
    out.mkdir(parents=True, exist_ok=True)

    # name -> (clip metadata, list of relative frame paths)
    clips = {}
    for view in views:
        clips[f"{args.name}-rerf-cam{view:02d}"] = {
            "method": "rerf", "camera": view, "kind": "colour", "frames": [],
        }
        if args.depth:
            clips[f"{args.name}-rerf-cam{view:02d}-depth"] = {
                "method": "rerf-depth", "camera": view, "kind": "depth", "frames": [],
            }
        if args.captured:
            clips[f"{args.name}-captured-cam{view:02d}"] = {
                "method": "captured", "camera": view, "kind": "captured",
                "frames": [],
            }

    started = time.time()
    total_bytes = 0
    decode_s = march_s = 0.0
    for position, frame in enumerate(player.play(loop=False)):
        if position >= len(frames):
            break
        decode_s += frame.decode_seconds
        for view in views:
            began = time.time()
            rendered = player.render(cameras[view], depth=args.depth)
            march_s += time.time() - began
            colour, depth = rendered if args.depth else (rendered, None)

            name = f"{args.name}-rerf-cam{view:02d}"
            for rung, (scale, quality) in rungs.items():
                # One ray-march per view, resampled per rung. The march is the
                # expensive step (~90 ms) and re-marching at a lower resolution
                # would be a *different* render, not the same content at a
                # different rate -- which is what a rendition has to be for
                # switching between them to be seamless.
                image = colour if scale == 1.0 else _resample(colour, scale)
                folder = name if rung == default else f"{name}@{rung}"
                relative = f"{folder}/frame_{position:04d}.jpg"
                written = write_jpeg(out / relative, image, quality)
                total_bytes += written
                if rung == default:
                    clips[name]["frames"].append(relative)
                else:
                    clips[name].setdefault("rungs", {}).setdefault(
                        rung, {"frames": [], "bytes": 0, "scale": scale,
                               "quality": quality})
                    clips[name]["rungs"][rung]["frames"].append(relative)
                    clips[name]["rungs"][rung]["bytes"] += written
                if rung == default:
                    clips[name].setdefault("default_bytes", 0)
                    clips[name]["default_bytes"] += written
            if args.depth:
                name = f"{args.name}-rerf-cam{view:02d}-depth"
                relative = f"{name}/frame_{position:04d}.jpg"
                total_bytes += write_jpeg(out / relative, depth, args.quality)
                clips[name]["frames"].append(relative)
            if args.captured:
                # The photograph this view reconstructs, composited onto the
                # same background the render uses. Written here rather than
                # left to another exporter so a scene arrives comparable: a
                # reconstruction with nothing to compare against is a pane
                # nobody can judge.
                name = f"{args.name}-captured-cam{view:02d}"
                relative = f"{name}/frame_{position:04d}.jpg"
                photo = captured_image(
                    player.corpus_dir, frame.index, view,
                    background=player.background,
                )
                total_bytes += write_jpeg(out / relative, photo, args.quality)
                clips[name]["frames"].append(relative)
        print(f"  frame {frame.index:3d}  {len(views)} views", flush=True)

    camera = cameras[views[0]]
    shared_notes = [
        "ReRF volume render at the scene's own capture camera — the same pose "
        "the photograph and every other method use here",
        f"rendered at the corpus's own {camera.width}x{camera.height} and "
        "intrinsics, so the framing matches the captured pane (upstream's "
        "loader letterboxes to 16:9, which earlier exports inherited)",
        "decoded from the bitstream, not from training checkpoints: this is "
        "what a receiver has",
        f"codec: pca={not args.no_pca} pca_chs={args.pca_chs} "
        f"group_size={player.group_size}",
    ]
    payload = {
        "format": "rerf-clips",
        "version": 1,
        "scene": args.scene,
        "representation": "pixels",
        "source": str(player.path),
        "created": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "resolution": [camera.width, camera.height],
        "bitstream_bytes": player.bitstream_bytes,
        # The scene's camera rig, so a bundle can offer station selection
        # across methods without being told the geometry separately.
        "rig": capture_rig(player.corpus_dir),
        "clips": [
            {
                "name": name,
                "method": clip["method"],
                "camera": clip["camera"],
                "frames": clip["frames"],
                "notes": (
                    KIND_NOTES[clip["kind"]] if clip["kind"] == "captured"
                    else shared_notes + KIND_NOTES[clip["kind"]]
                ),
                "variants": [
                    {
                        "name": rung,
                        "frames": info["frames"],
                        "bytes": info["bytes"],
                        "detail": {
                            "resolution": "%dx%d" % (
                                max(16, int(round(camera.width * info["scale"]))),
                                max(16, int(round(camera.height * info["scale"]))),
                            ),
                            "jpeg_quality": info["quality"],
                            "scale": info["scale"],
                        },
                    }
                    for rung, info in sorted((clip.get("rungs") or {}).items())
                ],
                "detail": {
                    "source": str(player.path),
                    "view": clip["camera"],
                    "resolution": f"{camera.width}x{camera.height}",
                    "renderer": "rerf_stream.export",
                    # What these pixels are of. A depth map is not an attempt
                    # to reproduce the photograph, so scoring it against one
                    # would produce a number (0.4 dB) that reads as total
                    # failure rather than as not applicable. See
                    # streamer.metrics.
                    "depicts": "depth" if clip["kind"] == "depth" else "appearance",
                },
            }
            for name, clip in clips.items()
        ],
    }
    (out / "clips.json").write_text(json.dumps(payload, indent=2) + "\n")

    elapsed = time.time() - started
    written = sum(
        len(clip["frames"]) + sum(
            len(rung["frames"]) for rung in (clip.get("rungs") or {}).values()
        )
        for clip in clips.values()
    )
    ladder = "" if len(rungs) == 1 else f", {len(rungs)} rungs each"
    print(
        f"\n{written} frames across {len(clips)} clips{ladder} in {elapsed:.0f}s "
        f"({total_bytes / 1e6:.1f} MB)\n"
        f"  decode {decode_s:.1f}s over {len(frames)} frames, "
        f"march {march_s:.1f}s over {written} renders\n"
        f"  wrote {out}/clips.json",
        flush=True,
    )
    return 0


def parse_args(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--config", required=True)
    parser.add_argument("--compression-path", required=True)
    parser.add_argument("--out", required=True, help="directory to write clips into")
    parser.add_argument("--name", default="rerf",
                        help="clip name prefix, e.g. the object's name")
    parser.add_argument("--scene", required=True,
                        help="the subject these reconstruct, shared with other methods")
    parser.add_argument("--views", default="",
                        help="comma-separated camera indices; default is every one")
    parser.add_argument("--frames", type=int, default=0, help="0 means all of them")
    parser.add_argument("--scale", type=float, default=1.0,
                        help="fraction of the corpus resolution")
    parser.add_argument("--depth", action="store_true",
                        help="also write a relative depth map per view")
    parser.add_argument("--captured", action="store_true",
                        help="also write the photograph each view reconstructs, so "
                             "the scene arrives with something to compare against")
    parser.add_argument("--quality", type=int, default=QUALITY)
    parser.add_argument("--rungs", default="",
                        help="comma-separated quality levels to write as variants, "
                             f"from: {', '.join(sorted(RUNGS))}. The highest becomes "
                             "the clip's default rendition. Omit for one rendition.")
    parser.add_argument("--overwrite", action="store_true",
                        help="delete --out first, rather than adding to it")
    parser.add_argument("--no-pca", action="store_true")
    parser.add_argument("--pca_chs", default="7,13")
    parser.add_argument("--group-size", type=int, default=0)
    return parser.parse_args(argv)


def main(argv=None) -> int:
    return run(parse_args(argv))


if __name__ == "__main__":
    raise SystemExit(main())
