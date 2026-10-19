"""Recompute quality and cold-decode saved training checkpoints without retraining.

Usage: python -m scripts.codec_rd.evaluate /path/to/experiment --samples 4096
"""

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np

from open4d.codecs import decode_sequence
from open4d.codecs._npz import NumPyZipCodec
from open4d.core import TriangleMesh
from .__main__ import digest, run_worker
from .metrics import mesh_errors, summarize_frames, bounding_box_diagonal


def quality(frames, path, peak, samples, seed):
    metrics = []
    with decode_sequence(path, codec=NumPyZipCodec("raw")) as decoded:
        if len(decoded) != len(frames):
            raise ValueError("decoded frame count differs from the reference")
        for reference, actual in zip(frames, decoded, strict=True):
            if (reference.frame_index, reference.timestamp) != (actual.frame_index, actual.timestamp):
                raise ValueError("decoded timeline differs from the reference")
            metrics.append({"frame_index": reference.frame_index, "timestamp": reference.timestamp,
                **mesh_errors(reference.geometry, actual.geometry, peak=peak, samples=samples,
                              seed=seed, exact_hausdorff=False)})
    return metrics


def evaluate(directory, *, samples=4096, checkpoints=True):
    directory = Path(directory).resolve()
    result = directory / "results.json"
    report = json.loads(result.read_text())
    input_path = directory / "input.o4d"
    source_hash = digest(input_path)
    with decode_sequence(input_path, codec=NumPyZipCodec("raw")) as source:
        frames = tuple(source)
    peak = bounding_box_diagonal(*(frame.geometry.positions for frame in frames))
    for row in report["results"]:
        if row["source_sha256"] != source_hash:
            raise ValueError("reference input hash differs from the encoded experiment")
        path = directory / row["id"]
        artifact = path / "sequence.o4d"
        if digest(artifact) != row["artifact_sha256"]:
            raise ValueError("encoded artifact changed since the experiment")
        seed = row["options"].get("seed", 0)
        metrics = quality(frames, path / "decoded.o4d", peak, samples, seed)
        row.update(frames=metrics, quality=summarize_frames(metrics))
        stages = []
        for stage in row["encode"]["stages"]:
            with np.load(stage["path"], allow_pickle=False) as arrays:
                mesh = TriangleMesh(arrays["positions"], arrays["triangles"])
            stages.append({"frame": stage["frame"], "stage": stage["stage"],
                **mesh_errors(frames[stage["frame"]].geometry, mesh, peak=peak,
                              samples=samples, seed=seed, exact_hausdorff=False)})
        row["stage_quality"] = stages
        if checkpoints:
            extra = []
            for epoch, name in row["options"].get("encode", {}).get("checkpoint_artifacts", {}).items():
                checkpoint = path / name
                target = path / ("checkpoint-%06d" % int(epoch))
                target.mkdir(exist_ok=True)
                decoded_path = target / "decoded.o4d"
                request = {"operation": "decode", "codec": row["codec"], "seed": seed,
                    "artifact": str(checkpoint), "decoded": str(decoded_path),
                    "options": row["options"].get("decode", {}), "report": str(target / "decode.json"),
                    "deny_paths": [str(input_path), report["source"], str(path / "stages")]}
                decoded = run_worker(request, target, 3600)
                values = quality(frames, decoded_path, peak, samples, seed)
                size = checkpoint.stat().st_size
                extra.append({"epoch": int(epoch), "artifact_bytes": size,
                    "artifact_sha256": digest(checkpoint), "decode": decoded,
                    "bitrate_bits_per_second": 8 * size / row["rate_duration_seconds"],
                    "quality": summarize_frames(values), "frames": values})
            row["checkpoints"] = extra
        row["quality_evaluation"] = {"samples_per_surface": samples,
            "metrics_source_sha256": hashlib.sha256(Path(__file__).with_name("metrics.py").read_bytes()).hexdigest(),
            "surface_metrics": "exact distances from area samples to triangles; separate from point-set D1/D2"}
        (path / "result.json").write_text(json.dumps(row, indent=2, allow_nan=False) + "\n")
        result.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
        print(path, row["quality"].get("surface_d1_psnr_db_of_mean_mse"), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directories", nargs="+", type=Path)
    parser.add_argument("--samples", type=int, default=4096)
    parser.add_argument("--skip-checkpoints", action="store_true")
    args = parser.parse_args()
    if args.samples < 1:
        parser.error("samples must be positive")
    for path in args.directories:
        evaluate(path, samples=args.samples, checkpoints=not args.skip_checkpoints)


if __name__ == "__main__":
    main()
