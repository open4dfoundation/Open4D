"""Render a loaded frame, to check that step 1 loaded it correctly.

:func:`check_against_rerf` in ``nevo.importance`` proves the marching
transcription matches the vendored model -- but it compares a model against
*itself*, so it would pass just as happily on a model reassembled wrongly from
its checkpoint. Reconstructing a ``DirectVoxGO`` from ReRF's files takes half a
dozen fixups (the shared colour MLP arrives in a separate file, the
residual/deform switches come from the config rather than the checkpoint, and
a P-frame's feature grid is a residual that only means anything once
``former_k0_cur`` is wired up), and getting any of them wrong yields a model
that still renders, just not the trained scene.

So: render a training viewpoint and compare against the image ReRF was trained
on. A correctly reassembled frame lands within a dB or so of the PSNR the
trainer logged; a mis-wired one is tens of dB off.
"""
from __future__ import annotations

import json
from pathlib import Path
from typing import Optional, Tuple

import numpy as np

from . import rerf_env
from .cameras import Camera


def render_view(sequence, frame, camera: Camera, chunk: int = 1 << 19) -> np.ndarray:
    """Render one viewport as an ``[H, W, 3]`` float array in [0, 1]."""
    rerf_env.activate()
    with rerf_env.rerf_cwd():
        import torch
        from lib import dvgo

    render_kwargs = sequence.render_kwargs()
    with torch.no_grad():
        c2w = torch.tensor(camera.c2w, dtype=torch.float32, device="cuda")
        intrinsics = torch.tensor(
            camera.intrinsic_matrix, dtype=torch.float32, device="cuda"
        )
        rays_o, rays_d, viewdirs = dvgo.get_rays_of_a_view(
            H=camera.height,
            W=camera.width,
            K=intrinsics,
            c2w=c2w,
            ndc=False,
            inverse_y=render_kwargs["inverse_y"],
            flip_x=render_kwargs["flip_x"],
            flip_y=render_kwargs["flip_y"],
        )
        rays_o = rays_o.flatten(0, -2)
        rays_d = rays_d.flatten(0, -2)
        viewdirs = viewdirs.flatten(0, -2)
        pieces = [
            frame.model(
                rays_o[begin : begin + chunk].contiguous(),
                rays_d[begin : begin + chunk].contiguous(),
                viewdirs[begin : begin + chunk].contiguous(),
                **render_kwargs,
            )["rgb_marched"]
            for begin in range(0, len(rays_o), chunk)
        ]
        image = torch.cat(pieces).reshape(camera.height, camera.width, 3)
    return image.clamp(0.0, 1.0).cpu().numpy()


def training_view(sequence, frame_index: int, view: int = 0) -> Tuple[Camera, np.ndarray]:
    """A camera and its ground-truth image, straight from the corpus.

    The image is composited onto white, matching ``white_bkgd=True`` in the
    config: ``lib.load_data`` does ``rgb * alpha + (1 - alpha)`` before the
    trainer ever sees it.
    """
    with open(sequence.corpus_dir / ("cams_%d.json" % frame_index)) as handle:
        frames = sorted(json.load(handle)["frames"], key=lambda d: d["file"])
    if view >= len(frames):
        raise IndexError(
            f"cams_{frame_index}.json lists {len(frames)} training cameras; {view} is not one "
            "of them (a held-out camera is absent by design -- use held_out_view)"
        )
    entry = frames[view]
    truth = _composite(entry["file"], entry["mask"])

    intrinsic = np.asarray(entry["intrinsic"], dtype=np.float64)
    height, width = truth.shape[:2]
    camera = Camera(
        camera_id=view,
        width=width,
        height=height,
        fx=float(intrinsic[0, 0]),
        fy=float(intrinsic[1, 1]),
        cx=float(intrinsic[0, 2]),
        cy=float(intrinsic[1, 2]),
        c2w=np.asarray(entry["extrinsic"], dtype=np.float64),
    )
    return camera, truth


def _composite(image_path, mask_path) -> np.ndarray:
    """Load a corpus view over white, the way ``lib.load_data`` does.

    ``rgb * alpha + (1 - alpha)`` with ``white_bkgd=True`` -- the trainer never
    sees the black-backed PNG, so neither should anything scored against it.
    """
    from PIL import Image

    rgb = np.asarray(Image.open(image_path).convert("RGB"), dtype=np.float32) / 255.0
    alpha = (
        np.asarray(Image.open(mask_path).convert("L"), dtype=np.float32)[..., None] / 255.0
    )
    return rgb * alpha + (1.0 - alpha)


def held_out_view(sequence, frame_index: int, view: Optional[int] = None):
    """The camera kept out of training, and its captured image.

    Deliberately not read from ``cams_<frame>.json``: that file *is* the
    training set (ReRF's NHR loader trains on every entry), so the held-out
    camera is absent from it by construction. Its calibration lives in the
    corpus manifest and its pixels are on disk beside the training views.
    """
    with open(sequence.corpus_dir / "nevo_corpus.json") as handle:
        manifest = json.load(handle)
    if view is None:
        view = manifest.get("holdout_view")
    if view is None:
        raise ValueError(
            f"{sequence.corpus_dir} has no held-out camera; it was prepared without "
            "--holdout-view, so every view was trained on"
        )
    entry = next(
        (item for item in manifest["cameras"] if int(item["camera_id"]) == int(view)), None
    )
    if entry is None:
        raise ValueError(f"camera {view} is not in the corpus manifest")

    directory = sequence.corpus_dir
    truth = _composite(
        directory / "image" / str(frame_index) / ("img_%04d.png" % view),
        directory / "mask" / str(frame_index) / ("img_%04d.png" % view),
    )
    height, width = truth.shape[:2]
    camera = Camera(
        camera_id=int(view),
        width=width,
        height=height,
        fx=float(entry["fx"]),
        fy=float(entry["fy"]),
        cx=float(entry["cx"]),
        cy=float(entry["cy"]),
        c2w=np.asarray(entry["c2w_normalised"], dtype=np.float64),
    )
    return camera, truth


def psnr(prediction: np.ndarray, truth: np.ndarray) -> float:
    error = float(np.mean((prediction.astype(np.float64) - truth.astype(np.float64)) ** 2))
    if error <= 0.0:
        return float("inf")
    return float(-10.0 * np.log10(error))


def check_reload(sequence, frame, view: int = 0, save_to: Optional[Path] = None) -> dict:
    """Render a training view of ``frame`` and score it against the ground truth."""
    camera, truth = training_view(sequence, frame.index, view)
    rendered = render_view(sequence, frame, camera)
    score = psnr(rendered, truth)
    if save_to is not None:
        from PIL import Image

        side_by_side = np.concatenate([rendered, truth], axis=1)
        Image.fromarray((side_by_side * 255.0).astype(np.uint8)).save(save_to)
    return {
        "frame": frame.index,
        "view": view,
        "psnr": score,
        "size": [camera.width, camera.height],
    }
