#!/usr/bin/env python
"""Live demo of *all* ORBIT scene objects at once — a 3x3 wall of streams.

Where `live_demo.py` encodes one scene and plays it, this consumes a bitstream
already produced by `orbitvega.prepare` (so nothing is re-encoded) and plays
every object in it simultaneously. Each tile runs the full client path
independently, exactly as the single-scene demo does for one object:

    HTTP chunk fetch  ->  StreamingPlayer.reconstruct  ->  hierarchical colour
    decode  ->  object-level early culling + priority scheduling (§6)  ->  render

The nine tiles are then composited into one frame and pushed out as a single
MJPEG stream, so one browser tab shows the whole corpus playing.

Usage (from the repo root, on the GPU machine):

    python -m orbitvega.wall_demo \\
        --bitstream-dir results/vega-gaussian/prepared-final \\
        --mjpeg-port 8768

Then open http://<this-machine-ip>:8768/ in a browser.
"""
from __future__ import annotations

import argparse
import io
import json
import math
import threading
import time
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import numpy as np
import torch
from PIL import Image, ImageDraw

from orbitvega.eval_camera import (eval_camera_for_bounds, playback_fov_deg,
                                                 safe_orbit_radius)
from vega.cameras import orbit_cameras
from vega.pipeline import ViewAdaptiveRenderer
from vega.player import BitstreamClient, StreamingPlayer
from vega.profiling import profile_latency_model
from vega.streaming.mjpeg_server import FrameBuffer, serve_forever


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("--bitstream-dir", default="results/vega-gaussian/prepared-final",
                   help="output directory of orbitvega.prepare (must contain catalog.json)")
    p.add_argument("--objects", nargs="+", default=None,
                   help="subset of object names; default is every object in the catalog")
    p.add_argument("--tile", type=int, default=340, help="per-scene tile size in pixels")
    p.add_argument("--target-fps", type=float, default=30.0,
                   help="deadline the §6 scheduler is simulated against")
    p.add_argument("--chunk-port", type=int, default=8769)
    p.add_argument("--mjpeg-port", type=int, default=8768)
    p.add_argument("--n-play-cameras", type=int, default=120)
    p.add_argument("--subject-fill", type=float, default=0.8)
    return p.parse_args()


class Scene:
    """One tile: its own chunk client, player, renderer and camera path."""

    def __init__(self, entry: dict, base_url: str, latency_model, tile: int,
                 target_fps: float, n_cameras: int, subject_fill: float, device: str):
        self.name = entry["name"]
        self.n_frames = entry["frame_count"]
        self.client = BitstreamClient(f"{base_url}/{entry['dir']}")
        self.manifest = self.client.get_manifest()
        self.color_model = self.client.get_color_model(device)
        self.player = StreamingPlayer(self.color_model, device=device)
        self.renderer = ViewAdaptiveRenderer(self.color_model, latency_model,
                                            t_deadline_ms=1000.0 / target_fps)

        bmin = torch.tensor(entry["bounds_min"], dtype=torch.float32, device=device)
        bmax = torch.tensor(entry["bounds_max"], dtype=torch.float32, device=device)
        radius = safe_orbit_radius(bmin, bmax)
        fov = playback_fov_deg(bmin, bmax, radius, fill=subject_fill)
        self.cameras = orbit_cameras(
            n_cameras=n_cameras, radius=radius,
            height=float((bmax[1] - bmin[1]).item() * 0.3),
            target=((bmin + bmax) / 2).cpu().numpy().tolist(),
            width=tile, height_px=tile, fov_deg=fov, device=device)
        self._chunks: dict[int, dict] = {}
        self.last_fps = 0.0

    def chunk(self, frame_idx: int) -> dict:
        # Chunks stay on the CPU between uses; only the frame being rendered is
        # moved to the GPU (by StreamingPlayer), so nine sequences fit at once.
        if frame_idx not in self._chunks:
            self._chunks[frame_idx] = self.client.get_frame_chunk(frame_idx)
        return self._chunks[frame_idx]

    def render(self, step: int, bg: torch.Tensor):
        frame_idx = step % self.n_frames
        chunk = self.chunk(frame_idx)
        gs = self.player.reconstruct(chunk)
        is_key = chunk["frame_type"] == "key"
        # Residual chunks carry exactly the objects the encoder judged dynamic,
        # which is the dyn(O) term Eq. 8 wants.
        dyn = {oid: 1.0 for oid in chunk.get("transmitted_objects", [])}
        fr = self.renderer.render_frame(gs, self.cameras[step % len(self.cameras)],
                                        frame_idx, dyn, is_key, bg)
        self.last_fps = fr.fps
        return fr, frame_idx, chunk["frame_type"], len(gs)


def start_chunk_server(directory: str, port: int) -> ThreadingHTTPServer:
    handler = partial(SimpleHTTPRequestHandler, directory=directory)
    httpd = ThreadingHTTPServer(("0.0.0.0", port), handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    return httpd


def compose(tiles: list[np.ndarray], labels: list[list[str]], cols: int, tile: int,
            wall_fps: float) -> bytes:
    rows = math.ceil(len(tiles) / cols)
    canvas = Image.new("RGB", (cols * tile, rows * tile), (8, 8, 8))
    for i, (arr, lines) in enumerate(zip(tiles, labels)):
        img = Image.fromarray(arr, mode="RGB")
        if img.size != (tile, tile):
            img = img.resize((tile, tile), Image.LANCZOS)
        x, y = (i % cols) * tile, (i // cols) * tile
        canvas.paste(img, (x, y))
        draw = ImageDraw.Draw(canvas)
        ty = y + 4
        for line in lines:
            draw.text((x + 6, ty + 1), line, fill=(0, 0, 0))
            draw.text((x + 5, ty), line, fill=(255, 255, 80))
            ty += 12
    draw = ImageDraw.Draw(canvas)
    footer = f"wall {wall_fps:4.1f} FPS   {len(tiles)} scenes"
    draw.text((6, rows * tile - 14), footer, fill=(0, 0, 0))
    draw.text((5, rows * tile - 15), footer, fill=(140, 220, 255))
    buf = io.BytesIO()
    canvas.save(buf, format="JPEG", quality=82)
    return buf.getvalue()


def main():
    args = parse_args()
    device = "cuda"
    bitstream_dir = Path(args.bitstream_dir).resolve()
    catalog = json.loads((bitstream_dir / "catalog.json").read_text())
    entries = [e for e in catalog["objects"]
               if not args.objects or e["name"] in args.objects]
    if not entries:
        raise SystemExit(f"no objects selected; catalog has "
                         f"{', '.join(e['name'] for e in catalog['objects'])}")

    print(f"[1/4] Serving {bitstream_dir} on port {args.chunk_port} "
          f"({len(entries)} objects) ...", flush=True)
    start_chunk_server(str(bitstream_dir), args.chunk_port)
    base_url = f"http://127.0.0.1:{args.chunk_port}"

    print("[2/4] Profiling per-task latency on this GPU (shared across scenes) ...", flush=True)
    first = entries[0]
    lat_model = profile_latency_model(
        eval_camera_for_bounds(torch.tensor(first["bounds_min"], device=device),
                               torch.tensor(first["bounds_max"], device=device), device),
        sizes=[1000, 5000, 10000, 20000, 40000])

    print("[3/4] Attaching a client + player + renderer per scene ...", flush=True)
    scenes = []
    for e in entries:
        s = Scene(e, base_url, lat_model, args.tile, args.target_fps,
                  args.n_play_cameras, args.subject_fill, device)
        scenes.append(s)
        print(f"      {s.name:11s} {s.n_frames} frames, "
              f"{sum(f['total_bytes'] for f in s.manifest['frames'])/1e6:6.2f} MB", flush=True)

    cols = math.ceil(math.sqrt(len(scenes)))
    frame_buffer = FrameBuffer()
    state = {"fps": 0.0}

    def status_html():
        names = ", ".join(s.name for s in scenes)
        side = cols * args.tile
        return (
            "<html><head><title>Vega — ORBIT wall</title>"
            "<style>body{background:#0d0d0d;color:#eee;font-family:sans-serif;text-align:center}"
            "img{border:1px solid #333;margin-top:10px;max-width:98vw;height:auto}"
            "p{color:#999;font-size:13px}</style></head><body>"
            f"<h2>Vega — {len(scenes)} ORBIT objects streaming live</h2>"
            f"<p>{catalog['dataset_root']} · {catalog['dataset_format']} corpus · {names}</p>"
            f'<img src="/stream?t={int(time.time())}" width="{side}" height="{side}"/>'
            "</body></html>"
        )

    print(f"[4/4] Starting MJPEG stream on port {args.mjpeg_port} ...", flush=True)
    serve_forever(frame_buffer, args.mjpeg_port, status_html_fn=status_html)
    print(f"\nOpen http://<this-machine-ip>:{args.mjpeg_port}/ in a browser.\n", flush=True)

    bg = torch.zeros(3, device=device)
    step = 0
    period = 1.0 / args.target_fps
    last_update = None
    while True:
        t0 = time.time()
        tiles, labels = [], []
        for s in scenes:
            fr, frame_idx, ftype, n_gauss = s.render(step, bg)
            arr = (fr.image.clamp(0, 1).permute(1, 2, 0).detach().cpu().numpy() * 255).astype(np.uint8)
            tiles.append(arr)
            labels.append([
                f"{s.name}  {frame_idx:02d}/{s.n_frames} [{ftype[:3]}]",
                f"{n_gauss//1000}k gauss  sim {fr.fps:.0f} FPS",
            ])
        frame_buffer.update(compose(tiles, labels, cols, args.tile, state["fps"]))

        # Rate is measured between consecutive published frames, not from the
        # dispatch time of one iteration: the per-tile work is queued on the GPU
        # asynchronously, so timing the loop body alone reports a rate the
        # stream never actually achieves. Smoothed, since JPEG size and the
        # key/residual mix make individual frames uneven.
        now = time.time()
        if last_update is not None:
            inst = 1.0 / max(now - last_update, 1e-6)
            state["fps"] = inst if state["fps"] == 0.0 else 0.8 * state["fps"] + 0.2 * inst
        last_update = now

        time.sleep(max(0.0, period - (now - t0)))
        step += 1


if __name__ == "__main__":
    main()
