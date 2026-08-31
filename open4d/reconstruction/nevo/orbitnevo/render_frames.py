"""Render a trained ReRF sequence to images, with and without NeVo's filtering.

Two conditions come out of one pass:

``rerf``
    The whole feature voxel grid rendered as trained. This is the system NeVo
    is measured against, not NeVo.
``nevo<t>``
    The same frame with every feature voxel whose neural visibility falls below
    ``t`` removed -- NeVo's section 3.2, the visibility-aware optimisation. A
    dropped block is written back the way ReRF's decoder fills a block that
    never arrived (raw density -4.1, zero features), not zeroed.

Those are the artefacts another representation's output gets compared against,
so the things that must match on both sides are what ``manifest.json`` records:
camera extrinsics and intrinsics, resolution, and the white background the
corpus composites onto. The captured image from the same camera is written
alongside.

    python -m orbitnevo.render_frames \\
        --config baselines/NeVo/rerf/configs/nevo/g_basketball.py \\
        --out ~/nevo_output --view 7

Two things this does *not* do, both from the paper's section 3.2 and both
noted in RESULTS.md: the threshold is passed in rather than fitted per video
against an SSIM target, and the selected set is not dilated by 20 cm to absorb
viewport-prediction error. Filtering here therefore sees a perfectly predicted
viewport, which is the optimistic end of the design.

Runs in the ``nevo`` environment.
"""
from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

import numpy as np

MODULE_ROOT = Path(__file__).resolve().parents[1]
if str(MODULE_ROOT) not in sys.path:
    sys.path.insert(0, str(MODULE_ROOT))

from nevo import rerf_env  # noqa: E402

PAPER_THRESHOLD = 0.025
"""The importance threshold NeVo's section 3.2 quotes as its worked example."""


def _save(path: Path, image: np.ndarray) -> None:
    from PIL import Image

    path.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray((np.clip(image, 0.0, 1.0) * 255.0).astype(np.uint8)).save(path)


def run(args) -> dict:
    from nevo.blocks import BlockGrid
    from nevo.filtering import voxels_dropped
    from nevo.importance import ImportanceConfig, ImportanceScorer
    from nevo.render import held_out_view, render_view
    from nevo.sequence import ReRFSequence

    sequence = ReRFSequence(args.config)
    with open(sequence.corpus_dir / "nevo_corpus.json") as handle:
        manifest = json.load(handle)
    frames = sequence.available_frames()
    if not frames:
        raise RuntimeError(f"no trained frames under {sequence.run_dir}")
    if args.frames > 0:
        frames = frames[: args.frames]

    view = args.view if args.view >= 0 else int(manifest["cameras"][-1]["camera_id"])
    training_views = manifest.get("training_views") or [
        camera["camera_id"] for camera in manifest["cameras"]
    ]
    out_dir = Path(args.out).expanduser().resolve() / sequence.cfg.expname
    out_dir.mkdir(parents=True, exist_ok=True)
    thresholds = list(args.thresholds)

    print(
        f"{sequence.cfg.expname}: {len(frames)} frames at camera {view}, "
        f"conditions: rerf + {['nevo@%s' % t for t in thresholds]}",
        flush=True,
    )
    started = time.time()
    camera = None
    kept = {threshold: [] for threshold in thresholds}
    for frame_index in frames:
        camera, truth = held_out_view(sequence, frame_index, view=view)
        frame = sequence.frame(frame_index)
        _save(out_dir / f"frame_{frame_index:03d}.png", render_view(sequence, frame, camera))
        _save(out_dir / f"reference_{frame_index:03d}.png", truth)

        if thresholds:
            scorer = ImportanceScorer(
                sequence, frame, ImportanceConfig(block_size=args.block_size)
            )
            grid = BlockGrid(frame.grid_shape, args.block_size)
            scores = scorer.score(camera)
            occupied = int(scorer.occupancy.sum().item())
            for threshold in thresholds:
                keep = (scores >= threshold) & scorer.occupancy
                with voxels_dropped(frame, grid, keep):
                    rendered = render_view(sequence, frame, camera)
                _save(out_dir / f"nevo{threshold}_{frame_index:03d}.png", rendered)
                kept[threshold].append(int(keep.sum().item()) / max(occupied, 1))
            del scorer
        print(f"  frame {frame_index:3d}", flush=True)
        del frame
    elapsed = time.time() - started

    conditions = [
        {
            "name": "rerf",
            "prefix": "frame",
            "label": "ReRF (all feature voxels)",
            "threshold": None,
            "kept_fraction": 1.0,
        }
    ]
    for threshold in thresholds:
        conditions.append(
            {
                "name": f"nevo{threshold}",
                "prefix": f"nevo{threshold}",
                "label": f"NeVo (visibility-filtered, t={threshold})",
                "threshold": threshold,
                "kept_fraction": float(np.mean(kept[threshold])),
            }
        )

    payload = {
        "name": sequence.cfg.expname,
        "representation": "ReRF (NeRF feature voxels)",
        "frames": frames,
        "view": view,
        "view_in_training_set": view in training_views,
        "training_views": training_views,
        "width": camera.width,
        "height": camera.height,
        "intrinsics": {"fx": camera.fx, "fy": camera.fy, "cx": camera.cx, "cy": camera.cy},
        "c2w": camera.c2w.tolist(),
        "background": "white (corpus composites rgb*alpha + (1-alpha))",
        "block_size": args.block_size,
        "conditions": conditions,
        "corpus": str(sequence.corpus_dir),
        "run_dir": str(sequence.run_dir),
        "seconds": elapsed,
    }
    with open(out_dir / "manifest.json", "w") as handle:
        json.dump(payload, handle, indent=1)
    for condition in conditions:
        print(
            f"  {condition['label']}: {condition['kept_fraction'] * 100:.1f}% of "
            f"non-empty blocks kept",
            flush=True,
        )
    print(f"wrote {out_dir} ({elapsed:.0f}s)", flush=True)
    return payload


def parse_args(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--config", nargs="+", required=True)
    parser.add_argument("--out", default="~/nevo_output")
    parser.add_argument("--view", type=int, default=-1,
                        help="camera to render from; default is the corpus's last")
    parser.add_argument("--frames", type=int, default=0, help="0 = every trained frame")
    parser.add_argument("--thresholds", type=float, nargs="*", default=[PAPER_THRESHOLD],
                        help="one NeVo condition per importance threshold; empty = ReRF only")
    parser.add_argument("--block-size", type=int, default=8,
                        help="filtering unit; 8 is ReRF's codec block")
    return parser.parse_args(argv)


def main(argv=None) -> int:
    args = parse_args(argv)
    rerf_env.activate()
    configs = list(args.config)
    for config in configs:
        args.config = config
        run(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
