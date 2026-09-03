#!/usr/bin/env python
"""Stream ReRF live: entropy-decode the bitstream and ray-march it as you watch.

`live_demo.py` loops PNGs a renderer wrote earlier and says so. This does the
work per frame -- `nevo.stream.BitstreamPlayer` pulls the next frame out of the
bitstream, accumulates its residual, rebuilds the occupancy cache and
ray-marches a viewpoint -- so what arrives in the browser was computed while you
were looking at it. That is the difference between a clip a bundle may call
``origin="rendered"`` and one it must call ``origin="replay"``.

Measured on ``g_basketball`` at the corpus's own 1280x960: entropy decode 23-50
ms, ray-march 86-146 ms, so about 8 fps end to end on one 4090. Not 30, and the
status line reports the rate it is actually achieving rather than the rate it
was asked for. ``--scale`` trades resolution for rate when 8 is not enough.

The last hop is `vega.streaming.mjpeg_server`, the same server Vega's demos and
`live_demo.py` push into, so nothing about the transport can account for a
difference between the baselines.

**Why the camera can move but you cannot move it.** A viewpoint costs a full
ray-march, so this renders one and sends pixels; there is no geometry in the
stream and no channel back. ``--orbit`` walks the rig's own cameras so the
motion at least shows the reconstruction is three-dimensional. Making it
interactive needs a control channel, which is a different piece of work.

Usage, on the GPU machine, in the ``nevo`` environment:

    python -m orbitnevo.live_stream \\
        --config /media/frozzzen/LocalDisk/nevo_runs/g_basketball/config.py \\
        --compression-path /media/frozzzen/LocalDisk/nevo_runs/g_basketball/rerf \\
        --port 8802 --orbit

Then point a bundle at it with ``streamer.live.mjpeg(..., origin="rendered")``.
"""
from __future__ import annotations

import argparse
import io
import sys
import threading
import time
from collections import deque
from pathlib import Path

import numpy as np

MODULE_ROOT = Path(__file__).resolve().parents[1]
if str(MODULE_ROOT) not in sys.path:
    sys.path.insert(0, str(MODULE_ROOT))

# Vega's MJPEG server, resolved by path rather than left to PYTHONPATH: the two
# baselines are siblings under `open4d/reconstruction`, and requiring an
# environment variable to find one from the other is a step that gets forgotten
# and fails as an ImportError three seconds into a demo.
_VEGA = MODULE_ROOT.parent / "vega"
if _VEGA.is_dir() and str(_VEGA) not in sys.path:
    sys.path.insert(0, str(_VEGA))

from vega.streaming.mjpeg_server import FrameBuffer, serve_forever  # noqa: E402

from nevo.render import render_view  # noqa: E402
from nevo.stream import BitstreamPlayer  # noqa: E402

LABEL_HEIGHT = 26
#: How many frames the reported rate averages over. Short enough to react to a
#: key frame's heavier decode, long enough not to jitter every frame.
RATE_WINDOW = 15


def to_jpeg(image: np.ndarray, quality: int) -> bytes:
    from PIL import Image

    buffer = io.BytesIO()
    Image.fromarray((np.clip(image, 0.0, 1.0) * 255.0).astype(np.uint8)).save(
        buffer, format="JPEG", quality=quality
    )
    return buffer.getvalue()


def subject_box(image: np.ndarray, pad: float = 0.12, floor: float = 0.02):
    """Crop rectangle around whatever is not background, or None.

    The corpus frames a whole stage, so the subject is a small part of a
    1280x960 view and a 420 px pane leaves it a thumbnail. Taken from one
    rendered frame and then held fixed for the stream: recomputing per frame
    would make the crop breathe with the motion, which reads as camera shake.
    """
    grey = image.mean(axis=2)
    rows = np.flatnonzero((grey > floor).any(axis=1))
    columns = np.flatnonzero((grey > floor).any(axis=0))
    if rows.size == 0 or columns.size == 0:
        return None
    height, width = grey.shape
    margin_y = (rows[-1] - rows[0]) * pad
    margin_x = (columns[-1] - columns[0]) * pad
    return (
        int(max(columns[0] - margin_x, 0)),
        int(max(rows[0] - margin_y, 0)),
        int(min(columns[-1] + margin_x + 1, width)),
        int(min(rows[-1] + margin_y + 1, height)),
    )


def scaled(camera, factor: float):
    """The same camera at a fraction of its resolution.

    Every intrinsic scales with the image, so the view is identical and only
    the ray count changes -- which is the one knob that trades quality for rate
    without touching the bitstream.
    """
    import dataclasses

    if factor == 1.0:
        return camera
    width = max(16, int(round(camera.width * factor)))
    height = max(16, int(round(camera.height * factor)))
    return dataclasses.replace(
        camera,
        width=width,
        height=height,
        fx=camera.fx * factor,
        fy=camera.fy * factor,
        cx=camera.cx * factor,
        cy=camera.cy * factor,
    )


def label(image: np.ndarray, caption: str, box=None):
    """One captioned panel, cropped if asked. Returns a PIL image."""
    from PIL import Image, ImageDraw

    panel = Image.fromarray((np.clip(image, 0.0, 1.0) * 255.0).astype(np.uint8))
    if box:
        panel = panel.crop(box)
    sheet = Image.new("RGB", (panel.width, panel.height + LABEL_HEIGHT), (17, 17, 17))
    sheet.paste(panel, (0, LABEL_HEIGHT))
    ImageDraw.Draw(sheet).text((8, 7), caption, fill=(230, 230, 230))
    return sheet


def run(args) -> int:
    player = BitstreamPlayer(
        args.config,
        args.compression_path,
        pca=not args.no_pca,
        pca_channels=tuple(int(c) for c in args.pca_chs.split(",")),
        group_size=args.group_size or None,
    )
    cameras = [scaled(camera, args.scale) for camera in player.cameras()]
    if args.view >= len(cameras):
        raise SystemExit(
            f"--view {args.view}: this corpus has {len(cameras)} cameras (0-"
            f"{len(cameras) - 1})"
        )

    megabytes = player.bitstream_bytes / 1e6
    state = {"fps": 0.0, "decode_ms": 0.0, "render_ms": 0.0, "frame": -1, "passes": 0}
    frame_buffer = FrameBuffer()

    def status_html():
        first = cameras[args.view]
        return (
            "<html><head><title>NeVo live</title>"
            "<style>body{background:#111;color:#eee;font-family:sans-serif;text-align:center}"
            "img{border:2px solid #444;margin-top:12px}"
            "p{color:#999;font-size:13px}code{color:#7fd}</style></head><body>"
            f"<h2>ReRF &mdash; decoded and rendered live</h2>"
            f"<p>{len(player.frames)} frames, one group, "
            f"{megabytes:.1f} MB of bitstream &middot; "
            f"{first.width}x{first.height} &middot; "
            f"{state['fps']:.1f} fps measured "
            f"(decode {state['decode_ms']:.0f} ms + march {state['render_ms']:.0f} ms)</p>"
            "<p>Every frame you see was entropy-decoded and ray-marched on demand. "
            "The decode is sequential: a P-frame is a residual over its predecessor, "
            "so the group restarts from its key frame rather than seeking.</p>"
            f'<img src="/stream?t={int(time.time())}"/>'
            "</body></html>"
        )

    serve_forever(frame_buffer, args.port, status_html_fn=status_html)
    print(f"serving on http://<this-machine-ip>:{args.port}/", flush=True)
    print(
        f"bitstream {args.compression_path} "
        f"({len(player.frames)} frames, {megabytes:.1f} MB)",
        flush=True,
    )

    def loop():
        recent = deque(maxlen=RATE_WINDOW)
        box = None
        minimum = 1.0 / args.max_fps if args.max_fps > 0 else 0.0
        for frame in player.play(loop=True):
            started = time.time()
            # An orbiting camera walks the rig as time advances, which is the
            # cheapest way to show a pane is being rendered rather than replayed.
            index = (frame.index if args.orbit else args.view) % len(cameras)
            camera = cameras[index]
            image = render_view(player, frame, camera)
            render_seconds = time.time() - started
            if box is None and not args.no_crop:
                box = subject_box(image, args.crop_pad)

            recent.append(time.time())
            if len(recent) > 1:
                state["fps"] = (len(recent) - 1) / (recent[-1] - recent[0])
            state.update(
                frame=frame.index,
                decode_ms=frame.decode_seconds * 1000.0,
                render_ms=render_seconds * 1000.0,
            )
            if frame.index == player.frames[0]:
                state["passes"] += 1

            caption = (
                f"ReRF live  frame {frame.index:02d}"
                f"{'  I' if frame.is_key_frame else '  P'}"
                f"  cam {camera.camera_id}"
                f"  decode {frame.decode_seconds * 1000:.0f}ms"
                f"  march {render_seconds * 1000:.0f}ms"
                f"  {state['fps']:.1f} fps"
            )
            sheet = label(image, caption, box)
            frame_buffer.update(to_jpeg(np.asarray(sheet) / 255.0, args.quality))

            remaining = minimum - (time.time() - started)
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
    parser.add_argument("--config", required=True,
                        help="the run's config; the copy inside a run dir is the "
                             "resolved one")
    parser.add_argument("--compression-path", required=True,
                        help="directory codec/compress.py wrote")
    parser.add_argument("--port", type=int, default=8802)
    parser.add_argument("--view", type=int, default=0,
                        help="which rig camera to render from")
    parser.add_argument("--orbit", action="store_true",
                        help="advance the camera with the frame, walking the rig")
    parser.add_argument("--scale", type=float, default=1.0,
                        help="render at this fraction of the corpus resolution; the "
                             "one knob that trades quality for frame rate")
    parser.add_argument("--max-fps", type=float, default=0.0,
                        help="cap the rate; 0 means as fast as it renders")
    parser.add_argument("--quality", type=int, default=88)
    parser.add_argument("--no-crop", action="store_true",
                        help="send the whole frame instead of cropping to the subject")
    parser.add_argument("--crop-pad", type=float, default=0.12)
    parser.add_argument("--no-pca", action="store_true",
                        help="the bitstream was encoded without --pca")
    parser.add_argument("--pca_chs", default="7,13",
                        help="must match the encode, per upstream's README")
    parser.add_argument("--group-size", type=int, default=0,
                        help="key frame every N frames; 0 reads it as one group")
    return parser.parse_args(argv)


def main(argv=None) -> int:
    return run(parse_args(argv))


if __name__ == "__main__":
    raise SystemExit(main())
