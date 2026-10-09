#!/usr/bin/env python3
"""Turn ORBIT's calibrated multi-view renders into Gaussian sequences.

The ORBIT Gaussian corpus is training data -- eight calibrated views of each
frame, no splats -- so the benchmark needs a Gaussian sequence made from it
first. This uses Vega's own ORBIT loader (`vega.datasets.orbit_gaussian`):
carve a visual hull per frame, seed Gaussians in it, refine them against the
views. The same recipe, at the same settings, that this repository's ORBIT
audit used, so the result is a sequence of real `GaussianSplats` frames.

Writes, per subject::

    <out>/<subject>/frame_000000.npz ...  positions, scales, rotations,
                                          opacities, spherical_harmonics
    <out>/<subject>/gt/f000_v0.png ...    the ORBIT views at render size
    <out>/<subject>/meta.json             cameras, timings, counts

Needs CUDA and the Vega research tree on ``PYTHONPATH``
(``open4d/reconstruction/vega``)::

    python orbit_splats.py /path/to/ORBIT_datasets_gaussian out/orbit
"""

from __future__ import annotations

import argparse
import json
import time
from pathlib import Path

import numpy as np

#: Render scale against the 4096x3072 originals: 512x384, as the audit used.
IMAGE_SCALE = 0.125
MAX_POINTS = 70_000
REFINE_ITERATIONS = 200


def to_arrays(gaussians) -> dict[str, np.ndarray]:
    """A Vega `GaussianSet` as `open4d.GaussianSplats` fields, activated."""
    sh = np.concatenate([gaussians.sh_dc.detach().cpu().numpy(),
                         gaussians.sh_rest.detach().cpu().numpy()], axis=1)
    return {
        "positions": gaussians.get_xyz.detach().cpu().numpy(),
        "scales": gaussians.get_scaling.detach().cpu().numpy(),
        "rotations": gaussians.get_rotation.detach().cpu().numpy(),
        "opacities": gaussians.get_opacity.detach().cpu().numpy().reshape(-1),
        "spherical_harmonics": sh,
    }


def build(data: Path, subject: str, out: Path, frames: int | None) -> dict:
    import torch
    from PIL import Image
    from vega.datasets import orbit_gaussian as og

    transforms = og.load_object_transforms(data, subject)
    available = len(og.group_frames(transforms))
    indices = list(range(available if frames is None else min(frames, available)))
    start = time.perf_counter()
    splats, cameras, truth, lower, upper = og.load_scene(
        data, subject, indices, device="cuda", max_points_per_frame=MAX_POINTS,
        image_scale=IMAGE_SCALE, refine_iters=REFINE_ITERATIONS, load_gt_images=True,
    )
    seconds = time.perf_counter() - start

    target = out / subject
    (target / "gt").mkdir(parents=True, exist_ok=True)
    for index, gaussians in enumerate(splats):
        np.savez(target / f"frame_{index:06d}.npz", **to_arrays(gaussians))
        for view, image in enumerate(truth[index]):
            pixels = image.permute(1, 2, 0).clamp(0, 1).cpu().numpy()
            Image.fromarray((pixels * 255).round().astype(np.uint8)).save(
                target / "gt" / f"f{index:03d}_v{view}.png")
    meta = {
        "subject": subject,
        "frames": len(splats),
        "source_frame_indices": indices,
        "views": len(cameras),
        "image_scale": IMAGE_SCALE,
        "max_points_per_frame": MAX_POINTS,
        "refine_iterations": REFINE_ITERATIONS,
        "sh_degree": int(splats[0].sh_degree),
        "splats": [len(g.xyz) for g in splats],
        "build_seconds": seconds,
        "bounds_min": lower.tolist(),
        "bounds_max": upper.tolist(),
        "gpu": torch.cuda.get_device_name(0),
    }
    (target / "meta.json").write_text(json.dumps(meta, indent=2))
    return meta


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("data", type=Path)
    parser.add_argument("out", type=Path)
    parser.add_argument("--subjects", nargs="*")
    parser.add_argument("--frames", type=int)
    args = parser.parse_args(argv)
    dataset = json.loads((args.data / "dataset.json").read_text())
    subjects = args.subjects or [entry["name"] for entry in dataset["objects"]]
    for subject in subjects:
        if (args.out / subject / "meta.json").is_file():
            print(f"{subject}: cached", flush=True)
            continue
        meta = build(args.data, subject, args.out, args.frames)
        print(f"{subject}: {meta['frames']} frames, "
              f"{int(np.mean(meta['splats']))} splats/frame, "
              f"{meta['build_seconds']:.0f} s", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
