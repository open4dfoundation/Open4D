#!/usr/bin/env python3
"""Resumable raw-mesh to N4MC archive pipeline."""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

from natsort import natsorted


ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "n4mc_source"
STAGES = ("prepare", "train", "decode", "evaluate", "package")
EXTENSIONS = {".obj", ".ply", ".stl"}


class PipelineError(RuntimeError):
    pass


def run(command: list[str], cwd: Path = SOURCE, dry_run: bool = False) -> None:
    import shlex
    print("+", shlex.join(command), flush=True)
    if not dry_run:
        subprocess.run(command, cwd=cwd, check=True)


def stage_range(start: str, end: str) -> tuple[str, ...]:
    first, last = STAGES.index(start), STAGES.index(end)
    if first > last:
        raise PipelineError("--from must not come after --to")
    return STAGES[first:last + 1]


def discover(directory: Path, limit: int) -> list[Path]:
    if not directory.is_dir():
        raise PipelineError(f"mesh directory not found: {directory}")
    files = [Path(p) for p in natsorted(
        [str(p) for p in directory.iterdir() if p.suffix.lower() in EXTENSIONS]
    )]
    if limit:
        files = files[:limit]
    if not files:
        raise PipelineError(f"no OBJ/PLY/STL meshes found in {directory}")
    return files


def write_config(path: Path, data_path: Path, run_dir: Path, frames: int,
                 voxel_res: int, epochs: int) -> None:
    size = voxel_res + 1
    if size < 16 or size % 16:
        raise PipelineError("voxel resolution + 1 must be divisible by 16 and at least 16")
    embed_hwd = size // 16
    values = {
        "model": "QuantGeneratorV2",
        "encoder_dim_list": "64_64_64_16",
        "encoder_stride_list": "2_2_2_2",
        "decoder_dim_list": "48_36_24_16_12",
        "decoder_stride_list": "2_2_2_2_1",
        "after_embed_dim": 0,
        "bias": True,
        "act": "gelu",
        "conv_type": "conv",
        "dataset": "SDF_dataset_npz",
        "data_path": str(data_path),
        "num_frames": frames,
        "pin_memory": True,
        "log_path": str(run_dir.parent),
        "run_dir": str(run_dir),
        "batch_size": 1,
        "n_epoch": epochs,
        "val_frequence": epochs,
        "voxel_grid_res": voxel_res,
        "lr": 0.001,
        "lr_type": "cosine",
        "lr_min": 0.00001,
        "device": "cuda",
        "warmup": 0.2,
        "important_weight": 5,
        "ssim_weight": 0,
        "offset_weight": 0,
        "num_bits": 8,
        "l1_reg": 0,
        "embed_dim": 16,
        "embed_hwd": embed_hwd,
        "embed_reg": 0,
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(f"{key}: {value}" for key, value in values.items()) + "\n",
                    encoding="utf-8")


def package_archive(archive: Path, config: Path, checkpoint: Path,
                    metadata: Path, report: Path, frame_count: int) -> None:
    required = [config, checkpoint / "decoder_compressed.pt", metadata]
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise PipelineError("cannot package; missing: " + ", ".join(missing))
    latents = sorted((checkpoint / "embed_features").glob("*.npy"))
    if len(latents) != frame_count:
        raise PipelineError(f"cannot package: expected {frame_count} latents, found {len(latents)}")
    archive.parent.mkdir(parents=True, exist_ok=True)
    manifest = {
        "format": "n4mc",
        "format_version": 1,
        "frame_count": frame_count,
        "decoder": "decoder_compressed.pt",
        "latents": "embed_features/embed_feature_*.npy",
        "config": "config.txt",
        "normalization": "normalization.json",
    }
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED,
                         compresslevel=9) as zf:
        zf.writestr("manifest.json", json.dumps(manifest, indent=2))
        zf.write(config, "config.txt")
        zf.write(checkpoint / "decoder_compressed.pt", "decoder_compressed.pt")
        zf.write(metadata, "normalization.json")
        if report.is_file():
            zf.write(report, "evaluation.json")
        for latent in latents:
            zf.write(latent, f"embed_features/{latent.name}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Run N4MC from raw meshes to a portable archive")
    parser.add_argument("--mesh-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--frames", type=int, default=0, help="0 means all frames")
    parser.add_argument("--voxel-res", type=int, default=127)
    parser.add_argument("--epochs", type=int, default=500)
    parser.add_argument("--samples", type=int, default=10000)
    parser.add_argument("--from", dest="start", choices=STAGES, default=STAGES[0])
    parser.add_argument("--to", dest="end", choices=STAGES, default=STAGES[-1])
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    output = args.output_dir.resolve()
    selected_dir = output / "source_meshes"
    data_dir = output / "tsdf"
    run_dir = output / "training"
    config = output / "config.txt"
    checkpoint = run_dir / f"checkpoint_{args.epochs:04d}"
    decoded = output / "decoded"
    report = output / "evaluation.json"
    archive = (args.archive or output / "sequence.n4mc").resolve()
    stages = stage_range(args.start, args.end)
    files = discover(args.mesh_dir.resolve(), args.frames)

    if not args.dry_run:
        output.mkdir(parents=True, exist_ok=True)
        write_config(config, data_dir, run_dir, len(files), args.voxel_res, args.epochs)

    try:
        if "prepare" in stages:
            marker = data_dir / ".complete"
            if args.force or not marker.is_file():
                if not args.dry_run:
                    if selected_dir.exists():
                        shutil.rmtree(selected_dir)
                    selected_dir.mkdir(parents=True)
                    for source in files:
                        shutil.copy2(source, selected_dir / source.name)
                run([sys.executable, str(SOURCE / "gen_tsdf_from_meshes.py"),
                     "--mesh_dir", str(selected_dir), "--save_path", str(data_dir),
                     "--voxel_grid_res", str(args.voxel_res)], dry_run=args.dry_run)
                if not args.dry_run:
                    marker.touch()
            else:
                print("[prepare] already complete")

        if "train" in stages:
            if args.force or not (checkpoint / "decoder_compressed.pt").is_file():
                run([sys.executable, str(SOURCE / "train_quant.py"),
                     f"--config={config}"], dry_run=args.dry_run)
            else:
                print("[train] already complete")

        if "decode" in stages:
            marker = decoded / ".complete"
            if args.force or not marker.is_file():
                run([sys.executable, str(SOURCE / "decode.py"),
                     "--config", str(config), "--checkpoint", str(checkpoint),
                     "--metadata", str(data_dir / "normalization.json"),
                     "--output", str(decoded)], dry_run=args.dry_run)
                if not args.dry_run:
                    marker.touch()
            else:
                print("[decode] already complete")

        if "evaluate" in stages:
            if args.force or not report.is_file():
                run([sys.executable, str(SOURCE / "evaluate_reconstruction.py"),
                     "--original", str(selected_dir), "--decoded", str(decoded),
                     "--output", str(report), "--samples", str(args.samples)],
                    dry_run=args.dry_run)
            else:
                print("[evaluate] already complete")

        if "package" in stages:
            print(f"[package] {archive}")
            if not args.dry_run:
                package_archive(archive, config, checkpoint,
                                data_dir / "normalization.json", report, len(files))
        print("N4MC pipeline complete." if not args.dry_run else "Dry run complete.")
        return 0
    except subprocess.CalledProcessError as exc:
        print(f"pipeline failed with exit code {exc.returncode}", file=sys.stderr)
        return exc.returncode
    except PipelineError as exc:
        print(f"pipeline error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
