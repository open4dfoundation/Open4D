"""Rate-distortion sweep: what a ReRF frame costs, and what it looks like.

Produces the numbers a comparison against another volumetric representation
(Vega, say) needs, on both axes at once:

* **bytes** -- ReRF's own encoder run over the retained blocks, plus the block
  mask and, on P-frames, the motion vectors. See ``nevo.bitstream`` for exactly
  what is and is not counted.
* **quality** -- the delivered content rendered at a camera the model never
  trained on, scored against that camera's captured image, on the subject's
  bounding box. See ``nevo.metrics``.

Sweeping the importance threshold traces one system's rate-distortion curve.
The unfiltered end (threshold 0) is plain ReRF; everything above it is NeVo's
visibility filtering.

    python -m orbitnevo.rd_sweep \\
        --config baselines/NeVo/rerf/configs/nevo/h_basketball.py \\
        --out ~/nevo_results/rd_h_basketball

Writes ``results.csv`` (one row per threshold x frame, plus per-threshold
means), ``summary.json``, and ``renders/`` holding every rendered frame next to
the held-out reference.

Runs in the ``nevo`` environment.
"""
from __future__ import annotations

import argparse
import csv
import json
import sys
import time
from pathlib import Path

import numpy as np

MODULE_ROOT = Path(__file__).resolve().parents[1]
if str(MODULE_ROOT) not in sys.path:
    sys.path.insert(0, str(MODULE_ROOT))

from nevo import rerf_env  # noqa: E402

DEFAULT_THRESHOLDS = (0.0, 0.01, 0.05, 0.15, 0.35)
"""Five points spanning unfiltered to aggressive. 0.0 keeps every occupied
block and is plain ReRF -- the anchor the rest of the curve is read against."""


def _save_png(path: Path, image: np.ndarray) -> None:
    from PIL import Image

    path.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray((np.clip(image, 0.0, 1.0) * 255.0).astype(np.uint8)).save(path)


def run(args) -> dict:
    from nevo.bitstream import SequenceCoder, startup_bytes
    from nevo.blocks import BlockGrid
    from nevo.filtering import voxels_dropped
    from nevo.importance import ImportanceConfig, ImportanceScorer
    from nevo.metrics import QualityScorer, silhouette_box
    from nevo.render import held_out_view, render_view
    from nevo.sequence import ReRFSequence

    sequence = ReRFSequence(args.config)
    with open(sequence.corpus_dir / "nevo_corpus.json") as handle:
        manifest = json.load(handle)
    holdout = manifest.get("holdout_view")
    training_views = manifest.get("training_views") or [
        camera["camera_id"] for camera in manifest["cameras"]
    ]
    if args.view >= 0:
        eval_view = args.view
    elif holdout is not None:
        eval_view = holdout
    else:
        # Corpora prepared before --holdout-view existed trained on every
        # camera. Still usable for producing comparable renders and byte
        # counts, but the quality figures flatter the model, so say so loudly
        # here and carry the fact into the summary and the page.
        eval_view = int(manifest["cameras"][-1]["camera_id"])
    held_out = eval_view not in training_views
    if not held_out:
        print(
            f"WARNING: camera {eval_view} is in the training set "
            f"{training_views}. Quality numbers are optimistic; bytes are unaffected.",
            flush=True,
        )
    trained = sequence.available_frames()
    if not trained:
        raise RuntimeError(f"no trained frames under {sequence.run_dir}")
    frames = trained[: args.frames] if args.frames > 0 else trained
    thresholds = list(args.thresholds)

    out_dir = Path(args.out).expanduser().resolve()
    renders = out_dir / "renders"
    renders.mkdir(parents=True, exist_ok=True)
    print(
        f"{sequence.cfg.expname}: {len(frames)} frames x {len(thresholds)} thresholds, "
        f"camera {eval_view} ({'held out' if held_out else 'IN TRAINING SET'}), "
        f"trained on {training_views}",
        flush=True,
    )

    # One crop for the whole sweep, so every number is over the same pixels.
    references = {}
    mattes = []
    for frame_index in frames:
        camera, truth = held_out_view(sequence, frame_index, view=eval_view)
        references[frame_index] = (camera, truth)
        mattes.append((truth < 0.999).any(axis=2))
    box = silhouette_box(mattes, pad=args.box_pad)
    print(f"scoring box (top, left, bottom, right) = {box} of "
          f"{mattes[0].shape[1]}x{mattes[0].shape[0]}", flush=True)

    scorer = QualityScorer(net=args.lpips_net)
    rows = []
    with SequenceCoder(sequence, quality=args.quality) as coder:
        for frame_index in frames:
            started = time.time()
            camera, truth = references[frame_index]
            frame = sequence.frame(frame_index)
            grid = BlockGrid(frame.grid_shape, args.block_size)
            importance = ImportanceScorer(
                sequence, frame, ImportanceConfig(block_size=args.block_size)
            )
            # Scored at the camera being rendered: the filter gets a perfectly
            # predicted viewport, which is the optimistic end of the design.
            scores = importance.score(camera)
            price = coder.advance(frame)

            if frame_index == frames[0]:
                _save_png(renders / f"reference_f{frame_index:03d}.png", truth)

            for threshold in thresholds:
                keep = (scores >= threshold) & importance.occupancy
                cost = price(keep)
                with voxels_dropped(frame, grid, keep):
                    rendered = render_view(sequence, frame, camera)
                quality = scorer.score(rendered, truth, box)
                _save_png(
                    renders / f"t{threshold}_f{frame_index:03d}.png", rendered
                )
                if not args.keep_reference_once:
                    _save_png(renders / f"reference_f{frame_index:03d}.png", truth)
                row = {"threshold": threshold, **cost.as_dict(), **quality.as_dict()}
                rows.append(row)
                print(
                    f"  t={threshold:<5} frame {frame_index:3d} "
                    f"{cost.total_bytes / 1024:8.1f} kB  "
                    f"kept {cost.kept_blocks:5d}/{cost.occupied_blocks:5d}  "
                    f"PSNR {quality.psnr:5.2f}  SSIM {quality.ssim:.4f}  "
                    f"LPIPS {quality.lpips:.4f}",
                    flush=True,
                )
            print(f"frame {frame_index} done in {time.time() - started:.0f}s", flush=True)
            del frame, importance

    fields = list(rows[0].keys())
    with open(out_dir / "results.csv", "w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)

    per_threshold = []
    for threshold in thresholds:
        subset = [row for row in rows if row["threshold"] == threshold]
        per_threshold.append(
            {
                "threshold": threshold,
                "frames": len(subset),
                "bytes_per_frame": float(np.mean([r["total_bytes"] for r in subset])),
                "feature_bytes_per_frame": float(np.mean([r["feature_bytes"] for r in subset])),
                "mask_bytes_per_frame": float(np.mean([r["mask_bytes"] for r in subset])),
                "motion_bytes_per_frame": float(np.mean([r["motion_bytes"] for r in subset])),
                "kept_fraction": float(np.mean([r["kept_fraction"] for r in subset])),
                "psnr": float(np.mean([r["psnr"] for r in subset])),
                "ssim": float(np.mean([r["ssim"] for r in subset])),
                "lpips": float(np.mean([r["lpips"] for r in subset])),
                "mbps_at_30fps": float(
                    np.mean([r["total_bytes"] for r in subset]) * 8 * 30 / 1e6
                ),
            }
        )
        entry = per_threshold[-1]
        print(
            f"[t={entry['threshold']}] {entry['bytes_per_frame'] / 1024:8.1f} kB/frame "
            f"({entry['mbps_at_30fps']:6.1f} Mbps @30fps)  kept {entry['kept_fraction'] * 100:5.1f}%  "
            f"PSNR {entry['psnr']:5.2f}  SSIM {entry['ssim']:.4f}  LPIPS {entry['lpips']:.4f}",
            flush=True,
        )

    summary = {
        "object": sequence.cfg.expname,
        "config": str(sequence.config_path),
        "corpus": str(sequence.corpus_dir),
        "eval_view": eval_view,
        "held_out": held_out,
        "holdout_view": holdout,
        "training_views": training_views,
        "frames": frames,
        "block_size": args.block_size,
        "quality": args.quality,
        "render_size": [references[frames[0]][0].width, references[frames[0]][0].height],
        "scoring_box_tlbr": list(box),
        "lpips_net": args.lpips_net,
        "startup_bytes": startup_bytes(sequence),
        "per_threshold": per_threshold,
    }
    with open(out_dir / "summary.json", "w") as handle:
        json.dump(summary, handle, indent=1)
    print(f"wrote {out_dir}/results.csv and summary.json", flush=True)
    return summary


def parse_args(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--config", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--frames", type=int, default=0, help="0 = every trained frame")
    parser.add_argument("--thresholds", type=float, nargs="+", default=list(DEFAULT_THRESHOLDS))
    parser.add_argument("--block-size", type=int, default=8)
    parser.add_argument("--quality", type=int, default=99,
                        help="ReRF codec quality; 99 is compress.py's default")
    parser.add_argument("--box-pad", type=int, default=8)
    parser.add_argument("--lpips-net", default="alex", choices=("alex", "vgg"))
    parser.add_argument("--view", type=int, default=-1,
                        help="camera to render and score at; default is the corpus's "
                             "held-out camera, or the last one if none was held out")
    parser.add_argument("--keep-reference-once", action="store_true",
                        help="write the reference image only for the first frame")
    return parser.parse_args(argv)


def main(argv=None) -> int:
    args = parse_args(argv)
    rerf_env.activate()
    run(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
