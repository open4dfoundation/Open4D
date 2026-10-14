#!/usr/bin/env python
"""Stream ReRF live: decode its bitstream and ray-march it as you watch.

Every frame that reaches the browser was entropy-decoded and rendered on
demand. Measured on ``g_basketball``, one RTX 4090:

===========  ==========  =========  =========
resolution   decode      march      end to end
===========  ==========  =========  =========
1280x960     25-46 ms    84-153 ms  ~7.8 fps
640x480      25-46 ms    22 ms      ~21 fps
===========  ==========  =========  =========

**Resolution is this method's rate ladder, and that is not a workaround.** For
a server-rendered representation the bytes on the wire are JPEG, not features,
so what a receiver can trade is pixels and JPEG quality -- both available on the
bitstream that already exists. Changing the *bitstream's* quality would mean
re-encoding, which needs the training checkpoints, and those are gone. So
``--rung`` is the knob a platform can actually adapt on today, and
``--report-ladder`` measures what each setting costs before anything adapts.

**Why the camera moves but you cannot move it.** A viewpoint costs a full
ray-march, so this renders one and sends pixels; there is no geometry in the
stream and no channel back. ``--orbit`` walks the rig's own cameras so the
motion at least shows the reconstruction is three-dimensional. Steering it
needs a control channel, which is separate work.

Usage, in the Python 3.8 environment ReRF's entropy coder requires::

    python -m rerf_stream.serve \\
        --config /media/frozzzen/LocalDisk/nevo_runs/g_basketball/config.py \\
        --compression-path /media/frozzzen/LocalDisk/nevo_runs/g_basketball/rerf \\
        --port 8802 --orbit

Then point a bundle at it with ``streamer.live.mjpeg(..., origin="rendered")``.
"""
from __future__ import annotations

import argparse
import io
import time
from collections import deque
from pathlib import Path

import numpy as np

from .bitstream import BitstreamPlayer
from .mjpeg import FrameBuffer, serve as serve_mjpeg

LABEL_HEIGHT = 26

#: Named rungs: (scale, jpeg quality). A server-rendered stream's rate is set
#: by how many pixels it sends and how hard they are compressed, so these are
#: the levels a receiver could be offered. Rates are measured, not assumed --
#: see ``--report-ladder``.
RUNGS = {
    "high": (1.0, 92),
    "medium": (0.5, 88),
    "low": (0.25, 80),
}

#: Frames the reported rate averages over. Short enough to react to a key
#: frame's heavier decode, long enough not to jitter every frame.
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

    The corpus frames a whole stage, so the subject is a small part of the view
    and an unscaled pane leaves it a thumbnail. Taken from one frame and then
    held fixed: recomputing per frame makes the crop breathe with the motion,
    which reads as camera shake.
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


def label(image: np.ndarray, caption: str, box=None):
    """One captioned panel, cropped if asked."""
    from PIL import Image, ImageDraw

    panel = Image.fromarray((np.clip(image, 0.0, 1.0) * 255.0).astype(np.uint8))
    if box:
        panel = panel.crop(box)
    sheet = Image.new("RGB", (panel.width, panel.height + LABEL_HEIGHT), (17, 17, 17))
    sheet.paste(panel, (0, LABEL_HEIGHT))
    ImageDraw.Draw(sheet).text((8, 7), caption, fill=(230, 230, 230))
    return sheet


def report_ladder(player: BitstreamPlayer, *, frames: int = 8) -> list:
    """Measure what each rung costs: resolution, frame rate, bytes per second.

    The numbers a platform needs before it can adapt between these. Measured on
    real frames rather than predicted, because they can be: the content already
    exists, so there is no reason to model it.
    """
    cameras = player.cameras()
    stream = player.play(loop=True)
    rows = []
    for name, (scale, quality) in RUNGS.items():
        camera = cameras[0].scaled(scale)
        decode_s, march_s, total_bytes = 0.0, 0.0, 0
        for _ in range(frames):
            frame = next(stream)
            started = time.time()
            image = player.render(camera)
            march_s += time.time() - started
            decode_s += frame.decode_seconds
            total_bytes += len(to_jpeg(image, quality))
        per_frame = (decode_s + march_s) / frames
        rows.append({
            "rung": name,
            "resolution": "%dx%d" % (camera.width, camera.height),
            "jpeg_quality": quality,
            "fps": 1.0 / per_frame if per_frame else 0.0,
            "kb_per_frame": total_bytes / frames / 1000.0,
            "mbit_per_second": total_bytes / frames * 8 / per_frame / 1e6
            if per_frame else 0.0,
            "decode_ms": decode_s / frames * 1000.0,
            "march_ms": march_s / frames * 1000.0,
        })
    return rows


def run(args) -> int:
    player = BitstreamPlayer(
        args.config,
        args.compression_path,
        pca=not args.no_pca,
        pca_channels=tuple(int(c) for c in args.pca_chs.split(",")),
        group_size=args.group_size or None,
    )

    if args.report_ladder:
        rows = report_ladder(player)
        header = f"{'rung':<8}{'resolution':<12}{'q':>4}{'fps':>7}{'kB/frame':>10}{'Mbit/s':>9}"
        print(header)
        print("-" * len(header))
        for row in rows:
            print(
                f"{row['rung']:<8}{row['resolution']:<12}{row['jpeg_quality']:>4}"
                f"{row['fps']:>7.1f}{row['kb_per_frame']:>10.1f}"
                f"{row['mbit_per_second']:>9.1f}"
            )
        return 0

    scale, quality = RUNGS[args.rung]
    if args.scale is not None:
        scale = args.scale
    cameras = [camera.scaled(scale) for camera in player.cameras()]
    if args.view >= len(cameras):
        raise SystemExit(
            f"--view {args.view}: this corpus has {len(cameras)} cameras "
            f"(0-{len(cameras) - 1})"
        )

    megabytes = player.bitstream_bytes / 1e6
    state = {"fps": 0.0, "decode_ms": 0.0, "march_ms": 0.0, "kb": 0.0}
    frames = FrameBuffer()

    def status_html():
        camera = cameras[args.view]
        return (
            "<html><head><title>ReRF live</title>"
            "<style>body{background:#111;color:#eee;font-family:sans-serif;"
            "text-align:center}img{border:2px solid #444;margin-top:12px}"
            "p{color:#999;font-size:13px}</style></head><body>"
            "<h2>ReRF &mdash; decoded and rendered live</h2>"
            f"<p>{len(player.frames)} frames, one group, {megabytes:.1f} MB of "
            f"bitstream &middot; rung <b>{args.rung}</b> at "
            f"{camera.width}x{camera.height} &middot; "
            f"{state['fps']:.1f} fps measured "
            f"(decode {state['decode_ms']:.0f} ms + march {state['march_ms']:.0f} ms) "
            f"&middot; {state['kb']:.0f} kB/frame out</p>"
            "<p>Every frame was entropy-decoded and ray-marched on demand. The "
            "decode is sequential: a P-frame is a residual over its predecessor, "
            "so the group restarts at its key frame rather than seeking.</p>"
            f'<img src="/stream?t={int(time.time())}"/>'
            "</body></html>"
        )

    serve_mjpeg(frames, args.port, status_html)
    print(f"serving on http://<this-machine-ip>:{args.port}/", flush=True)
    print(
        f"bitstream {args.compression_path} "
        f"({len(player.frames)} frames, {megabytes:.1f} MB), rung {args.rung}",
        flush=True,
    )

    recent = deque(maxlen=RATE_WINDOW)
    box = None
    minimum = 1.0 / args.max_fps if args.max_fps > 0 else 0.0
    try:
        for frame in player.play(loop=True):
            started = time.time()
            index = (frame.index if args.orbit else args.view) % len(cameras)
            camera = cameras[index]
            image = player.render(camera)
            march_seconds = time.time() - started
            if box is None and not args.no_crop:
                box = subject_box(image, args.crop_pad)

            recent.append(time.time())
            if len(recent) > 1:
                state["fps"] = (len(recent) - 1) / (recent[-1] - recent[0])
            caption = (
                f"ReRF live  frame {frame.index:02d}"
                f"{'  I' if frame.is_key_frame else '  P'}"
                f"  cam {camera.camera_id}  {camera.width}x{camera.height}"
                f"  decode {frame.decode_seconds * 1000:.0f}ms"
                f"  march {march_seconds * 1000:.0f}ms"
                f"  {state['fps']:.1f} fps"
            )
            jpeg = to_jpeg(np.asarray(label(image, caption, box)) / 255.0, quality)
            state.update(
                decode_ms=frame.decode_seconds * 1000.0,
                march_ms=march_seconds * 1000.0,
                kb=len(jpeg) / 1000.0,
            )
            frames.update(jpeg)

            remaining = minimum - (time.time() - started)
            if remaining > 0:
                time.sleep(remaining)
    except KeyboardInterrupt:
        print("stopped", flush=True)
    return 0


def parse_args(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--config", required=True,
                        help="the run's config; the copy inside a run dir is resolved")
    parser.add_argument("--compression-path", required=True,
                        help="directory upstream's codec/compress.py wrote")
    parser.add_argument("--port", type=int, default=8802)
    parser.add_argument("--rung", choices=sorted(RUNGS), default="high",
                        help="resolution and JPEG quality; this method's rate ladder")
    parser.add_argument("--scale", type=float, default=None,
                        help="override the rung's resolution scale")
    parser.add_argument("--report-ladder", action="store_true",
                        help="measure fps and bitrate for every rung, then exit")
    parser.add_argument("--view", type=int, default=0, help="which rig camera")
    parser.add_argument("--orbit", action="store_true",
                        help="advance the camera with the frame, walking the rig")
    parser.add_argument("--max-fps", type=float, default=0.0,
                        help="cap the rate; 0 means as fast as it renders")
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
