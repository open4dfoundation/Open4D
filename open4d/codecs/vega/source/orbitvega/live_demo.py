#!/usr/bin/env python
"""Live end-to-end Vega demo.

Encodes a real ORBIT capture into a Vega bitstream, serves it over HTTP from
this machine (the "Vega Server"), then runs a player loop that fetches
chunks over that same HTTP connection, reconstructs each frame, runs the
real view-adaptive rendering pipeline (object-level early culling +
priority-based task scheduling, §6), and pushes the rendered frames out as
an MJPEG stream any browser can open — so you can watch it live from a
different machine while all the encoding/decoding/rendering compute stays
on this GPU box.

Either ORBIT corpus works, told apart automatically from `--dataset-root`
(see `orbitvega.prepare.detect_format`): the multi-view RGB Gaussian-training
corpus (default, geometry recovered by silhouette carving — see
`vega.datasets.orbit_gaussian`) or the older RGBD corpus.

Usage (on the remote GPU machine, from the 4DVideoStreaming repo root):

    python -m orbitvega.live_demo \\
        --dataset-root /media/frozzzen/DataDrive/ORBIT_datasets_gaussian \\
        --scene basketball --n-frames 30 --mjpeg-port 8767

Then open http://<this-machine's-ip>:8767/ in a browser.
"""
from __future__ import annotations

import argparse
import dataclasses
import io
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
from orbitvega.prepare import (DEFAULT_K_OBJECTS, RGBD_DATASET_ROOT, detect_format,
                                              list_objects, load_frames, pick_frames)
from vega.bitstream import write_bitstream
from vega.cameras import orbit_cameras
from vega.color_encoding import DEFAULT_COLOR_CONFIG
from vega.encoder import VegaEncoderConfig, encode_sequence
from vega.pipeline import ViewAdaptiveRenderer
from vega.player import BitstreamClient, StreamingPlayer
from vega.profiling import profile_latency_model
from vega.segmentation import segment_sequence
from vega.streaming.mjpeg_server import FrameBuffer, serve_forever

DEFAULT_DATASET_ROOT = "/media/frozzzen/DataDrive/ORBIT_datasets_gaussian"


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("--dataset-root", "--level-dir", dest="dataset_root",
                   default=DEFAULT_DATASET_ROOT,
                   help=f"ORBIT corpus root (gaussian or rgbd; rgbd lives at {RGBD_DATASET_ROOT})")
    p.add_argument("--dataset-format", choices=("gaussian", "rgbd"), default=None,
                   help="default: auto-detected from --dataset-root")
    p.add_argument("--scene", default="basketball")
    p.add_argument("--n-frames", type=int, default=30)
    p.add_argument("--max-points", type=int, default=120000)
    p.add_argument("--image-scale", type=float, default=0.125)
    p.add_argument("--voxel-size", type=float, default=0.006)
    p.add_argument("--carve-scale", type=float, default=0.5)
    p.add_argument("--view-slack", type=int, default=0)
    p.add_argument("--prune-opacity", type=float, default=0.01,
                   help="drop Gaussians refinement faded below this opacity (0 disables)")
    p.add_argument("--refine-iters", type=int, default=0,
                   help="photometric 3DGS refinement iterations per frame after carving")
    p.add_argument("--k-objects", type=int, default=DEFAULT_K_OBJECTS,
                   help="see orbitvega.prepare.DEFAULT_K_OBJECTS")
    p.add_argument("--key-iters", type=int, default=800)
    p.add_argument("--residual-iters", type=int, default=600)
    p.add_argument("--tiny-hash-log2", type=int, default=None,
                   help="log2 entries per level in the per-residual-frame tiny hash "
                        "(default: vega.color_encoding's); main lever on residual colour fidelity")
    p.add_argument("--dyn-iters", type=int, default=20)
    p.add_argument("--target-fps", type=float, default=30.0)
    p.add_argument("--chunk-port", type=int, default=8766)
    p.add_argument("--mjpeg-port", type=int, default=8767)
    p.add_argument("--outdir", default="results/vega-run/live_demo_bitstream")
    p.add_argument("--n-play-cameras", type=int, default=120)
    p.add_argument("--render-size", type=int, default=512,
                   help="square render resolution; also the size streamed, so no upscaling")
    p.add_argument("--play-fov", type=float, default=None,
                   help="playback field of view in degrees; default fits the subject to the frame")
    p.add_argument("--subject-fill", type=float, default=0.8,
                   help="fraction of the half-frame the subject should span when --play-fov is auto")
    return p.parse_args()


def start_chunk_server(directory: str, port: int) -> ThreadingHTTPServer:
    handler = partial(SimpleHTTPRequestHandler, directory=directory)
    httpd = ThreadingHTTPServer(("0.0.0.0", port), handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    return httpd


def tensor_to_jpeg(img: torch.Tensor, hud_lines: list[str], size: int | None = None) -> bytes:
    arr = (img.clamp(0, 1).permute(1, 2, 0).detach().cpu().numpy() * 255).astype(np.uint8)
    pil = Image.fromarray(arr, mode="RGB")
    # Only resample if the caller actually wants a different size, and then
    # with a real filter — NEAREST on an upscale is what made the subject look
    # like it was made of bricks.
    if size is not None and (pil.width, pil.height) != (size, size):
        pil = pil.resize((size, size), Image.LANCZOS)
    draw = ImageDraw.Draw(pil)
    y = 6
    for line in hud_lines:
        draw.text((7, y + 1), line, fill=(0, 0, 0))
        draw.text((6, y), line, fill=(255, 255, 80))
        y += 14
    buf = io.BytesIO()
    pil.save(buf, format="JPEG", quality=85)
    return buf.getvalue()


def main():
    args = parse_args()
    device = "cuda"
    dataset_root = Path(args.dataset_root)
    fmt = args.dataset_format or detect_format(dataset_root)

    objects = {obj.name: obj for obj in list_objects(dataset_root, fmt)}
    if args.scene not in objects:
        raise SystemExit(f"unknown scene {args.scene!r}; available: {', '.join(objects)}")
    obj = objects[args.scene]

    print(f"[1/6] Loading {args.n_frames} frames of scene '{args.scene}' from "
          f"{dataset_root} (format: {fmt}) ...", flush=True)
    frame_ids = pick_frames(obj, fmt, args.n_frames)
    frames, cams, gts, bmin, bmax = load_frames(
        dataset_root, fmt, obj, frame_ids, max_points=args.max_points,
        image_scale=args.image_scale, device=device, voxel_size=args.voxel_size,
        carve_scale=args.carve_scale, view_slack=args.view_slack,
        refine_iters=args.refine_iters, prune_opacity=args.prune_opacity, verbose=True)
    n_frames = len(frames)

    print(f"[2/6] Segmenting into {args.k_objects} object(s) (Gaussian-grouping fallback) ...",
          flush=True)
    frames_seg = segment_sequence(frames, k=args.k_objects, n_iters=20)

    print("[3/6] Encoding (GOV planning + hierarchical color encoding + dynamicity filtering) ...", flush=True)
    color_config = DEFAULT_COLOR_CONFIG
    if args.tiny_hash_log2 is not None:
        color_config = dataclasses.replace(
            DEFAULT_COLOR_CONFIG,
            tiny_hash={**DEFAULT_COLOR_CONFIG.tiny_hash,
                       "log2_hashmap_size": args.tiny_hash_log2})
    cfg = VegaEncoderConfig(color_config=color_config, key_iters=args.key_iters,
                            residual_iters=args.residual_iters, dyn_iters=args.dyn_iters)
    # See orbitvega.eval_camera for why the encoder's own default camera is
    # not used here.
    eval_cam = eval_camera_for_bounds(bmin, bmax, device)
    t0 = time.time()
    result = encode_sequence(frames_seg, bmin, bmax, eval_camera=eval_cam, config=cfg)
    total_bytes = sum(c.total_bytes for c in result.chunks)
    print(f"      done in {time.time()-t0:.1f}s — {total_bytes/1e6:.2f} MB for {n_frames} frames, "
          f"{sum(1 for c in result.chunks if c.frame_type=='key')} key frame(s), "
          f"mean PSNR {sum(result.psnr_db)/len(result.psnr_db):.1f} dB")

    write_bitstream(args.outdir, result.color_model, result.chunks)
    outdir_abs = str(Path(args.outdir).resolve())

    print(f"[4/6] Starting chunk HTTP server on port {args.chunk_port} (serving {outdir_abs}) ...", flush=True)
    start_chunk_server(outdir_abs, args.chunk_port)
    client = BitstreamClient(f"http://127.0.0.1:{args.chunk_port}")
    manifest = client.get_manifest()
    color_model = client.get_color_model(device)
    player = StreamingPlayer(color_model, device=device)

    print("[5/6] Profiling real per-task latency (hash/MLP/sort/render) on this GPU ...", flush=True)
    lat_model = profile_latency_model(eval_cam, sizes=[1000, 5000, 10000, 20000, 40000])
    renderer = ViewAdaptiveRenderer(color_model, lat_model, t_deadline_ms=1000.0 / args.target_fps)

    # Radius/FOV keep a deliberate safety margin: the subject moves around
    # within its own sequence-wide bounding box over time, but the camera
    # orbits a single fixed point (that box's center), so too tight a margin
    # clips parts of the subject once it has drifted off that fixed center.
    # Measured worst-case visibility over 30 frames x orbit angles:
    #   radius (at 90 deg FOV): x3.15 and x2.80 hold 100%; x2.50 clips (91.7%)
    #   FOV (at radius x3.15):  90..35 deg all hold 100%
    # Distance is therefore the binding constraint, not field of view — the
    # subject only subtends ~18 degrees at this orbit radius, so FOV has lots
    # of headroom. 70 deg is chosen over the earlier 90 deg because it frames
    # the subject ~43% larger *and* avoids obvious wide-angle distortion;
    # verified clean over all 30 frames x all 120 orbit angles.
    # `safe_orbit_radius` additionally floors the radius at the distance this
    # environment's rasterizer needs to draw anything at all (see
    # orbitvega.eval_camera).
    #
    # Field of view is no longer the fixed 70 deg quoted above. At the orbit
    # radius that floor forces, a ~1.9 m subject subtends only ~18 deg, so a
    # 70 deg frame spent ~90% of its pixels on empty background and left the
    # subject a ~45 px smudge. `playback_fov_deg` instead fits the subject's
    # bounding sphere to `--subject-fill` of the half-frame, which is the same
    # compensation `eval_camera_for_bounds` applies to the encoder's camera.
    play_radius = safe_orbit_radius(bmin, bmax)
    play_fov = args.play_fov if args.play_fov is not None else playback_fov_deg(
        bmin, bmax, play_radius, fill=args.subject_fill)
    print(f"      playback: radius {play_radius:.2f}, fov {play_fov:.1f} deg, "
          f"{args.render_size}x{args.render_size}")
    play_cams = orbit_cameras(n_cameras=args.n_play_cameras,
                              radius=play_radius,
                              height=float((bmax[1] - bmin[1]).item() * 0.3),
                              target=((bmin + bmax) / 2).cpu().numpy().tolist(),
                              width=args.render_size, height_px=args.render_size,
                              fov_deg=play_fov, device=device)
    bg = torch.zeros(3, device=device)

    frame_buffer = FrameBuffer()

    def status_html():
        return (
            "<html><head><title>Vega live demo</title>"
            "<style>body{background:#111;color:#eee;font-family:sans-serif;text-align:center}"
            "img{border:2px solid #444;margin-top:12px}</style></head><body>"
            f"<h2>Vega — {args.scene} (live, rendered on remote GPU)</h2>"
            f"<p>{dataset_root} · {fmt} corpus · {n_frames} frames"
            f"{f' · {args.refine_iters} refine iters' if args.refine_iters else ''}</p>"
            # Cache-busting token: without it a browser that already has an
            # open/cached /stream from an earlier run of this demo will happily
            # keep showing that older scene.
            f'<img src="/stream?t={int(time.time())}" '
            f'width="{args.render_size}" height="{args.render_size}"/>'
            "</body></html>"
        )

    print(f"[6/6] Starting MJPEG stream on port {args.mjpeg_port} ...", flush=True)
    serve_forever(frame_buffer, args.mjpeg_port, status_html_fn=status_html)
    print(f"\nOpen http://<this-machine-ip>:{args.mjpeg_port}/ in a browser.\n", flush=True)

    chunk_cache: dict[int, dict] = {}
    dyn_by_frame = {d["frame_idx"]: set(d.get("dynamic_objects", [])) for d in result.dynamicity_log}

    step = 0
    frame_period = 1.0 / args.target_fps
    while True:
        frame_idx = step % n_frames
        cam = play_cams[step % len(play_cams)]

        if frame_idx not in chunk_cache:
            chunk_cache[frame_idx] = client.get_frame_chunk(frame_idx)
        chunk = chunk_cache[frame_idx]

        t_start = time.time()
        gs = player.reconstruct(chunk)
        is_key = chunk["frame_type"] == "key"
        # The bitstream's filtering decision is already a binary
        # dynamic/static split (§5.3); reuse it directly as dyn(O) in the
        # playback-time priority function (Eq. 8) rather than threading the
        # continuous training-time dynamicity value through the wire format.
        dyn_by_object = {oid: 1.0 for oid in dyn_by_frame.get(frame_idx, set())}

        fr = renderer.render_frame(gs, cam, frame_idx, dyn_by_object, is_key, bg)

        # With a single object there is no budget contention for §6.3 to
        # resolve and nothing for §6.2 to cull, so the simulated frame time
        # collapses to one GPU task and the FPS figure stops being meaningful.
        # Say so on the HUD rather than letting a four-digit number read as a
        # performance result.
        sched_note = "" if len(fr.visible) > 1 else "  [1 obj: sched inert]"
        hud = [
            f"frame {frame_idx:02d}/{n_frames} [{chunk['frame_type']}]",
            f"sim {fr.fps:5.1f} FPS  (budget {args.target_fps:.0f}){sched_note}",
            f"cpu {fr.cpu_ms:5.1f}ms gpu {fr.gpu_ms:5.1f}ms npu {fr.npu_ms:5.1f}ms",
            f"visible {len(fr.visible)}  culled {len(fr.culled)}",
        ]
        jpeg = tensor_to_jpeg(fr.image, hud, size=args.render_size)
        frame_buffer.update(jpeg)

        elapsed = time.time() - t_start
        time.sleep(max(0.0, frame_period - elapsed))
        step += 1


if __name__ == "__main__":
    main()
