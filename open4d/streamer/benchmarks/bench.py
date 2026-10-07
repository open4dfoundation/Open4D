#!/usr/bin/env python3
"""Benchmark the streamer on real mesh captures and real Gaussian sequences.

Phases, each writing ``<out>/results/<phase>.json`` and safe to rerun alone:

``mesh``
    Every frame of each mesh capture, written at a Draco ladder and scored
    against the capture with `open4d.compare_sequences`.
``codecs``
    Open4D codec rungs (``klt/draco`` and the native V-DMC pair) on a window of
    one capture, beside plain Draco.
``gaussians``
    Gaussian sequences made by `orbit_splats.py`, written at a PLY/.splat ladder
    that also keeps fractions of each frame's Gaussians, and scored in *pixels*:
    each rung is rendered at the eight ORBIT cameras with Vega's rasterizer and
    compared with ORBIT's own images.
``native``
    The Gaussian forms Open4D hands back from its own codecs -- a Vega
    ``.vmesh`` opened as a `NativeSequence`, and a QUEEN `GaussianRun` --
    streamed through ``open4d.stream``.
``delivery``
    The bundle server, over loopback and through a shaped `Link`: what a rung
    costs to fetch, one connection and four.
``fetch``
    The same measurement from another machine, against a running server.
``playback``
    `streamer.playback` over constant links and the study's bandwidth traces,
    adaptive against fixed-rung baselines, one pane and several.

The mesh and Gaussian phases need the optional ``open4d-streamer`` package,
DracoPy and SciPy; ``gaussians`` and ``native`` also need CUDA and the Vega
research tree on ``PYTHONPATH``.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import contextlib
import csv
import json
import math
import os
import platform
import statistics
import sys
import time
import urllib.request
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
FPS = 30

MESH_CAPTURES = ("C3", "C4", "C5", "C6")
MESH_RUNGS = ["ply", "draco", "draco@11", "draco@8"]
CODEC_RUNGS = ["draco", "klt/draco", "vdmc/draco", "faster_vdmc/draco"]
CODEC_FRAMES = 30
GAUSSIAN_RUNGS = ["ply", "splat", "splat@50%", "splat@25%", "splat@10%"]
#: Frames scored per Gaussian clip: every third, at all eight cameras.
SCORE_EVERY = 3
#: A trace's zero-bandwidth seconds, as a capacity `Trace` accepts: 10 kbit/s.
OUTAGE_BPS = 1e4
#: Foreground: a pixel either image lights above this.
FOREGROUND = 0.03


# ---------------------------------------------------------------- helpers ---


def write_result(out: Path, phase: str, result: dict) -> Path:
    path = out / "results" / f"{phase}.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    result = {"phase": phase, "machine": machine(), **result}
    path.write_text(json.dumps(result, indent=2, allow_nan=False) + "\n")
    print(f"wrote {path}", flush=True)
    return path


def machine() -> dict:
    info = {"host": platform.node(), "python": platform.python_version(),
            "cpus": os.cpu_count()}
    try:
        import torch

        if torch.cuda.is_available():
            info["gpu"] = torch.cuda.get_device_name(0)
    except ImportError:
        pass
    return info


@contextlib.contextmanager
def timed_exports():
    """Seconds each clip export took, keyed by the frame directory it wrote."""
    from streamer import export, gaussians, session

    times: dict[str, float] = {}
    patched = [(export, "from_sequence"), (gaussians, "from_frames"),
               (session, "_through_codec")]
    originals = [getattr(module, name) for module, name in patched]

    def wrap(function, key):
        def timed(*args, **kwargs):
            start = time.perf_counter()
            result = function(*args, **kwargs)
            label = key(args, kwargs)
            times[label] = times.get(label, 0.0) + time.perf_counter() - start
            return result
        return timed

    export.from_sequence = wrap(originals[0], lambda a, k: k["name"])
    gaussians.from_frames = wrap(originals[1], lambda a, k: k["name"])
    session._through_codec = wrap(originals[2], lambda a, k: f"codec:{a[1]}")
    try:
        yield times
    finally:
        for (module, name), original in zip(patched, originals):
            setattr(module, name, original)


def rung_rows(clip, out: Path, times: dict, representation: str) -> list[dict]:
    """One row per rendition of ``clip``: size, rate, quality, cost."""
    from streamer import session

    renditions = [(clip.detail["rung"], clip.frames, clip.detail.get("quality", {}),
                   clip.name, clip.detail)]
    for variant in clip.variants:
        rung = session.parse_rung(variant["name"], representation)
        renditions.append((variant["name"], variant["frames"], variant["quality"],
                           session._rendition_name(clip.name, rung), variant["detail"]))
    rows = []
    for name, frames, quality, directory, detail in renditions:
        size = sum((out / frame).stat().st_size for frame in frames)
        row = {
            "rung": name,
            "frames": len(frames),
            "bytes": size,
            "bytes_per_frame": size / len(frames),
            "mbit_s": size * 8 * FPS / len(frames) / 1e6,
            "export_seconds": times.get(directory),
            "quality": quality,
        }
        if detail.get("codec"):
            row["codec"] = detail["codec"]
            row["codec_bytes"] = detail["codec_bytes"]
            row["codec_mbit_s"] = detail["codec_bytes"] * 8 * FPS / len(frames) / 1e6
            row["codec_seconds"] = times.get(f"codec:{detail['codec']}")
        rows.append(row)
    return rows


def decode_ms(out: Path, frames: list[str], sample: int = 30) -> float:
    """Mean milliseconds to parse one of ``frames`` back to arrays, in Python."""
    chosen = frames[:: max(1, len(frames) // sample)][:sample]
    start = time.perf_counter()
    for frame in chosen:
        path = out / frame
        if path.suffix == ".drc":
            import DracoPy

            DracoPy.decode(path.read_bytes())
        elif path.suffix == ".splat":
            np.frombuffer(path.read_bytes(), dtype=np.uint8).reshape(-1, 32)
        else:
            import open4d

            if "gaussians" in str(path.parent):
                open4d.load_gaussians(path)
            else:
                with open4d.load(path) as sequence:
                    sequence[0].geometry
    return (time.perf_counter() - start) * 1000 / len(chosen)


# ------------------------------------------------------------------- mesh ---


def subject_of(directory: Path) -> str:
    first = sorted(directory.glob("*.obj"))[0].stem
    return first.rsplit("_fr", 1)[0]


def phase_mesh(args) -> dict:
    import open4d
    import streamer
    from streamer import score

    out = args.out / "bundles" / "mesh"
    rows = []
    with streamer.Bundle(out, title="Open4D mesh captures (C3-C6)", fps=FPS) as bundle:
        for capture in args.captures:
            directory = args.mesh_data / capture
            subject = subject_of(directory)
            raw = sum(p.stat().st_size for p in directory.glob("*.obj"))
            with open4d.load(directory, fps=FPS) as sequence:
                vertices = [len(sequence[i].geometry.positions)
                            for i in range(0, len(sequence), 30)]
                with timed_exports() as times:
                    start = time.perf_counter()
                    clip = bundle.add(sequence, name=subject, scene=subject,
                                      method="mesh", rungs=MESH_RUNGS)
                    export_seconds = time.perf_counter() - start
                start = time.perf_counter()
                score.score_clip(sequence, clip, out)
                score_seconds = time.perf_counter() - start
            rungs = rung_rows(clip, out, times, "mesh")
            for row, frames in zip(rungs, [clip.frames] + [v["frames"] for v in clip.variants]):
                row["decode_ms_per_frame"] = decode_ms(out, frames)
            rows.append({
                "capture": capture, "subject": subject, "frames": len(clip.frames),
                "mean_vertices": float(np.mean(vertices)),
                "obj_bytes": raw, "obj_mbit_s": raw * 8 * FPS / len(clip.frames) / 1e6,
                "export_seconds": export_seconds, "score_seconds": score_seconds,
                "rungs": rungs,
            })
            print(f"{capture} {subject}: " + ", ".join(
                f"{r['rung']} {r['mbit_s']:.1f} Mbit/s {r['quality'][score.METRIC]:.1f} dB"
                for r in rungs), flush=True)
    return {"bundle": str(out), "metric": score.METRIC, "fps": FPS, "captures": rows}


def phase_codecs(args) -> dict:
    import open4d
    import streamer
    from open4d.core import SequenceView
    from streamer import score

    out = args.out / "bundles" / "codecs"
    rows = []
    with streamer.Bundle(out, title=f"Open4D codecs, first {args.codec_frames} frames",
                         fps=FPS) as bundle:
        for capture in args.codec_captures:
            directory = args.mesh_data / capture
            subject = subject_of(directory)
            with open4d.load(directory, fps=FPS) as sequence:
                window = SequenceView(sequence, range(args.codec_frames))
                for rung in args.codec_rungs:
                    # One clip per codec rather than one ladder: a codec that
                    # fails must not take the others down with it.
                    name = f"{subject}-{rung.replace('/', '-').replace('@', '')}"
                    with timed_exports() as times:
                        try:
                            start = time.perf_counter()
                            clip = bundle.add(window, name=name, scene=subject,
                                              method=rung.split("/")[0], rungs=[rung])
                            seconds = time.perf_counter() - start
                        except Exception as error:  # noqa: BLE001 - recorded
                            rows.append({"capture": capture, "rung": rung,
                                         "error": f"{type(error).__name__}: {error}"})
                            print(f"{capture} {rung}: FAILED {error}", flush=True)
                            continue
                    score.score_clip(window, clip, out)
                    (row,) = rung_rows(clip, out, times, "mesh")
                    row.update(capture=capture, total_seconds=seconds)
                    rows.append(row)
                    print(f"{capture} {rung}: {row['mbit_s']:.2f} Mbit/s served, "
                          f"{row.get('codec_mbit_s', row['mbit_s']):.2f} as bitstream, "
                          f"{row['quality'][score.METRIC]:.1f} dB, {seconds:.0f} s",
                          flush=True)
    return {"bundle": str(out), "metric": score.METRIC, "frames": args.codec_frames,
            "rows": rows}


# -------------------------------------------------------------- gaussians ---


class Renderer:
    """Vega's rasterizer at ORBIT's eight cameras, for one subject."""

    def __init__(self, data: Path, subject: str, meta: dict):
        import torch
        from vega.datasets import orbit_gaussian as og

        self.torch, self.og = torch, og
        transforms = og.load_object_transforms(data, subject)
        entries = og.group_frames(transforms)[0]
        self.cameras = [og.build_camera(transforms, entry, meta["image_scale"],
                                        device="cuda") for entry in entries]
        self.centre = torch.tensor(
            (np.asarray(meta["bounds_min"]) + np.asarray(meta["bounds_max"])) / 2,
            dtype=torch.float32, device="cuda")
        # The audit's projection-preserving workaround: at the rig's own
        # distance renders come out black, so world and cameras are scaled
        # together, which leaves every projection unchanged.
        nearest = min(float((c.camera_center - self.centre).norm()) for c in self.cameras)
        self.scale = max(1.0, 6.0 / nearest)

    def render(self, arrays: dict, *, dc_only: bool) -> list[np.ndarray]:
        torch = self.torch
        from vega.gaussians import GaussianSet
        from vega.rasterize import render

        arrays = {key: np.asarray(value, dtype=np.float32) for key, value in arrays.items()}

        sh = torch.tensor(arrays["sh"], device="cuda")
        if dc_only:
            sh = sh[:, :1]
        opacity = torch.tensor(np.clip(arrays["opacities"], 1e-6, 1 - 1e-6),
                               device="cuda")[:, None]
        gaussians = GaussianSet(
            torch.tensor(arrays["positions"], device="cuda"),
            torch.tensor(arrays["scales"], device="cuda").log(),
            torch.tensor(arrays["rotations"], device="cuda"),
            torch.logit(opacity), sh[:, :1].contiguous(), sh[:, 1:].contiguous(),
            torch.zeros(len(opacity), dtype=torch.long, device="cuda"),
            sh_degree=int(math.isqrt(sh.shape[1])) - 1,
        )
        scaled, cameras = self.og._uniform_scale(gaussians, self.cameras,
                                                 self.centre, self.scale)
        images = []
        with torch.no_grad():
            for camera in cameras:
                image = render(camera, scaled, torch.zeros(3, device="cuda"))["render"]
                images.append(image.permute(1, 2, 0).clamp(0, 1).cpu().numpy())
        return images


def frame_arrays(path: Path) -> dict:
    """A rung's frame file as activated arrays, however it was written."""
    if path.suffix == ".splat":
        payload = np.frombuffer(path.read_bytes(), dtype=np.uint8).reshape(-1, 32)
        floats = payload[:, :24].copy().view(np.float32).reshape(-1, 6)
        rotations = (payload[:, 28:32].astype(np.float32) - 128) / 128
        rotations /= np.maximum(np.linalg.norm(rotations, axis=1, keepdims=True), 1e-12)
        rgb = payload[:, 24:27].astype(np.float32) / 255
        return {"positions": floats[:, :3].copy(), "scales": floats[:, 3:].copy(),
                "rotations": rotations, "opacities": payload[:, 27] / 255.0,
                "sh": ((rgb - 0.5) / 0.28209479177387814)[:, None, :]}
    import open4d

    splats = open4d.load_gaussians(path)
    return {"positions": splats.positions, "scales": splats.scales,
            "rotations": splats.rotations, "opacities": splats.opacities,
            "sh": splats.spherical_harmonics}


def pixel_quality(renderer: Renderer, frames: list[Path], truth_dir: Path,
                  *, dc_only: bool, to_orbit=None) -> dict:
    """``to_orbit`` maps a frame written in another frame back to ORBIT's."""
    from PIL import Image
    from streamer.metrics import psnr, ssim

    scores = {"psnr": [], "ssim": [], "foreground_psnr": []}
    for index in range(0, len(frames), SCORE_EVERY):
        arrays = frame_arrays(frames[index])
        if to_orbit is not None:
            arrays = to_orbit(arrays)
        images = renderer.render(arrays, dc_only=dc_only)
        for view, image in enumerate(images):
            truth = np.asarray(Image.open(truth_dir / f"f{index:03d}_v{view}.png"),
                               dtype=np.float32) / 255
            scores["psnr"].append(psnr(image, truth))
            scores["ssim"].append(ssim(image, truth))
            mask = (truth.max(2) > FOREGROUND) | (image.max(2) > FOREGROUND)
            scores["foreground_psnr"].append(psnr(image[mask], truth[mask]))
    return {key: float(np.mean(values)) for key, values in scores.items()}


def load_cached(directory: Path):
    import open4d

    frames = []
    for path in sorted(directory.glob("frame_*.npz")):
        with np.load(path) as data:
            frames.append(open4d.GaussianSplats(**{name: data[name] for name in data.files}))
    return frames


def phase_gaussians(args) -> dict:
    import streamer

    out = args.out / "bundles" / "gaussians"
    subjects = args.subjects or sorted(
        p.parent.name for p in args.orbit_cache.glob("*/meta.json"))
    rows = []
    with streamer.Bundle(out, title="ORBIT Gaussian sequences", fps=FPS) as bundle:
        for subject in subjects:
            cache = args.orbit_cache / subject
            meta = json.loads((cache / "meta.json").read_text())
            frames = load_cached(cache)
            with timed_exports() as times:
                start = time.perf_counter()
                clip = bundle.add(frames, name=subject, scene=subject,
                                  method="gaussians", rungs=GAUSSIAN_RUNGS)
                export_seconds = time.perf_counter() - start
            del frames
            renderer = Renderer(args.orbit_data, subject, meta)
            start = time.perf_counter()
            # The source, at full SH, is the ceiling every rung is measured
            # against; scored from the PLY rung, which holds it losslessly.
            source = pixel_quality(renderer, [out / f for f in clip.frames],
                                   cache / "gt", dc_only=False)
            clip.detail["quality"] = pixel_quality(
                renderer, [out / f for f in clip.frames], cache / "gt", dc_only=True)
            for variant in clip.variants:
                variant["quality"] = pixel_quality(
                    renderer, [out / f for f in variant["frames"]], cache / "gt",
                    dc_only=True)
            score_seconds = time.perf_counter() - start
            rungs = rung_rows(clip, out, times, "gaussians")
            for row, files in zip(rungs, [clip.frames] + [v["frames"] for v in clip.variants]):
                row["decode_ms_per_frame"] = decode_ms(out, files, sample=10)
                row["mean_gaussians"] = float(np.mean(
                    [(out / f).stat().st_size / 32 for f in files]
                    if files[0].endswith(".splat") else clip.counts))
            rows.append({
                "subject": subject, "frames": len(clip.frames),
                "build_seconds": meta["build_seconds"], "sh_degree": meta["sh_degree"],
                "mean_gaussians": float(np.mean(meta["splats"])),
                "export_seconds": export_seconds, "score_seconds": score_seconds,
                "source_full_sh": source, "rungs": rungs,
            })
            print(f"{subject}: source {source['psnr']:.1f} dB; " + ", ".join(
                f"{r['rung']} {r['mbit_s']:.0f} Mbit/s {r['quality']['psnr']:.1f} dB"
                for r in rungs), flush=True)
    return {"bundle": str(out), "metric": "psnr", "fps": FPS,
            "scored": f"every {SCORE_EVERY}rd frame at all 8 ORBIT cameras, "
                      "512x384, rungs rendered with SH degree 0 as the client "
                      "draws them", "subjects": rows}


# ----------------------------------------------------------------- native ---


def phase_native(args) -> dict:
    import open4d

    out = args.out / "bundles" / "native"
    out.mkdir(parents=True, exist_ok=True)
    rows = []

    def attempt(label, function):
        start = time.perf_counter()
        try:
            detail = function()
            rows.append({"case": label, "status": "ok",
                         "seconds": time.perf_counter() - start, **detail})
        except Exception as error:  # noqa: BLE001 - recorded, not hidden
            import traceback

            rows.append({"case": label, "status": "failed",
                         "seconds": time.perf_counter() - start,
                         "error": f"{type(error).__name__}: {error}",
                         "traceback": traceback.format_exc()[-2000:]})
        print(f"{label}: {rows[-1]['status']} in {rows[-1]['seconds']:.0f} s "
              f"{rows[-1].get('error', '')}", flush=True)

    def served(server) -> dict:
        try:
            index = json.loads((server.bundle_dir / "view.json").read_text())
            clip = index["clips"][0]
            frames = clip["frames"]
            size = sum((server.bundle_dir / f).stat().st_size for f in frames)
            return {"representation": clip["representation"], "frames": len(frames),
                    "rung": clip["detail"]["rung"], "bytes": size,
                    "mbit_s": size * 8 * FPS / len(frames) / 1e6,
                    "notes": clip["notes"], "fps": index["fps"]}
        finally:
            server.shutdown()

    def vega():
        frames = load_cached(args.orbit_cache / "basketball")[: args.native_frames]
        artifact = out / "vega-basketball.vmesh"
        artifact.unlink(missing_ok=True)
        start = time.perf_counter()
        open4d.encode(frames, artifact, codec="vega", key_iterations=800,
                      residual_iterations=600)
        encode_seconds = time.perf_counter() - start
        with open4d.load(artifact) as native:
            server = open4d.stream(native, out_dir=out / "vega-bundle", rungs=["splat"],
                                   open_browser=False, block=False)
        return {"encode_seconds": encode_seconds, "codec_bytes": artifact.stat().st_size,
                "codec_mbit_s": artifact.stat().st_size * 8 * FPS / len(frames) / 1e6,
                **served(server)}

    def queen():
        run = open4d.GaussianRun(method="queen", path=args.queen_run,
                                 source=args.queen_run.parent / "calibrated-dynerf",
                                 runtime=Path(open4d.__file__).parent / "reconstruction" / "gs_tools",
                                 python=sys.executable)
        server = open4d.stream(run, out_dir=out / "queen-bundle",
                               rungs=["ply", "splat@25%"], fps=FPS,
                               open_browser=False, block=False)
        return served(server)

    def native_vmesh(path: Path):
        def run():
            with open4d.load(path) as native:
                server = open4d.stream(native, out_dir=out / f"{path.stem}-bundle",
                                       rungs=["splat"], open_browser=False, block=False)
            return {"artifact": str(path), "codec_bytes": path.stat().st_size,
                    **served(server)}
        return run

    attempt("vega .vmesh -> NativeSequence -> open4d.stream", vega)
    attempt("QUEEN GaussianRun -> open4d.stream", queen)
    for path in args.native_vmesh:
        attempt(f"{path.name} -> NativeSequence -> open4d.stream", native_vmesh(path))
    return {"rows": rows}


# --------------------------------------------------------------- showcase ---

#: ORBIT rendered its views from these captures, frame for frame, so a mesh pane
#: and a Gaussian pane of one subject show the same instant.
PAIRS = {"C3": "basketball", "C4": "dancer", "C5": "mitch", "C6": "thomas"}
SHOWCASE_MESH = ["draco", "draco@11", "draco@8"]
MM_TO_M = 1e-3
SHOWCASE_GAUSSIANS = ["splat@25%", "splat@50%", "splat@10%"]


def _yaw(quarter_turns: int) -> tuple[np.ndarray, np.ndarray]:
    """A rotation about +y by quarter turns: its matrix and (w, x, y, z)."""
    angle = quarter_turns * math.pi / 2
    c, s = round(math.cos(angle)), round(math.sin(angle))
    matrix = np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]], dtype=np.float64)
    quaternion = np.array([math.cos(angle / 2), 0, math.sin(angle / 2), 0])
    return matrix, quaternion


def _compose(left: np.ndarray, right: np.ndarray) -> np.ndarray:
    """Hamilton product ``left * right`` of one quaternion with many, (w, x, y, z)."""
    w1, x1, y1, z1 = left
    w2, x2, y2, z2 = right.T
    return np.stack([w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2,
                     w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2,
                     w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
                     w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2], axis=1).astype(np.float32)


def _turns(mesh_positions: np.ndarray, splat_positions: np.ndarray) -> int:
    """Quarter turns about +y that best lay a capture onto ORBIT's splats.

    ORBIT placed some subjects facing the other way, which bounding boxes
    cannot see -- a half turn leaves them unchanged. Chosen by symmetric
    nearest-neighbour distance between the centred point sets.
    """
    from scipy.spatial import cKDTree

    mesh_positions = mesh_positions - mesh_positions.mean(0)
    splat_positions = splat_positions - splat_positions.mean(0)
    tree = cKDTree(splat_positions)

    def distance(turns):
        moved = mesh_positions @ _yaw(turns)[0].T
        return (tree.query(moved)[0].mean() + cKDTree(moved).query(splat_positions)[0].mean())

    return min(range(4), key=distance)


def phase_showcase(args) -> dict:
    """Mesh and Gaussian clips of the same subjects, in one bundle to watch."""
    import open4d
    import streamer
    from open4d.core import SequenceView
    from streamer import score

    out = args.out / "bundles" / "showcase"
    rows = []
    with streamer.Bundle(out, title="Open4D streamer: meshes and Gaussians",
                         fps=FPS) as bundle:
        for capture, subject in PAIRS.items():
            cache = args.orbit_cache / subject
            meta = json.loads((cache / "meta.json").read_text())
            with open4d.load(args.mesh_data / capture, fps=FPS) as sequence:
                window = SequenceView(sequence, range(meta["frames"]))
                mesh = bundle.add(window, name=f"{subject}-mesh", scene=subject,
                                  method="mesh (Draco)", rungs=SHOWCASE_MESH, score=True)
                first_mesh = np.asarray(window[0].geometry.positions, dtype=np.float64)
            # The viewer gives a scene one camera, so its clips must share a
            # coordinate frame. ORBIT placed each capture in a stage, in
            # metres; the captures are in millimetres about their own origin.
            # ORBIT records no transform. The scale is the unit change, which
            # two subjects' bounds confirm to four digits; the shift aligns the
            # two bounding-box centres over the same frames, and the residual
            # is how far the boxes still disagree.
            # ORBIT = scale * R * capture + shift, with R a turn about +y.
            cached = load_cached(cache)
            scale = MM_TO_M
            turns = _turns(first_mesh * scale, np.asarray(cached[0].positions, np.float64))
            rotation, quaternion = _yaw(turns)
            inverse = quaternion * np.array([1, -1, -1, -1])
            corners = np.array([[x, y, z] for x in (0, 1) for y in (0, 1) for z in (0, 1)])
            lower, upper = np.asarray(mesh.bounds_min), np.asarray(mesh.bounds_max)
            turned = (lower + corners * (upper - lower)) @ rotation.T
            t_lower, t_upper = turned.min(0), turned.max(0)
            o_lower, o_upper = np.asarray(meta["bounds_min"]), np.asarray(meta["bounds_max"])
            shift = (o_lower + o_upper) / 2 - scale * (t_lower + t_upper) / 2
            residual = (o_upper - (scale * t_upper + shift)) / scale
            frames = [open4d.GaussianSplats(
                positions=((g.positions - shift) / scale) @ rotation,
                scales=g.scales / scale, rotations=_compose(inverse, g.rotations),
                opacities=g.opacities, spherical_harmonics=g.spherical_harmonics)
                for g in cached]
            del cached
            splats = bundle.add(frames, name=f"{subject}-gaussians", scene=subject,
                                method="Gaussians (.splat)", rungs=SHOWCASE_GAUSSIANS,
                                notes=[f"moved into the capture's frame: ORBIT metres "
                                       f"x {1 / scale:.0f}, turned {turns * 90} degrees "
                                       f"about +y and shifted; bounds agree to "
                                       f"{np.abs(residual).max():.1f} mm"])
            del frames

            def to_orbit(arrays, scale=scale, shift=shift, rotation=rotation,
                         quaternion=quaternion):
                return {**arrays,
                        "positions": (arrays["positions"] @ rotation.T) * scale + shift,
                        "scales": arrays["scales"] * scale,
                        "rotations": _compose(quaternion, arrays["rotations"])}

            renderer = Renderer(args.orbit_data, subject, meta)
            splats.detail["quality"] = pixel_quality(
                renderer, [out / f for f in splats.frames], cache / "gt",
                dc_only=True, to_orbit=to_orbit)
            for variant in splats.variants:
                variant["quality"] = pixel_quality(
                    renderer, [out / f for f in variant["frames"]], cache / "gt",
                    dc_only=True, to_orbit=to_orbit)
            rows.append({"subject": subject, "capture": capture,
                         "alignment": {"scale": scale, "shift": shift.tolist(),
                                       "yaw_degrees": turns * 90,
                                       "bounds_residual_mm": residual.tolist()},
                         "mesh": rung_rows(mesh, out, {}, "mesh"),
                         "gaussians": rung_rows(splats, out, {}, "gaussians")})
            print(f"{subject}: mesh + gaussians", flush=True)
    return {"bundle": str(out), "rows": rows}


# --------------------------------------------------------------- delivery ---


def fetch_rung(base: str, frames: list[str], *, connections: int = 1) -> dict:
    """GET every frame, as the client does, and time it."""
    latencies = []

    def get(frame):
        start = time.perf_counter()
        with urllib.request.urlopen(base + frame, timeout=120) as response:
            size = len(response.read())
        latencies.append(time.perf_counter() - start)
        return size

    start = time.perf_counter()
    if connections == 1:
        total = sum(get(frame) for frame in frames)
    else:
        with concurrent.futures.ThreadPoolExecutor(connections) as pool:
            total = sum(pool.map(get, frames))
    seconds = time.perf_counter() - start
    return {
        "frames": len(frames), "bytes": total, "seconds": seconds,
        "mbit_s": total * 8 / seconds / 1e6,
        "frames_per_second": len(frames) / seconds,
        "realtime": len(frames) / seconds >= FPS,
        "latency_ms_p50": 1000 * statistics.median(latencies),
        "latency_ms_p95": 1000 * float(np.percentile(latencies, 95)),
    }


def ladders_of(index: dict, clips: list[str] | None) -> list[tuple[str, list[tuple[str, list[str]]]]]:
    found = []
    for clip in index["clips"]:
        if clips and clip["name"] not in clips:
            continue
        rungs = [(clip["detail"]["rung"], clip["frames"])]
        rungs += [(variant["name"], variant["frames"]) for variant in clip["variants"]]
        found.append((clip["name"], rungs))
    return found


def measure_server(base: str, *, clips, frame_limit, connections) -> list[dict]:
    with urllib.request.urlopen(base + "view.json", timeout=30) as response:
        index = json.loads(response.read())
    rows = []
    for clip, rungs in ladders_of(index, clips):
        for rung, frames in rungs:
            for count in connections:
                result = fetch_rung(base, frames[:frame_limit], connections=count)
                rows.append({"clip": clip, "rung": rung, "connections": count, **result})
                print(f"  {clip} {rung} x{count}: {result['mbit_s']:.0f} Mbit/s, "
                      f"{result['frames_per_second']:.1f} frames/s, "
                      f"p95 {result['latency_ms_p95']:.0f} ms", flush=True)
    return rows


def phase_delivery(args) -> dict:
    import streamer

    results = {}
    shaped = {"name": "shaped 100 Mbit/s, 20 ms one-way",
              "link": lambda: streamer.Link(capacity=100e6, latency=0.02)}
    for kind, clips, limit in (("mesh", args.delivery_mesh, None),
                               ("gaussians", args.delivery_gaussians, args.gaussian_frames)):
        bundle = args.out / "bundles" / kind
        for condition in ({"name": "loopback", "link": lambda: None}, shaped):
            server = streamer.serve(bundle, port=0, block=False, open_browser=False,
                                    link=condition["link"]())
            base = f"http://127.0.0.1:{server.server_address[1]}/"
            print(f"{kind} / {condition['name']}", flush=True)
            try:
                rows = measure_server(base, clips=clips, frame_limit=limit,
                                      connections=(1, 4))
            finally:
                server.shutdown()
            results[f"{kind} / {condition['name']}"] = rows
    return {"conditions": results}


def phase_fetch(args) -> dict:
    """Run from the machine a viewer sits at, against a server left running."""
    results = {}
    for base, clips, limit in ((args.mesh_url, args.delivery_mesh, None),
                               (args.gaussian_url, args.delivery_gaussians,
                                args.gaussian_frames)):
        if not base:
            continue
        base = base if base.endswith("/") else base + "/"
        print(base, flush=True)
        results[base] = measure_server(base, clips=clips, frame_limit=limit,
                                       connections=(1, 4))
    return {"client": machine(), "conditions": results}


# --------------------------------------------------------------- playback ---


def study_traces(directory: Path) -> dict:
    """The study's bandwidth traces, as `streamer.link.Trace`s.

    They are ``time,bandwidth`` CSVs in Mbit/s, which `Trace.read` -- two
    whitespace columns in bit/s -- does not parse. Some hold outages, zero
    bandwidth, which `Trace` refuses; those are floored at `OUTAGE_BPS`, which
    is an outage for every rung here.
    """
    from streamer.link import Trace

    traces = {}
    for path in sorted(directory.glob("*.csv")):
        with path.open() as handle:
            points = [(float(row["time"]), max(OUTAGE_BPS, float(row["bandwidth"]) * 1e6))
                      for row in csv.DictReader(handle)]
        if points:
            traces[path.stem] = (Trace.steps(*points, loop=True), points)
    return traces


def play(ladders, link_factory, metric, *, policy="adaptive", seconds=30.0) -> dict:
    from streamer import playback

    run = playback.Playback(ladders, link_factory(), fps=FPS, metric=metric,
                            target_buffer=3.0)
    if policy != "adaptive":
        which = 0 if policy == "fixed lowest" else -1
        run.chooser = lambda rate, buffers: {clip: rungs[which]
                                             for clip, rungs in run.ladders.items()}
    report = run.run(seconds).as_dict()
    report.pop("link", None)
    return report


def phase_playback(args) -> dict:
    from streamer import link, policy, score

    traces = study_traces(args.traces)
    results = {}
    for kind, metric, capacities in (
        ("mesh", score.METRIC, [1, 2, 5, 10, 20, 50, 100]),
        ("gaussians", "psnr", [25, 50, 100, 200, 500, 1000, 2000]),
    ):
        bundle = args.out / "bundles" / kind
        if not (bundle / "view.json").is_file():
            continue
        ladders = policy.measured_rungs(bundle)
        conditions = [(f"{c} Mbit/s", (lambda c=c: link.Link(capacity=c * 1e6, latency=0.02,
                                                             clock=lambda: 0.0)))
                      for c in capacities]
        conditions += [(f"trace {name}", (lambda t=trace: link.Link(trace=t, latency=0.02,
                                                                    clock=lambda: 0.0)))
                       for name, (trace, _) in traces.items()]
        rows = []
        for label, factory in conditions:
            for chooser in ("adaptive", "fixed lowest", "fixed highest"):
                single = [play([ladder], factory, metric, policy=chooser)
                          for ladder in ladders]
                panes = play(ladders[: args.panes], factory, metric, policy=chooser)
                rows.append({
                    "condition": label, "policy": chooser,
                    "single_pane": {
                        key: float(np.mean([r[key] for r in single]))
                        for key in ("stalled_seconds", "frozen_seconds", "switches",
                                    "mean_quality", "score")
                    },
                    "panes": args.panes, "multi_pane": panes,
                })
            print(f"{kind} {label}: adaptive {rows[-3]['single_pane']['mean_quality']:.1f} "
                  f"dB, stall {rows[-3]['single_pane']['stalled_seconds']:.1f} s", flush=True)
        results[kind] = {
            "metric": metric,
            "ladders": [[{"clip": r.clip, "rung": r.name,
                          "mbit_s": r.bits_per_second / 1e6,
                          "quality": r.quality.get(metric)} for r in ladder]
                        for ladder in ladders],
            "rows": rows,
        }
    return {"seconds": 30.0, "target_buffer": 3.0, "latency_s": 0.02,
            "traces": {name: {"mean_mbit_s": float(np.mean([p[1] for p in pts]) / 1e6),
                              "min_mbit_s": float(min(p[1] for p in pts) / 1e6),
                              "outage_seconds": sum(1 for p in pts if p[1] <= OUTAGE_BPS)}
                       for name, (_, pts) in traces.items()},
            "kinds": results}


# ------------------------------------------------------------------- main ---


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("phase", choices=["mesh", "codecs", "gaussians", "native",
                                          "showcase", "delivery", "fetch", "playback"])
    parser.add_argument("--out", type=Path, default=Path("out"))
    parser.add_argument("--mesh-data", type=Path)
    parser.add_argument("--captures", nargs="*", default=list(MESH_CAPTURES))
    parser.add_argument("--codec-captures", nargs="*", default=["C3"])
    parser.add_argument("--codec-rungs", nargs="*", default=CODEC_RUNGS)
    parser.add_argument("--codec-frames", type=int, default=CODEC_FRAMES)
    parser.add_argument("--orbit-data", type=Path)
    parser.add_argument("--orbit-cache", type=Path)
    parser.add_argument("--subjects", nargs="*")
    parser.add_argument("--native-frames", type=int, default=10)
    parser.add_argument("--queen-run", type=Path)
    parser.add_argument("--native-vmesh", type=Path, nargs="*", default=[])
    parser.add_argument("--delivery-mesh", nargs="*", default=["basketball_player"])
    parser.add_argument("--delivery-gaussians", nargs="*", default=["basketball"])
    parser.add_argument("--gaussian-frames", type=int, default=None,
                        help="frames per Gaussian rung to fetch")
    parser.add_argument("--mesh-url")
    parser.add_argument("--gaussian-url")
    parser.add_argument("--traces", type=Path,
                        default=HERE.parent / "study/system/Client/traces")
    parser.add_argument("--panes", type=int, default=4)
    parser.add_argument("--label", help="results file suffix, for fetch runs")
    args = parser.parse_args(argv)

    phase = {"mesh": phase_mesh, "codecs": phase_codecs, "gaussians": phase_gaussians,
             "native": phase_native, "showcase": phase_showcase,
             "delivery": phase_delivery, "fetch": phase_fetch,
             "playback": phase_playback}[args.phase]
    start = time.perf_counter()
    result = phase(args)
    result["wall_seconds"] = time.perf_counter() - start
    write_result(args.out, args.phase + (f"-{args.label}" if args.label else ""), result)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
