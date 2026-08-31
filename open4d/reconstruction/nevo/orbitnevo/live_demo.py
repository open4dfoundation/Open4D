"""Play NeVo's output in a browser, the way `orbitvega/live_demo.py` does.

Same last hop as Vega: frames are pushed as JPEG into
`vega.streaming.mjpeg_server`'s `FrameBuffer` and served over
`multipart/x-mixed-replace`, behind a dark single-image status page. Reusing
Vega's server rather than writing another one means the two baselines are
watched through identical machinery, so nothing about the viewer can account
for a difference between them.

The one deliberate difference is what feeds it. Vega renders live on the GPU;
a NeRF frame takes ~0.5 s to ray-march at this resolution, which is not a
playback rate, and holding a GPU for the life of a browser tab is antisocial
on a box that is also training. So this loops the frames
`orbitnevo/render_frames.py` already wrote. The status line says so.

Each streamed frame is a horizontal composite of the conditions -- plain ReRF,
NeVo's visibility-filtered reconstruction, and the captured camera -- at the
same instant and viewpoint, which is the comparison the whole baseline exists
to show.

    python -m orbitnevo.live_demo --object g_basketball --port 8752
"""
from __future__ import annotations

import argparse
import io
import json
import sys
import threading
import time
from pathlib import Path

MODULE_ROOT = Path(__file__).resolve().parents[1]
if str(MODULE_ROOT) not in sys.path:
    sys.path.insert(0, str(MODULE_ROOT))

from vega.streaming.mjpeg_server import FrameBuffer, serve_forever  # noqa: E402

LABEL_HEIGHT = 26


def load_clip(directory: Path, nevo_only: bool = False) -> dict:
    with open(directory / "manifest.json") as handle:
        manifest = json.load(handle)
    conditions = list(manifest.get("conditions") or [])
    if nevo_only:
        # Just the stream NeVo would send: drop plain ReRF and the captured
        # camera, which are the comparison rather than the output.
        conditions = [c for c in conditions if c.get("threshold") is not None]
    else:
        conditions.append(
            {
                "name": "capture",
                "prefix": "reference",
                "label": f"captured camera {manifest['view']}",
                "threshold": None,
                "kept_fraction": None,
            }
        )
    manifest["conditions"] = [
        condition
        for condition in conditions
        if (directory / f"{condition['prefix']}_{manifest['frames'][0]:03d}.png").is_file()
    ]
    manifest["directory"] = directory
    manifest["crop"] = None
    return manifest


def subject_box(manifest: dict, pad: float = 0.12):
    """Crop rectangle around the subject, unioned over the clip's frames.

    Same motivation as Vega's ``--subject-fill``: the corpus frames a whole
    stage, so a 1280x960 view downscaled to a 420 px panel leaves the body a
    thumbnail and the comparison unreadable. Taken from the *captured* frames
    (background is pure white there) and applied identically to every panel, so
    the conditions stay pixel-aligned.
    """
    import numpy as np
    from PIL import Image

    directory = manifest["directory"]
    top, left = np.inf, np.inf
    bottom, right = -np.inf, -np.inf
    height = width = 0
    for frame in manifest["frames"]:
        path = directory / f"reference_{frame:03d}.png"
        if not path.is_file():
            continue
        pixels = np.asarray(Image.open(path).convert("L"))
        height, width = pixels.shape
        rows = np.flatnonzero((pixels < 245).any(axis=1))
        columns = np.flatnonzero((pixels < 245).any(axis=0))
        if rows.size == 0 or columns.size == 0:
            continue
        top, bottom = min(top, rows[0]), max(bottom, rows[-1])
        left, right = min(left, columns[0]), max(right, columns[-1])
    if not np.isfinite(top):
        return None
    margin_y = (bottom - top) * pad
    margin_x = (right - left) * pad
    return (
        int(max(left - margin_x, 0)),
        int(max(top - margin_y, 0)),
        int(min(right + margin_x + 1, width)),
        int(min(bottom + margin_y + 1, height)),
    )


def compose(manifest: dict, frame: int, panel_width: int):
    """One row: every condition at this instant, captioned."""
    from PIL import Image, ImageDraw

    panels = []
    for condition in manifest["conditions"]:
        path = manifest["directory"] / f"{condition['prefix']}_{frame:03d}.png"
        image = Image.open(path).convert("RGB")
        box = manifest.get("crop")
        if box:
            image = image.crop(box)
        height = max(1, round(image.height * panel_width / image.width))
        panels.append((condition, image.resize((panel_width, height), Image.LANCZOS)))

    panel_height = max(image.height for _, image in panels)
    sheet = Image.new(
        "RGB", (panel_width * len(panels), panel_height + LABEL_HEIGHT), (17, 17, 17)
    )
    draw = ImageDraw.Draw(sheet)
    for index, (condition, image) in enumerate(panels):
        left = index * panel_width
        sheet.paste(image, (left, LABEL_HEIGHT))
        caption = condition["label"]
        kept = condition.get("kept_fraction")
        if condition.get("threshold") is not None and kept is not None:
            caption += f"  ({kept * 100:.0f}% of blocks)"
        draw.text((left + 8, 7), caption, fill=(230, 230, 230))
    return sheet


def to_jpeg(image, quality: int) -> bytes:
    buffer = io.BytesIO()
    image.save(buffer, format="JPEG", quality=quality)
    return buffer.getvalue()


def run(args) -> int:
    root = Path(args.output_root).expanduser().resolve()
    directories = (
        [root / args.object]
        if args.object
        else sorted(path.parent for path in root.glob("*/manifest.json"))
    )
    clips = [
        load_clip(directory, nevo_only=args.nevo_only)
        for directory in directories
        if (directory / "manifest.json").is_file()
    ]
    if not args.no_crop:
        for clip in clips:
            clip["crop"] = subject_box(clip, args.crop_pad)
    if not clips:
        raise SystemExit(
            f"no rendered output under {root}. Run orbitnevo.render_frames first."
        )

    names = ", ".join(clip["name"] for clip in clips)
    total_frames = sum(len(clip["frames"]) for clip in clips)
    reference = clips[0]
    conditions = " | ".join(condition["label"] for condition in reference["conditions"])
    width = args.panel_width * len(reference["conditions"])
    probe = compose(reference, reference["frames"][0], args.panel_width)
    height = probe.height

    frame_buffer = FrameBuffer()

    def status_html():
        return (
            "<html><head><title>NeVo output</title>"
            "<style>body{background:#111;color:#eee;font-family:sans-serif;text-align:center}"
            "img{border:2px solid #444;margin-top:12px}"
            "p{color:#999;font-size:13px}</style></head><body>"
            f"<h2>NeVo &mdash; {names} (precomputed frames, looping at {args.fps} fps)</h2>"
            f"<p>{conditions}</p>"
            f"<p>camera {reference['view']} &middot; "
            f"{reference['width']}x{reference['height']} &middot; "
            f"{total_frames} frames &middot; rendered from ReRF feature voxels</p>"
            # Cache-busting token, same reason as Vega's demo: a browser holding
            # an open /stream from an earlier run will keep showing that clip.
            f'<img src="/stream?t={int(time.time())}" width="{width}" height="{height}"/>'
            "</body></html>"
        )

    serve_forever(frame_buffer, args.port, status_html_fn=status_html)
    print(f"serving on http://<this-machine-ip>:{args.port}/", flush=True)
    print(f"clips: {names}", flush=True)

    def loop():
        interval = 1.0 / max(args.fps, 1)
        while True:
            for clip in clips:
                for frame in clip["frames"]:
                    started = time.time()
                    frame_buffer.update(
                        to_jpeg(compose(clip, frame, args.panel_width), args.quality)
                    )
                    remaining = interval - (time.time() - started)
                    if remaining > 0:
                        time.sleep(remaining)

    thread = threading.Thread(target=loop, daemon=True)
    thread.start()
    try:
        while True:
            time.sleep(3600)
    except KeyboardInterrupt:
        print("stopped", flush=True)
    return 0


def parse_args(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--output-root", default="~/nevo_output")
    parser.add_argument("--object", default="", help="one clip; default cycles through all")
    parser.add_argument("--port", type=int, default=8752)
    parser.add_argument("--fps", type=int, default=8)
    parser.add_argument("--panel-width", type=int, default=420)
    parser.add_argument("--quality", type=int, default=88)
    parser.add_argument("--nevo-only", action="store_true",
                        help="stream only NeVo's visibility-filtered output, without the "
                             "plain-ReRF and captured-camera panels beside it")
    parser.add_argument("--no-crop", action="store_true",
                        help="show the full frame instead of cropping to the subject")
    parser.add_argument("--crop-pad", type=float, default=0.12)
    return parser.parse_args(argv)


def main(argv=None) -> int:
    return run(parse_args(argv))


if __name__ == "__main__":
    raise SystemExit(main())
