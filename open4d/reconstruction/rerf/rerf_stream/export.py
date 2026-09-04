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

#: JPEG quality for the written frames. High, because these are the reference
#: renders a method is judged by -- compression artefacts here would be read as
#: reconstruction artefacts.
QUALITY = 94


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
            "method": "rerf", "camera": view, "depth": False, "frames": [],
        }
        if args.depth:
            clips[f"{args.name}-rerf-cam{view:02d}-depth"] = {
                "method": "rerf-depth", "camera": view, "depth": True, "frames": [],
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
            relative = f"{name}/frame_{position:04d}.jpg"
            total_bytes += write_jpeg(out / relative, colour, args.quality)
            clips[name]["frames"].append(relative)
            if args.depth:
                name = f"{args.name}-rerf-cam{view:02d}-depth"
                relative = f"{name}/frame_{position:04d}.jpg"
                total_bytes += write_jpeg(out / relative, depth, args.quality)
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
        "clips": [
            {
                "name": name,
                "method": clip["method"],
                "camera": clip["camera"],
                "frames": clip["frames"],
                "notes": shared_notes + (
                    ["relative depth in ray-march steps, near bright — not a "
                     "distance in world units"] if clip["depth"] else []
                ),
                "detail": {
                    "source": str(player.path),
                    "view": clip["camera"],
                    "resolution": f"{camera.width}x{camera.height}",
                    "renderer": "rerf_stream.export",
                },
            }
            for name, clip in clips.items()
        ],
    }
    (out / "clips.json").write_text(json.dumps(payload, indent=2) + "\n")

    elapsed = time.time() - started
    written = sum(len(clip["frames"]) for clip in clips.values())
    print(
        f"\n{written} frames across {len(clips)} clips in {elapsed:.0f}s "
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
    parser.add_argument("--quality", type=int, default=QUALITY)
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
