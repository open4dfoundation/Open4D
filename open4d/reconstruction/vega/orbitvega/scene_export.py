#!/usr/bin/env python
"""Export the composed multi-object scene to `.pt` files — one per frame.

`scene_demo.py` builds the merged scene in memory and throws it away after
rasterising it. This writes that same merged scene to disk instead, so it can
be loaded without re-running the streaming client.

What the export is NOT: a Vega bitstream. The per-object bitstream in
`--bitstream-dir` stores geometry only, and colour comes from each object's own
hierarchical model queried per view direction at render time. A single merged
file cannot carry nine bbox-normalised hash grids, so colour here is **baked**:
evaluated once, at one camera, and frozen as per-Gaussian RGB. The result is a
static snapshot of the scene's appearance from that direction, not a
view-dependent representation. Re-run with a different `--bake-azimuth` to get
the appearance from elsewhere.

Two things are corrected relative to a naive `GaussianSet.cat`:

* **object_id is reassigned.** Every object in the corpus was prepared with
  `k_objects=1`, so each one's internal `object_id` is `0`. Concatenating them
  as-is makes all nine subjects indistinguishable in the merged tensor. Each
  object is renumbered by its index in the scene, and the mapping is recorded
  in the manifest.
* **Colour is decoded before placement.** The hash grid is queried at each
  Gaussian's original position (see `SceneObject.decode`), never at its
  re-placed one, which would clamp every point onto the training bbox faces.

Usage:

    python -m orbitvega.scene_export \\
        --bitstream-dir results/vega-gaussian/prepared-final \\
        --out results/vega-gaussian/scene_export --layout native

Each frame lands as `frame_XXXX.pt` holding a flat dict of tensors:

    xyz (N,3)  scale_raw (N,3)  rot_raw (N,4)  opacity_raw (N,1)
    rgb (N,3)  object_id (N,)

with `scene_manifest.json` describing the layout, the bake camera and the
per-object offsets and id mapping.
"""
from __future__ import annotations

import argparse
import json
from argparse import Namespace
from pathlib import Path

import numpy as np
import torch

from orbitvega.scene_demo import (SceneObject, build_camera,
                                                 lay_out, start_chunk_server)


def parse_args():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--bitstream-dir", default="results/vega-gaussian/prepared-final",
                   help="output directory of orbitvega.prepare (must contain catalog.json)")
    p.add_argument("--out", default="results/vega-gaussian/scene_export",
                   help="directory to write frame_XXXX.pt + scene_manifest.json into")
    p.add_argument("--objects", nargs="+", default=None,
                   help="subset of object names; default is every object in the catalog")
    p.add_argument("--layout", choices=("native", "row", "grid"), default="native",
                   help="native (default) keeps each subject's real captured world "
                        "position; row/grid re-place them onto a synthetic lattice")
    p.add_argument("--columns", type=int, default=3, help="grid layout only")
    p.add_argument("--spacing", type=float, default=1.7, help="metres between subjects (x)")
    p.add_argument("--row-spacing", type=float, default=2.6, help="metres between rows (z)")
    p.add_argument("--frames", type=int, default=None,
                   help="how many frames to export; default is the longest object's count")
    p.add_argument("--bake-azimuth", type=float, default=0.0,
                   help="camera azimuth in degrees that colour is baked from")
    p.add_argument("--half", action="store_true",
                   help="store float16 instead of float32 (halves size, ~1e-3 error)")
    # Only used to define the bake camera, so the same framing logic as the viewer applies.
    p.add_argument("--width", type=int, default=1920)
    p.add_argument("--height", type=int, default=560)
    p.add_argument("--fov", type=float, default=60.0)
    p.add_argument("--chunk-port", type=int, default=8791)
    return p.parse_args()


def main():
    args = parse_args()
    device = "cuda" if torch.cuda.is_available() else "cpu"
    bdir = Path(args.bitstream_dir).resolve()
    catalog = json.loads((bdir / "catalog.json").read_text())
    entries = [e for e in catalog["objects"] if not args.objects or e["name"] in args.objects]
    if not entries:
        raise SystemExit("no objects selected; catalog has "
                         + ", ".join(e["name"] for e in catalog["objects"]))

    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)

    print(f"[1/3] Serving {bdir} on port {args.chunk_port} ...", flush=True)
    start_chunk_server(str(bdir), args.chunk_port)
    base_url = f"http://127.0.0.1:{args.chunk_port}"

    print(f"[2/3] Attaching {len(entries)} objects, layout={args.layout} ...", flush=True)
    objects = [SceneObject(e, base_url, device) for e in entries]
    lay_out(objects, args.layout, args.columns, args.spacing, args.row_spacing, device)
    for o in objects:
        lo, hi = o.placed_bounds()
        print(f"      {o.name:11s} feet y={lo[1]:+.2f}  centre "
              f"({(lo[0]+hi[0])/2:+7.2f}, {(lo[2]+hi[2])/2:+7.2f})", flush=True)

    # The bake camera is fixed for every frame, so the frozen colour is
    # consistent across the sequence rather than drifting with a sweep.
    cam_args = Namespace(width=args.width, height=args.height, fov=args.fov,
                         sweep_deg=0.0, margin=1.04, elevation=0.06)
    cam = build_camera(objects, cam_args, args.bake_azimuth, device)

    n_frames = args.frames or max(o.n_frames for o in objects)
    dtype = torch.float16 if args.half else torch.float32
    print(f"[3/3] Exporting {n_frames} frames to {out} ...", flush=True)

    manifest = {
        "source_bitstream": str(bdir),
        "dataset_root": catalog.get("dataset_root"),
        "layout": args.layout,
        "n_frames": n_frames,
        "dtype": "float16" if args.half else "float32",
        "colour": {
            "baked": True,
            "bake_azimuth_deg": args.bake_azimuth,
            "note": "per-Gaussian RGB frozen at one camera; not view-dependent",
        },
        "objects": [],
        "frames": [],
    }

    for i, o in enumerate(objects):
        lo, hi = o.placed_bounds()
        manifest["objects"].append({
            "scene_object_id": i,
            "name": o.name,
            "n_frames": o.n_frames,
            "offset": o.offset.cpu().numpy().tolist(),
            "placed_bounds_min": lo.tolist(),
            "placed_bounds_max": hi.tolist(),
        })

    total_bytes = 0
    for step in range(n_frames):
        xyz, scale, rot, opac, rgb, oid = [], [], [], [], [], []
        per_object = []
        for i, o in enumerate(objects):
            gs, fi, ftype = o.frame(step)
            # Colour first, at ORIGINAL positions; then place the geometry.
            colour = o.decode(gs, cam, fi, ftype == "key")
            placed = o.placed(gs)
            n = len(placed)
            xyz.append(placed.xyz.detach())
            scale.append(placed.scale_raw.detach())
            rot.append(placed.rot_raw.detach())
            opac.append(placed.opacity_raw.detach())
            rgb.append(colour.detach())
            # Renumber: every object's own object_id is 0, so keep them apart.
            oid.append(torch.full((n,), i, dtype=torch.long, device=placed.xyz.device))
            per_object.append({"scene_object_id": i, "name": o.name,
                               "source_frame_idx": fi, "frame_type": ftype,
                               "n_gaussians": n})

        payload = {
            "frame_idx": step,
            "xyz": torch.cat(xyz).to(dtype).cpu(),
            "scale_raw": torch.cat(scale).to(dtype).cpu(),
            "rot_raw": torch.cat(rot).to(dtype).cpu(),
            "opacity_raw": torch.cat(opac).to(dtype).cpu(),
            "rgb": torch.cat(rgb).clamp(0, 1).to(dtype).cpu(),
            "object_id": torch.cat(oid).cpu(),
            "objects": per_object,
            "layout": args.layout,
            "activation": "scale=exp(scale_raw), rot=normalize(rot_raw), "
                          "opacity=sigmoid(opacity_raw), rgb is already linear [0,1]",
        }
        path = out / f"frame_{step:04d}.pt"
        torch.save(payload, path)
        size = path.stat().st_size
        total_bytes += size
        n_total = payload["xyz"].shape[0]
        manifest["frames"].append({"frame_idx": step, "file": path.name,
                                   "n_gaussians": n_total, "bytes": size})
        print(f"      frame {step:03d}/{n_frames}  {n_total//1000:4d}k gaussians  "
              f"{size/1e6:6.2f} MB", flush=True)

    manifest["total_bytes"] = total_bytes
    (out / "scene_manifest.json").write_text(json.dumps(manifest, indent=2))
    print(f"\nWrote {n_frames} frames, {total_bytes/1e6:.1f} MB total, to {out}", flush=True)
    print(f"Manifest: {out / 'scene_manifest.json'}", flush=True)


if __name__ == "__main__":
    main()
