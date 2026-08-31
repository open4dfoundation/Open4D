#!/usr/bin/env python
"""Render the composed scene to a shareable MP4 — captured positions, real floor levels.

This is the offline counterpart to `scene_demo.py`: same merged single-pass
render, but `--layout native` by default (so every subject keeps its real
captured world position and floor tier) and the output is a video file rather
than an MJPEG endpoint.

Rendered from the bitstream rather than from `scene_export.py`'s `.pt` files on
purpose. The export bakes colour at one fixed camera; here the camera orbits,
so colour is re-decoded per object per frame at the direction each subject is
actually being seen from — which is what the hierarchical colour model is for.

The venue is roughly 16 x 17 m with three floor levels (ground y=0, stage
y=1.55, riser y=2.11). Framing all of it means each 1.8 m subject is a modest
fraction of frame height; that is the honest scale of the capture, not a
framing bug. Use `--fov` / `--zoom` for a tighter look at fewer subjects.

Usage:

    python -m orbitvega.scene_render \\
        --bitstream-dir results/vega-gaussian/prepared-final \\
        --out results/vega-gaussian/renders/scene_native.mp4
"""
from __future__ import annotations

import argparse
import json
import math
import subprocess
from pathlib import Path

import numpy as np
import torch
from PIL import Image, ImageDraw

from orbitvega.scene_demo import (SceneObject, lay_out,
                                                 project, start_chunk_server)
from vega.cameras import Camera, look_at_RT
from vega.gaussians import GaussianSet
from vega.rasterize import render

MIN_CAMERA_DISTANCE = 6.0


def parse_args():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--bitstream-dir", default="results/vega-gaussian/prepared-final")
    p.add_argument("--out", default="results/vega-gaussian/renders/scene_native.mp4")
    p.add_argument("--objects", nargs="+", default=None)
    p.add_argument("--layout", choices=("native", "row", "grid"), default="native",
                   help="native (default) keeps real captured positions and floor levels")
    p.add_argument("--columns", type=int, default=3)
    p.add_argument("--spacing", type=float, default=1.7)
    p.add_argument("--row-spacing", type=float, default=2.6)
    p.add_argument("--width", type=int, default=1920)
    p.add_argument("--height", type=int, default=1080)
    p.add_argument("--fov", type=float, default=50.0, help="horizontal FOV in degrees")
    p.add_argument("--fps", type=int, default=30)
    p.add_argument("--seconds", type=float, default=10.0, help="output duration")
    p.add_argument("--orbit-deg", type=float, default=360.0,
                   help="total azimuth swept over the whole video; 0 pins the front view")
    p.add_argument("--start-azimuth", type=float, default=0.0)
    p.add_argument("--elevation", type=float, default=0.10,
                   help="camera height above scene centre, as a fraction of the diagonal")
    p.add_argument("--zoom", type=float, default=1.0,
                   help=">1 pushes the camera further out, <1 pulls it in")
    p.add_argument("--margin", type=float, default=1.06, help="framing slack")
    p.add_argument("--labels", action="store_true", default=True)
    p.add_argument("--no-labels", dest="labels", action="store_false")
    p.add_argument("--freeze-frame", type=int, default=None,
                   help="hold this source frame for the whole render, so an orbit "
                        "varies viewing angle only (isolates angle from frame type)")
    p.add_argument("--stills", type=int, default=4,
                   help="also write this many evenly spaced PNG stills")
    p.add_argument("--crf", type=int, default=18, help="H.264 quality (lower = better)")
    p.add_argument("--chunk-port", type=int, default=8793)
    return p.parse_args()


def orbit_geometry(objects, args):
    """Scene centre plus a camera distance that frames every azimuth of the orbit.

    Sized once for the worst azimuth so the framing does not pump as the camera
    swings. The scene's horizontal half-extent along the camera's right axis is
    ``half_w*|cos a| + half_d*|sin a|``, whose maximum over all ``a`` is the
    hypotenuse of the two — so that is what has to fit.
    """
    los, his = zip(*(o.placed_bounds() for o in objects))
    lo, hi = np.min(np.stack(los), axis=0), np.max(np.stack(his), axis=0)
    centre = (lo + hi) / 2.0
    extent = hi - lo
    diag = float(np.linalg.norm(extent))

    half_w, half_h, half_d = extent[0] / 2, extent[1] / 2, extent[2] / 2
    eff_w = math.hypot(half_w, half_d)
    fovx = math.radians(args.fov)
    fovy = 2 * math.atan(math.tan(fovx / 2) / (args.width / max(args.height, 1)))
    fit = max(eff_w / math.tan(fovx / 2), half_h / math.tan(fovy / 2))
    dist = max(fit * args.margin + eff_w * 0.35, MIN_CAMERA_DISTANCE) * args.zoom
    return centre, extent, diag, fovx, fovy, dist


def build_camera(centre, diag, fovx, fovy, dist, azimuth_deg, args, device):
    a = math.radians(azimuth_deg)
    eye = centre + np.array([math.sin(a) * dist, args.elevation * diag, math.cos(a) * dist],
                            dtype=np.float32)
    R, T = look_at_RT(eye.astype(np.float32), centre.astype(np.float32))
    return Camera(R=R, T=T, fovx=fovx, fovy=fovy, width=args.width, height=args.height,
                  znear=0.05, zfar=max(400.0, 4 * dist), device=device)


def annotate(arr, header, tags):
    pil = Image.fromarray(arr, mode="RGB")
    draw = ImageDraw.Draw(pil)
    for x, y, text in tags:
        draw.text((x + 1, y + 1), text, fill=(0, 0, 0))
        draw.text((x, y), text, fill=(150, 230, 255))
    y = 8
    for line in header:
        draw.text((9, y + 1), line, fill=(0, 0, 0))
        draw.text((8, y), line, fill=(255, 255, 90))
        y += 15
    return np.asarray(pil)


def main():
    args = parse_args()
    device = "cuda" if torch.cuda.is_available() else "cpu"
    bdir = Path(args.bitstream_dir).resolve()
    catalog = json.loads((bdir / "catalog.json").read_text())
    entries = [e for e in catalog["objects"] if not args.objects or e["name"] in args.objects]
    if not entries:
        raise SystemExit("no objects selected")

    out = Path(args.out).resolve()
    out.parent.mkdir(parents=True, exist_ok=True)

    print(f"[1/4] Serving {bdir} on port {args.chunk_port} ...", flush=True)
    start_chunk_server(str(bdir), args.chunk_port)
    base_url = f"http://127.0.0.1:{args.chunk_port}"

    print(f"[2/4] Attaching {len(entries)} objects, layout={args.layout} ...", flush=True)
    objects = [SceneObject(e, base_url, device) for e in entries]
    lay_out(objects, args.layout, args.columns, args.spacing, args.row_spacing, device)
    tiers: dict[float, list[str]] = {}
    for o in objects:
        lo, hi = o.placed_bounds()
        tiers.setdefault(round(float(lo[1]), 2), []).append(o.name)
        print(f"      {o.name:11s} feet y={lo[1]:+.2f}  centre "
              f"({(lo[0]+hi[0])/2:+7.2f}, {(lo[2]+hi[2])/2:+7.2f})", flush=True)
    print("      floor levels: " + "; ".join(f"y={y:.2f} -> {', '.join(n)}"
                                             for y, n in sorted(tiers.items())), flush=True)

    centre, extent, diag, fovx, fovy, dist = orbit_geometry(objects, args)
    print(f"      venue {extent[0]:.1f} x {extent[1]:.1f} x {extent[2]:.1f} m, "
          f"camera {dist:.1f} m out", flush=True)

    n_out = max(1, int(round(args.fps * args.seconds)))
    n_src = max(o.n_frames for o in objects)

    print(f"[3/4] Rendering {n_out} frames ({args.width}x{args.height}) ...", flush=True)
    ff = subprocess.Popen(
        ["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24",
         "-s", f"{args.width}x{args.height}", "-r", str(args.fps), "-i", "pipe:0",
         "-an", "-c:v", "libx264", "-preset", "slow", "-crf", str(args.crf),
         "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(out)],
        stdin=subprocess.PIPE)

    still_at = set(np.linspace(0, n_out - 1, args.stills).astype(int).tolist()) if args.stills else set()
    bg = torch.zeros(3, device=device)
    for step in range(n_out):
        az = args.start_azimuth + args.orbit_deg * (step / n_out)
        cam = build_camera(centre, diag, fovx, fovy, dist, az, args, device)

        parts, colours, tags = [], [], []
        src = step if args.freeze_frame is None else args.freeze_frame
        for o in objects:
            gs, fi, ftype = o.frame(src)
            # Colour at the ORIGINAL positions (bbox-normalised hash grid), view
            # direction from the placed geometry -- see SceneObject.decode.
            colours.append(o.decode(gs, cam, fi, ftype == "key"))
            parts.append(o.placed(gs))
            if args.labels:
                lo, hi = o.placed_bounds()
                top = np.array([(lo[0] + hi[0]) / 2, hi[1] + 0.15, (lo[2] + hi[2]) / 2],
                               dtype=np.float32)
                at = project(cam, top)
                if at:
                    tags.append((at[0] - 4 * len(o.name) // 2, max(0, at[1] - 14), o.name))

        scene = GaussianSet.cat(parts)
        with torch.no_grad():
            img = render(cam, scene, bg, colors_override=torch.cat(colours, dim=0))["render"]
        arr = (img.clamp(0, 1).permute(1, 2, 0).cpu().numpy() * 255).astype(np.uint8)

        shown = (step if args.freeze_frame is None else args.freeze_frame) % n_src
        header = [f"{len(objects)} ORBIT objects, captured positions "
                  f"({len(tiers)} floor levels)",
                  f"{len(scene)//1000}k gaussians  frame {shown:02d}/{n_src}"
                  f"{' [FROZEN]' if args.freeze_frame is not None else ''}  "
                  f"azimuth {az % 360:5.1f} deg"]
        arr = annotate(arr, header, tags)
        ff.stdin.write(arr.tobytes())

        if step in still_at:
            Image.fromarray(arr).save(out.with_suffix("").as_posix() + f"_still_{step:04d}.png")
        if step % 30 == 0:
            print(f"      {step:4d}/{n_out}  {len(scene)//1000}k gaussians  "
                  f"az {az % 360:5.1f}", flush=True)

    ff.stdin.close()
    if ff.wait() != 0:
        raise SystemExit("ffmpeg failed")

    print(f"[4/4] Wrote {out}  ({out.stat().st_size/1e6:.1f} MB, "
          f"{n_out/args.fps:.1f}s @ {args.fps} fps)", flush=True)
    for p in sorted(out.parent.glob(out.stem + "_still_*.png")):
        print(f"      still: {p}  ({p.stat().st_size/1e6:.2f} MB)", flush=True)


if __name__ == "__main__":
    main()
