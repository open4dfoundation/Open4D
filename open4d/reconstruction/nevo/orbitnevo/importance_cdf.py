"""Steps 1-2 end to end: load a ReRF sequence, score its voxels, plot the CDF.

    python -m orbitnevo.importance_cdf \\
        --config baselines/NeVo/rerf/configs/nevo/basketball.py \\
        --viewports 300 --out ~/nevo_results/basketball

Writes ``importance_cdf.json`` (summaries plus the plottable curves),
``importance_cdf.png``, and per-frame ``scores_<frame>.npy`` so later stages do
not have to re-march. ``--verify`` additionally checks that this module's
transcription of ReRF's ray marching returns the same weights the vendored
model does.

Must run in the ``nevo`` conda environment; see baselines/NeVo/README.md.
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

from nevo import cdf as cdf_module  # noqa: E402
from nevo import rerf_env, viewports  # noqa: E402

# Importing lib.dvgo has import-time side effects (a JIT CUDA build, a chdir
# requirement, torch's default tensor type flipped to cuda), so it happens
# through rerf_env and only once the arguments have been parsed.


def _corpus_manifest(sequence) -> dict:
    path = sequence.corpus_dir / "nevo_corpus.json"
    if not path.is_file():
        raise FileNotFoundError(f"{path} not found; was this corpus made by orbitnevo.prepare?")
    with open(path) as handle:
        return json.load(handle)


def run(args) -> dict:
    from nevo.importance import (
        ImportanceConfig,
        ImportanceScorer,
        check_against_rerf,
    )
    from nevo.render import check_reload
    from nevo.sequence import ReRFSequence

    sequence = ReRFSequence(args.config)
    manifest = _corpus_manifest(sequence)
    trained = sequence.available_frames()
    if not trained:
        raise RuntimeError(f"no trained frames under {sequence.run_dir}")
    frames = trained[: args.frames] if args.frames > 0 else trained
    print(f"{sequence.cfg.expname}: {len(trained)} trained frames, scoring {len(frames)}",
          flush=True)

    out_dir = Path(args.out).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    reference = manifest["cameras"][0]
    focal = float(reference["fx"])
    width, height = int(manifest["width"]), int(manifest["height"])
    radius = float(np.linalg.norm(np.asarray(reference["c2w_normalised"])[:3, 3]))

    spread = viewports.ViewportSpread(
        radius_scale=tuple(args.radius_scale),
        elevation_degrees=tuple(args.elevation),
        aim_jitter=args.aim_jitter,
    )
    cameras = viewports.sample_viewports(
        args.viewports,
        manifest["xyz_min"],
        manifest["xyz_max"],
        reference_radius=radius,
        width=width,
        height=height,
        focal=focal,
        spread=spread,
        seed=args.seed,
    )
    cameras = viewports.downscale(cameras, args.render_factor)
    print(f"{len(cameras)} viewports at {cameras[0].width}x{cameras[0].height}, "
          f"rig radius {radius:.3f}", flush=True)

    config = ImportanceConfig(
        block_size=args.block_size,
        assignment=args.assignment,
        render_factor=1,  # already applied to the cameras
        ray_chunk=args.ray_chunk,
    )

    # Pre-pass for the occupancy union. Which blocks are non-empty shifts as the
    # subject moves, and pooling a CDF over frames needs one fixed column set;
    # taking the union means a block occupied in any frame is scored in all of
    # them, rather than silently changing the denominator per frame.
    from nevo.blocks import BlockGrid

    occupancy = None
    grid = None
    for frame_index in frames:
        density = sequence.frame_density(frame_index)
        if grid is None:
            grid = BlockGrid.from_volume(density, args.block_size)
        frame_occupancy = grid.occupancy(density).cpu().numpy()
        occupancy = frame_occupancy if occupancy is None else np.logical_or(occupancy, frame_occupancy)
        del density
    occupied_index = np.flatnonzero(occupancy)
    print(
        f"occupancy union: {occupied_index.size}/{grid.num_blocks} blocks "
        f"({occupied_index.size / grid.num_blocks * 100:.1f}%), "
        f"grid {grid.grid_shape} in {grid.blocks_shape} blocks of {args.block_size}^3",
        flush=True,
    )
    if occupied_index.size == 0:
        raise RuntimeError("no occupied blocks; is this sequence trained?")

    per_viewport = cdf_module.ImportanceAccumulator(args.bins)
    per_frame = cdf_module.ImportanceAccumulator(args.bins)
    verification = {}
    reload_checks = {}
    per_frame_notes = []
    store_bytes = 0

    for frame_index in frames:
        started = time.time()
        frame = sequence.frame(frame_index)
        scorer = ImportanceScorer(sequence, frame, config)
        if scorer.num_blocks != grid.num_blocks:
            raise RuntimeError("grid resolution changes across frames; scores cannot be pooled")
        # Verify once on an I-frame and once on a P-frame: their models are
        # assembled differently (a P-frame's feature grid is a residual over a
        # motion-compensated predecessor), so one check does not cover both.
        kind = "I" if frame.is_key_frame else "P"
        if args.verify and kind not in verification:
            result = check_against_rerf(sequence, frame, cameras[0])
            print(f"marching check vs. ReRF ({kind}-frame {frame.index}): {result}", flush=True)
            if not result["agrees"]:
                raise RuntimeError("instrumented marching disagrees with ReRF's forward pass")
            verification[kind] = result
        if args.verify and kind not in reload_checks:
            reload = check_reload(
                sequence, frame, save_to=out_dir / f"reload_{kind.lower()}frame.png"
            )
            print(f"reload check ({kind}-frame {frame.index}): "
                  f"{reload['psnr']:.2f} dB against the training view", flush=True)
            if reload["psnr"] < args.min_reload_psnr:
                raise RuntimeError(
                    f"frame {frame.index} reloaded to {reload['psnr']:.2f} dB, below "
                    f"{args.min_reload_psnr} -- the checkpoint was not reassembled correctly"
                )
            reload_checks[kind] = reload

        dense = scorer.score_many(cameras).numpy()
        # How much visible weight sits in blocks ReRF's codec never sends. Its
        # occupancy test (raw density > ~3.39) is far stricter than the
        # renderer's alpha prune, so low-density haze renders but is not in the
        # bitstream. That is upstream's behaviour, but it caps what *any*
        # block-level filtering can preserve, so it is worth a number.
        outside = float(dense[:, ~occupancy].sum())
        inside = float(dense[:, occupancy].sum())
        scores = dense[:, occupied_index]
        del dense
        per_viewport.add(scores)
        per_frame.add(scores.max(axis=0))
        if store_bytes + scores.nbytes <= args.max_store_bytes:
            np.save(out_dir / f"scores_{frame_index}.npy", scores.astype(np.float32))
            store_bytes += scores.nbytes

        note = {
            "frame": frame_index,
            "key_frame": frame.is_key_frame,
            "grid_shape": list(frame.grid_shape),
            "raw_bytes": frame.raw_bytes(),
            "occupied_blocks_this_frame": scorer.occupied_blocks,
            "weight_outside_codec_mask": outside / (inside + outside) if inside + outside else 0.0,
            "has_motion_vectors": frame.motion is not None,
            "seconds": time.time() - started,
        }
        per_frame_notes.append(note)
        print(
            f"frame {frame_index:3d} "
            f"{'I' if frame.is_key_frame else 'P'} "
            f"grid {tuple(frame.grid_shape)} "
            f"occupied {scorer.occupied_blocks} "
            f"below {cdf_module.PAPER_THRESHOLD}: "
            f"{float((scores < cdf_module.PAPER_THRESHOLD).mean()) * 100:.1f}% "
            f"{note['seconds']:.1f}s",
            flush=True,
        )
        del scores, scorer, frame

    shared = dict(
        block_size=args.block_size,
        assignment=args.assignment,
        frames=len(frames),
        viewports=len(cameras),
        occupied_blocks=int(occupied_index.size),
        total_blocks=int(grid.num_blocks),
        extra={"object": sequence.cfg.expname},
    )
    summaries = [
        per_viewport.summary(pooling="per-viewport", **shared),
        per_frame.summary(pooling="per-frame", **shared),
    ]
    curves = {
        f"{sequence.cfg.expname} (per-viewport)": per_viewport.curve(),
        f"{sequence.cfg.expname} (per-frame)": per_frame.curve(),
    }

    report = {
        "object": sequence.cfg.expname,
        "config": str(sequence.config_path),
        "run_dir": str(sequence.run_dir),
        "viewports": len(cameras),
        "viewport_size": [cameras[0].width, cameras[0].height],
        "render_factor": args.render_factor,
        "block_size": args.block_size,
        "assignment": args.assignment,
        "seed": args.seed,
        "viewport_spread": {
            "radius_scale": list(args.radius_scale),
            "elevation_degrees": list(args.elevation),
            "aim_jitter": args.aim_jitter,
        },
        "verification": verification,
        "reload_checks": reload_checks,
        "frames": per_frame_notes,
        "summaries": summaries,
    }
    name = args.tag or f"block{args.block_size}_{args.assignment}"
    with open(out_dir / f"importance_cdf_{name}.json", "w") as handle:
        json.dump({"report": report, "curves": curves}, handle, indent=1)
    if not args.no_plot:
        cdf_module.plot(
            curves,
            out_dir / f"importance_cdf_{name}.png",
            f"{sequence.cfg.expname}: voxel importance "
            f"({len(frames)} frames x {len(cameras)} viewports, block {args.block_size})",
        )

    for summary in summaries:
        check = summary["paper_check"]
        print(
            f"[{summary['pooling']}] below {check['threshold']}: "
            f"{check['observed_fraction_below'] * 100:.1f}% "
            f"(paper ~{check['paper_fraction_below'] * 100:.0f}%), "
            f"median {summary['quantiles']['0.5']:.4f}, "
            f"never hit {summary['never_hit_fraction'] * 100:.1f}%, "
            f"{summary['scored_samples']} samples",
            flush=True,
        )
    print(f"wrote {out_dir}/importance_cdf_{name}.json", flush=True)
    return report


def parse_args(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--config", required=True, help="the ReRF config used for training")
    parser.add_argument("--out", required=True)
    parser.add_argument("--frames", type=int, default=0, help="0 = every trained frame")
    parser.add_argument("--viewports", type=int, default=300)
    parser.add_argument("--render-factor", type=int, default=1,
                        help="integer downscale of each viewport before marching")
    parser.add_argument("--block-size", type=int, default=8,
                        help="8 = ReRF's codec block; 1 = single grid entries")
    parser.add_argument("--assignment", default="nearest", choices=("nearest", "trilinear"))
    parser.add_argument("--ray-chunk", type=int, default=1 << 18)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--radius-scale", type=float, nargs=2, default=(0.75, 1.45),
                        metavar=("MIN", "MAX"),
                        help="viewer distance, as a multiple of the capture rig radius")
    parser.add_argument("--elevation", type=float, nargs=2, default=(-25.0, 55.0),
                        metavar=("MIN", "MAX"), help="viewer elevation band, degrees")
    parser.add_argument("--aim-jitter", type=float, default=0.25,
                        help="how far the look-at point wanders, as a fraction of the bbox")
    parser.add_argument("--verify", action="store_true",
                        help="check the marching against ReRF's forward pass, and check that "
                             "each frame reloads to a sane render of its training view")
    parser.add_argument("--min-reload-psnr", type=float, default=25.0,
                        help="fail if a reloaded frame renders worse than this")
    parser.add_argument("--no-plot", action="store_true")
    parser.add_argument("--bins", type=int, default=cdf_module.DEFAULT_BINS)
    parser.add_argument("--tag", default="", help="suffix for the output files")
    parser.add_argument("--max-store-bytes", type=int, default=2 << 30,
                        help="stop writing per-frame scores_*.npy past this total")
    return parser.parse_args(argv)


def main(argv=None) -> int:
    args = parse_args(argv)
    rerf_env.activate()
    run(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
