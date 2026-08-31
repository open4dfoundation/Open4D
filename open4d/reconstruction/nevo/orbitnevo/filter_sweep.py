"""What does filtering by neural visibility actually cost in pixels?

The CDF answers half of the paper's section 3.2 claim -- that most voxels score
below 0.025. The other half is the part that makes it a *saving* rather than a
loss: "The SSIM consistently exceeds 0.98 (i.e., visually lossless), indicating
that removing these voxels does not impact visual quality."

This sweeps thresholds and measures both ends together: how many non-empty
blocks each threshold drops, and the SSIM and PSNR of the resulting render
against the same viewport rendered from the whole grid. Filtering is done
per-viewport with that viewport's own scores, which is the decision the edge
would make for a *correctly predicted* viewport -- an upper bound on the real
system, where the fetch is chosen from a prediction several frames early.

    python -m orbitnevo.filter_sweep \\
        --config baselines/NeVo/rerf/configs/nevo/g_basketball.py \\
        --out ~/nevo_results/g_basketball

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

from nevo import rerf_env, viewports  # noqa: E402

VISUALLY_LOSSLESS_SSIM = 0.98
"""The bar the paper cites (Cuervo et al., Kahawai, MobiSys'15)."""


def run(args) -> dict:
    from nevo.blocks import BlockGrid
    from nevo.filtering import preview
    from nevo.importance import ImportanceConfig, ImportanceScorer
    from nevo.sequence import ReRFSequence

    sequence = ReRFSequence(args.config)
    with open(sequence.corpus_dir / "nevo_corpus.json") as handle:
        manifest = json.load(handle)
    trained = sequence.available_frames()
    if not trained:
        raise RuntimeError(f"no trained frames under {sequence.run_dir}")
    frames = trained[: args.frames] if args.frames > 0 else trained

    reference = manifest["cameras"][0]
    radius = float(np.linalg.norm(np.asarray(reference["c2w_normalised"])[:3, 3]))
    cameras = viewports.sample_viewports(
        args.viewports,
        manifest["xyz_min"],
        manifest["xyz_max"],
        reference_radius=radius,
        width=int(manifest["width"]),
        height=int(manifest["height"]),
        focal=float(reference["fx"]),
        # A different seed from importance_cdf.py's default, so the thresholds
        # are scored on viewports the CDF did not also report.
        seed=args.seed,
    )
    cameras = viewports.downscale(cameras, args.render_factor)
    print(f"{sequence.cfg.expname}: {len(frames)} frames x {len(cameras)} viewports at "
          f"{cameras[0].width}x{cameras[0].height}, block {args.block_size}^3", flush=True)

    config = ImportanceConfig(block_size=args.block_size, assignment=args.assignment)
    samples = {threshold: [] for threshold in args.thresholds}
    for frame_index in frames:
        started = time.time()
        frame = sequence.frame(frame_index)
        scorer = ImportanceScorer(sequence, frame, config)
        grid = BlockGrid(frame.grid_shape, args.block_size)
        for camera in cameras:
            scores = scorer.score(camera)
            for threshold in args.thresholds:
                result = preview(
                    sequence, frame, camera, scores, grid, threshold, scorer.occupancy
                )
                samples[threshold].append(
                    (result.dropped_fraction, result.ssim, result.psnr)
                )
        print(f"frame {frame_index:3d} {'I' if frame.is_key_frame else 'P'} "
              f"{time.time() - started:.0f}s", flush=True)
        del frame, scorer

    rows = []
    for threshold in args.thresholds:
        values = np.asarray(samples[threshold])
        rows.append(
            {
                "threshold": threshold,
                "dropped_mean": float(values[:, 0].mean()),
                "ssim_mean": float(values[:, 1].mean()),
                "ssim_min": float(values[:, 1].min()),
                "psnr_mean": float(values[:, 2].mean()),
                "psnr_min": float(values[:, 2].min()),
                "visually_lossless": bool(values[:, 1].min() >= VISUALLY_LOSSLESS_SSIM),
            }
        )
        row = rows[-1]
        print(
            f"threshold {threshold:<7} dropped {row['dropped_mean'] * 100:5.1f}%  "
            f"SSIM {row['ssim_mean']:.4f} (worst {row['ssim_min']:.4f})  "
            f"PSNR {row['psnr_mean']:5.1f} dB (worst {row['psnr_min']:5.1f})  "
            f"{'lossless' if row['visually_lossless'] else 'DEGRADED'}",
            flush=True,
        )

    best = max(
        (row for row in rows if row["visually_lossless"]),
        key=lambda row: row["dropped_mean"],
        default=None,
    )
    if best is not None:
        print(
            f"\nlargest visually-lossless threshold: {best['threshold']}, "
            f"dropping {best['dropped_mean'] * 100:.1f}% of non-empty blocks",
            flush=True,
        )
    else:
        print("\nno threshold stayed above SSIM 0.98", flush=True)

    report = {
        "object": sequence.cfg.expname,
        "config": str(sequence.config_path),
        "frames": frames,
        "viewports": len(cameras),
        "viewport_size": [cameras[0].width, cameras[0].height],
        "block_size": args.block_size,
        "assignment": args.assignment,
        "seed": args.seed,
        "ssim_bar": VISUALLY_LOSSLESS_SSIM,
        "rows": rows,
        "best_lossless": best,
    }
    out_dir = Path(args.out).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / f"filter_sweep_block{args.block_size}.json"
    with open(path, "w") as handle:
        json.dump(report, handle, indent=1)
    print(f"wrote {path}", flush=True)
    return report


def parse_args(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--config", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--frames", type=int, default=0, help="0 = every trained frame")
    parser.add_argument("--viewports", type=int, default=8)
    parser.add_argument("--render-factor", type=int, default=1)
    parser.add_argument("--block-size", type=int, default=8)
    parser.add_argument("--assignment", default="nearest", choices=("nearest", "trilinear"))
    parser.add_argument("--thresholds", type=float, nargs="+",
                        default=[0.01, 0.025, 0.05, 0.1, 0.2, 0.35, 0.5, 0.7],
                        help="ranges well past the paper's 0.025 on purpose: the paper "
                             "*fits* its threshold to an SSIM target rather than fixing "
                             "it, so what matters is where quality actually breaks")
    parser.add_argument("--seed", type=int, default=101)
    return parser.parse_args(argv)


def main(argv=None) -> int:
    args = parse_args(argv)
    rerf_env.activate()
    run(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
