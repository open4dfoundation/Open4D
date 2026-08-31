#!/usr/bin/env python
"""Simple viewer: all objects of a prepared bitstream in ONE shared 3D view.

`wall_demo.py` gives each object its own tile and its own camera. This instead
puts every object into a single scene, on a single ground plane, and renders
them with one camera — so you can see all nine at once, at comparable scale,
in the same space.

Why re-place them: the nine ORBIT objects already share a world frame, but it
is a composed venue (see `scene_layout.json`) with three floor levels — ground
at y=0, a stage at y=1.55 and a riser at y=2.11 — spread over roughly
17 x 18 m. Framing that natively leaves the subjects tiny and scattered across
tiers. `--layout grid` (default) and `row` therefore translate each subject so
its feet sit on y=0 and its footprint lands on an evenly spaced lattice;
`--layout native` shows the real composed scene instead.

Only translation is applied — no rotation or scaling — so each subject's own
geometry and motion are untouched.

One deliberate difference from `wall_demo.py`: colour is decoded per object
(each carries its own hierarchical colour model) and then everything is
rasterised in a single pass, so inter-object depth is correct. That single pass
bypasses the §6 per-object culling/scheduling simulation, which is what
`wall_demo.py` is for — this module is for looking at the corpus.

Usage:

    python -m orbitvega.scene_demo \\
        --bitstream-dir results/vega-gaussian/prepared-final --mjpeg-port 8772

Then open http://<this-machine-ip>:8772/ in a browser.
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

import dataclasses

from vega.cameras import Camera, look_at_RT
from vega.gaussians import GaussianSet
from vega.player import BitstreamClient, StreamingPlayer
from vega.rasterize import render, view_directions
from vega.streaming.mjpeg_server import FrameBuffer, serve_forever

MIN_CAMERA_DISTANCE = 6.0   # see orbitvega.eval_camera; this rasterizer culls nearer than ~4


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("--bitstream-dir", default="results/vega-gaussian/prepared-final")
    p.add_argument("--objects", nargs="+", default=None)
    p.add_argument("--layout", choices=("grid", "row", "native"), default="row",
                   help="row (default) cannot self-occlude; grid is denser but subjects "
                        "in front hide those behind")
    p.add_argument("--columns", type=int, default=3, help="grid layout only")
    p.add_argument("--spacing", type=float, default=1.7, help="metres between subjects (x)")
    p.add_argument("--row-spacing", type=float, default=2.6, help="metres between rows (z)")
    p.add_argument("--width", type=int, default=1920)
    p.add_argument("--height", type=int, default=560)
    p.add_argument("--fov", type=float, default=60.0,
                   help="HORIZONTAL field of view in degrees; vertical follows the aspect")
    p.add_argument("--sweep-deg", type=float, default=8.0,
                   help="camera oscillates +/- this many degrees; 0 pins the front view")
    p.add_argument("--sweep-seconds", type=float, default=24.0)
    p.add_argument("--elevation", type=float, default=0.06,
                   help="camera height as a fraction of the scene's diagonal")
    p.add_argument("--margin", type=float, default=1.04, help="framing slack")
    p.add_argument("--target-fps", type=float, default=20.0)
    p.add_argument("--labels", action="store_true", default=True)
    p.add_argument("--no-labels", dest="labels", action="store_false")
    p.add_argument("--chunk-port", type=int, default=8773)
    p.add_argument("--mjpeg-port", type=int, default=8772)
    return p.parse_args()


def start_chunk_server(directory: str, port: int) -> ThreadingHTTPServer:
    handler = partial(SimpleHTTPRequestHandler, directory=directory)
    httpd = ThreadingHTTPServer(("0.0.0.0", port), handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    return httpd


class SceneObject:
    """One object's client + player, plus where it sits in the shared scene."""

    def __init__(self, entry: dict, base_url: str, device: str):
        self.name = entry["name"]
        self.n_frames = entry["frame_count"]
        self.client = BitstreamClient(f"{base_url}/{entry['dir']}")
        self.color_model = self.client.get_color_model(device)
        self.player = StreamingPlayer(self.color_model, device=device)
        self.bmin = np.asarray(entry["bounds_min"], dtype=np.float32)
        self.bmax = np.asarray(entry["bounds_max"], dtype=np.float32)
        self.offset = torch.zeros(3, device=device)
        self._chunks: dict[int, dict] = {}

    @property
    def footprint_centre(self) -> np.ndarray:
        """Centre of the subject's footprint, at floor height."""
        c = (self.bmin + self.bmax) / 2.0
        return np.array([c[0], self.bmin[1], c[2]], dtype=np.float32)

    def place(self, target: np.ndarray, device: str):
        self.offset = torch.as_tensor(target - self.footprint_centre,
                                      dtype=torch.float32, device=device)

    def placed_bounds(self) -> tuple[np.ndarray, np.ndarray]:
        o = self.offset.cpu().numpy()
        return self.bmin + o, self.bmax + o

    def frame(self, step: int):
        """(GaussianSet at its ORIGINAL positions, frame_idx, frame_type).

        Deliberately not translated yet: the colour model has to be queried at
        the positions it was trained on (see `decode`).
        """
        fi = step % self.n_frames
        if fi not in self._chunks:
            self._chunks[fi] = self.client.get_frame_chunk(fi)
        chunk = self._chunks[fi]
        return self.player.reconstruct(chunk), fi, chunk["frame_type"]

    def decode(self, gs: GaussianSet, camera: Camera, frame_idx: int, is_key: bool):
        """Colour for this object, decoded with its own hierarchical model.

        The hash grid is queried at the Gaussian's **original** position, not
        its position in the shared scene. `HierarchicalColorModel._normalize`
        maps position into [0,1] using the bbox the model was trained with —
        this object's own native bounds — and clamps. Feeding it re-placed
        coordinates pushes every point outside that box, where the clamp
        collapses them onto the box faces and the decoded colour degenerates
        into flat blocks and stripes.

        The view direction, by contrast, is taken from the *placed* geometry,
        since that is the direction the subject is actually being seen from.
        """
        with torch.no_grad():
            dirs = view_directions(camera, gs.get_xyz + self.offset)
            if is_key:
                return self.color_model.forward_key(gs.get_xyz, dirs)
            return self.color_model.forward_residual(gs.get_xyz, dirs, frame_idx)

    def placed(self, gs: GaussianSet) -> GaussianSet:
        """The same Gaussians moved into the shared scene, for rasterising."""
        return dataclasses.replace(gs, xyz=(gs.xyz + self.offset).contiguous())


def lay_out(objects: list[SceneObject], layout: str, columns: int, spacing: float,
            row_spacing: float, device: str):
    if layout == "native":
        return
    n = len(objects)
    cols = columns if layout == "grid" else n
    rows = math.ceil(n / cols)
    for i, obj in enumerate(objects):
        col, row = i % cols, i // cols
        # Alternate rows are offset by half a step so a subject in front never
        # sits directly in front of one behind it.
        x = (col - (cols - 1) / 2.0) * spacing + (spacing / 2.0 if row % 2 else 0.0)
        z = ((rows - 1) / 2.0 - row) * row_spacing     # first row nearest the camera
        obj.place(np.array([x, 0.0, z], dtype=np.float32), device)


def frame_distance(extent: np.ndarray, fovx: float, fovy: float, sweep_deg: float,
                   margin: float) -> float:
    """Camera distance that keeps the whole layout framed at every sweep angle.

    Sized once, for the worst azimuth in the sweep, so the framing does not
    pump in and out as the camera swings. As the scene rotates by `a`, its
    extent along the camera's right axis is `half_w*cos a + half_d*sin a`;
    both terms are positive, so checking the ends of the sweep is enough.
    Distance is measured to the scene centre, so a little over half the depth
    is added to keep the nearest row inside the frustum.
    """
    half_w, half_h, half_d = extent[0] / 2, extent[1] / 2, extent[2] / 2
    a = math.radians(abs(sweep_deg))
    eff_w = max(half_w, half_w * math.cos(a) + half_d * math.sin(a))
    fit = max(eff_w / math.tan(fovx / 2), half_h / math.tan(fovy / 2))
    return max(fit * margin + half_d * 0.6, MIN_CAMERA_DISTANCE)


def build_camera(objects: list[SceneObject], args, azimuth_deg: float, device: str) -> Camera:
    los, his = zip(*(o.placed_bounds() for o in objects))
    lo, hi = np.min(np.stack(los), axis=0), np.max(np.stack(his), axis=0)
    centre = (lo + hi) / 2.0
    extent = hi - lo
    diag = float(np.linalg.norm(extent))

    # Fit width and height independently rather than fitting the bounding
    # sphere: the layout is much wider than it is tall, so a sphere fit pushes
    # the camera back far enough to leave most of the frame empty.
    aspect = args.width / max(args.height, 1)
    fovx = math.radians(args.fov)
    fovy = 2 * math.atan(math.tan(fovx / 2) / aspect)
    dist = frame_distance(extent, fovx, fovy, args.sweep_deg, args.margin)

    a = math.radians(azimuth_deg)
    eye = centre + np.array([math.sin(a) * dist, args.elevation * diag, math.cos(a) * dist],
                            dtype=np.float32)
    R, T = look_at_RT(eye.astype(np.float32), centre.astype(np.float32))
    return Camera(R=R, T=T, fovx=fovx, fovy=fovy, width=args.width, height=args.height,
                  znear=0.05, zfar=max(400.0, 4 * dist), device=device)


def project(camera: Camera, point: np.ndarray) -> tuple[int, int] | None:
    p = torch.tensor([[*point, 1.0]], dtype=torch.float32, device=camera.world_view_transform.device)
    clip = p @ camera.full_proj_transform
    if float(clip[0, 3]) <= 1e-6:
        return None
    ndc = clip[:, :3] / clip[:, 3:4]
    x = float((ndc[0, 0] + 1.0) * camera.width - 1.0) * 0.5
    y = float((ndc[0, 1] + 1.0) * camera.height - 1.0) * 0.5
    if not (0 <= x < camera.width and 0 <= y < camera.height):
        return None
    return int(x), int(y)


def to_jpeg(img: torch.Tensor, header: list[str], tags: list[tuple[int, int, str]]) -> bytes:
    arr = (img.clamp(0, 1).permute(1, 2, 0).detach().cpu().numpy() * 255).astype(np.uint8)
    pil = Image.fromarray(arr, mode="RGB")
    draw = ImageDraw.Draw(pil)
    for x, y, text in tags:
        draw.text((x - 1 + 1, y + 1), text, fill=(0, 0, 0))
        draw.text((x - 1, y), text, fill=(150, 230, 255))
    y = 6
    for line in header:
        draw.text((7, y + 1), line, fill=(0, 0, 0))
        draw.text((6, y), line, fill=(255, 255, 90))
        y += 14
    buf = io.BytesIO()
    pil.save(buf, format="JPEG", quality=85)
    return buf.getvalue()


def main():
    args = parse_args()
    device = "cuda"
    bdir = Path(args.bitstream_dir).resolve()
    catalog = json.loads((bdir / "catalog.json").read_text())
    entries = [e for e in catalog["objects"] if not args.objects or e["name"] in args.objects]
    if not entries:
        raise SystemExit("no objects selected")

    print(f"[1/3] Serving {bdir} on port {args.chunk_port} ...", flush=True)
    start_chunk_server(str(bdir), args.chunk_port)
    base_url = f"http://127.0.0.1:{args.chunk_port}"

    print(f"[2/3] Attaching {len(entries)} objects and laying them out ({args.layout}) ...",
          flush=True)
    objects = [SceneObject(e, base_url, device) for e in entries]
    lay_out(objects, args.layout, args.columns, args.spacing, args.row_spacing, device)
    for o in objects:
        lo, hi = o.placed_bounds()
        print(f"      {o.name:11s} feet at y={lo[1]:+.2f}  centre "
              f"({(lo[0]+hi[0])/2:+.2f}, {(lo[2]+hi[2])/2:+.2f})", flush=True)
    n_frames = max(o.n_frames for o in objects)

    frame_buffer = FrameBuffer()
    state = {"fps": 0.0}

    def status_html():
        return (
            "<html><head><title>Vega — ORBIT scene</title>"
            "<style>body{background:#0c0c0c;color:#ddd;font-family:sans-serif;text-align:center}"
            "img{border:1px solid #333;margin-top:10px;max-width:98vw;height:auto}"
            "p{color:#888;font-size:13px}</style></head><body>"
            f"<h2>Vega — {len(objects)} ORBIT objects in one scene</h2>"
            f"<p>{catalog['dataset_root']} · {args.layout} layout · "
            f"{', '.join(o.name for o in objects)}</p>"
            f'<img src="/stream?t={int(time.time())}" '
            f'width="{args.width}" height="{args.height}"/>'
            "</body></html>")

    print(f"[3/3] Starting MJPEG stream on port {args.mjpeg_port} ...", flush=True)
    serve_forever(frame_buffer, args.mjpeg_port, status_html_fn=status_html)
    print(f"\nOpen http://<this-machine-ip>:{args.mjpeg_port}/ in a browser.\n", flush=True)

    bg = torch.zeros(3, device=device)
    step = 0
    period = 1.0 / args.target_fps
    last = None
    while True:
        t0 = time.time()
        phase = (step / max(args.target_fps * args.sweep_seconds, 1e-6)) * 2 * math.pi
        cam = build_camera(objects, args, args.sweep_deg * math.sin(phase), device)

        parts, colours, tags = [], [], []
        for o in objects:
            gs, fi, ftype = o.frame(step)
            colours.append(o.decode(gs, cam, fi, ftype == "key"))
            parts.append(o.placed(gs))
            if args.labels:
                lo, hi = o.placed_bounds()
                top = np.array([(lo[0] + hi[0]) / 2, hi[1] + 0.12, (lo[2] + hi[2]) / 2],
                               dtype=np.float32)
                at = project(cam, top)
                if at:
                    tags.append((at[0] - 4 * len(o.name) // 2, max(0, at[1] - 12), o.name))

        scene = GaussianSet.cat(parts)
        with torch.no_grad():
            out = render(cam, scene, bg, colors_override=torch.cat(colours, dim=0))["render"]

        header = [f"{len(objects)} objects · {len(scene)//1000}k gaussians · "
                  f"frame {step % n_frames:02d}/{n_frames}",
                  f"{args.layout} layout · view {state['fps']:4.1f} FPS"]
        frame_buffer.update(to_jpeg(out, header, tags))

        now = time.time()
        if last is not None:
            inst = 1.0 / max(now - last, 1e-6)
            state["fps"] = inst if state["fps"] == 0.0 else 0.85 * state["fps"] + 0.15 * inst
        last = now
        time.sleep(max(0.0, period - (now - t0)))
        step += 1


if __name__ == "__main__":
    main()
