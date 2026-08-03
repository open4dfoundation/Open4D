"""Preprocess and train QNDF on every basketball-player frame.

The run is resumable: a frame with both decoded meshes is skipped, and status is
written after every transition.  Separate workers may target separate physical
GPUs through CUDA_VISIBLE_DEVICES while QNDF continues to use cuda:0 internally.
"""

from __future__ import annotations

import argparse
import json
import os
import re
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timezone
from pathlib import Path
import shutil
import subprocess
import threading

import numpy as np
import open3d as o3d

from build_dataset_open3d import build


def now() -> str:
    return datetime.now(timezone.utc).isoformat()


def atomic_json(path: Path, value: dict) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n")
    temporary.replace(path)


def restore_mesh(normalized_path: Path, output_path: Path, transform_path: Path) -> None:
    transform = json.loads(transform_path.read_text())
    mesh = o3d.io.read_triangle_mesh(str(normalized_path), enable_post_processing=False)
    vertices = np.asarray(mesh.vertices) * float(transform["scale"]) + np.asarray(transform["bbox_min"])
    mesh.vertices = o3d.utility.Vector3dVector(vertices)
    if not o3d.io.write_triangle_mesh(str(output_path), mesh, write_vertex_normals=False):
        raise OSError(f"failed to write {output_path}")


def read_metrics(log_path: Path) -> dict[str, float]:
    text = log_path.read_text(errors="replace")
    labels = {
        "rec_to_target_error": r"Rec to Tar Compression Error obtained:\s*([0-9.eE+-]+)",
        "target_to_rec_error": r"Tar to Rec Compression Error obtained:\s*([0-9.eE+-]+)",
        "total_error": r"Total Compression Error obtained:\s*([0-9.eE+-]+)",
        "normal_error": r"Normal Error obtained:\s*([0-9.eE+-]+)",
        "representation_kib": r"Total Size of Compressed Representation is\s*([0-9.eE+-]+)KB",
    }
    metrics: dict[str, float] = {}
    for key, pattern in labels.items():
        matches = re.findall(pattern, text)
        if matches:
            metrics[key] = float(matches[-1])
    return metrics


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--pattern", default="basketball_player_fr*.obj")
    parser.add_argument("--gpus", default="0,1")
    parser.add_argument("--epochs", type=int, default=300)
    parser.add_argument("--coarse-size", type=int, default=5000)
    parser.add_argument("--num-subdiv", type=int, default=3)
    parser.add_argument("--hidden-dim", type=int, default=28)
    parser.add_argument("--num-layers", type=int, default=17)
    parser.add_argument("--output-dir", type=Path, default=Path("outputs/basketball_sequence_qndf"))
    parser.add_argument("--preprocess-only", action="store_true")
    args = parser.parse_args()

    root = Path(__file__).resolve().parent
    output_root = (root / args.output_dir).resolve() if not args.output_dir.is_absolute() else args.output_dir
    output_root.mkdir(parents=True, exist_ok=True)
    originals = root / "objs_original"
    originals.mkdir(exist_ok=True)
    sources = sorted(args.source_dir.resolve().glob(args.pattern))
    if not sources:
        raise SystemExit(f"no meshes matched {args.source_dir / args.pattern}")

    status_path = output_root / "status.json"
    lock = threading.Lock()
    status = {
        "state": "preprocessing",
        "started_at": now(),
        "updated_at": now(),
        "settings": vars(args) | {"source_dir": str(args.source_dir), "output_dir": str(output_root)},
        "frames": {},
    }
    for source in sources:
        status["frames"][source.stem] = {"state": "pending", "source": str(source)}
    atomic_json(status_path, status)

    for source in sources:
        name = source.stem
        destination = originals / source.name
        if not destination.exists() or destination.stat().st_size != source.stat().st_size:
            shutil.copy2(source, destination)
        try:
            metadata = build(name, args.coarse_size, args.num_subdiv, root)
            status["frames"][name].update(
                state="preprocessed",
                components=metadata["component_count"],
                training_vertices=metadata["training_vertices"],
                training_faces=metadata["training_faces"],
            )
        except Exception as error:
            status["frames"][name].update(state="failed", error=f"preprocess: {error}")
            status["state"] = "failed"
            status["updated_at"] = now()
            atomic_json(status_path, status)
            raise
        status["updated_at"] = now()
        atomic_json(status_path, status)

    if args.preprocess_only:
        status["state"] = "preprocessed"
        status["updated_at"] = now()
        atomic_json(status_path, status)
        return

    gpus = [gpu.strip() for gpu in args.gpus.split(",") if gpu.strip()]
    if not gpus:
        raise SystemExit("at least one GPU is required")
    status["state"] = "training"
    atomic_json(status_path, status)

    def train(source: Path, gpu: str) -> None:
        name = source.stem
        frame_dir = output_root / name
        frame_dir.mkdir(exist_ok=True)
        normalized = frame_dir / "reconstruction_normalized.obj"
        restored = frame_dir / "reconstruction_original_scale.obj"
        transform = root / "experiments" / name / f"transform_f{args.coarse_size}_s{args.num_subdiv}.json"
        if normalized.exists() and restored.exists():
            metrics = read_metrics(frame_dir / "train.log")
            if metrics:
                atomic_json(frame_dir / "metrics.json", metrics)
            with lock:
                status["frames"][name].update(
                    state="complete",
                    gpu=gpu,
                    resumed=True,
                    outputs={
                        "normalized": str(normalized.relative_to(output_root)),
                        "original_scale": str(restored.relative_to(output_root)),
                        "transform": str((frame_dir / "transform.json").relative_to(output_root)),
                        "metrics": str((frame_dir / "metrics.json").relative_to(output_root)),
                    },
                    metrics=metrics,
                )
                status["updated_at"] = now()
                atomic_json(status_path, status)
            return
        with lock:
            status["frames"][name].update(state="training", gpu=gpu, started_at=now())
            status["updated_at"] = now()
            atomic_json(status_path, status)
        command = [
            str(Path(os.environ.get("CONDA_PREFIX", "")) / "bin" / "python"),
            str(root / "compress.py"), name,
            "-ns", str(args.num_subdiv), "-cs", str(args.coarse_size),
            "-hd", str(args.hidden_dim), "-nl", str(args.num_layers),
            "-ne", str(args.epochs), "-rs", "basketball_sequence",
            "--output-dir", str(frame_dir), "--keep-artifacts",
        ]
        if not Path(command[0]).exists():
            command[0] = os.sys.executable
        environment = os.environ.copy()
        environment["CUDA_VISIBLE_DEVICES"] = gpu
        environment["MLFLOW_ALLOW_FILE_STORE"] = "true"
        environment["MLFLOW_TRACKING_URI"] = (output_root / "mlruns").as_uri()
        with (frame_dir / "train.log").open("w") as log:
            result = subprocess.run(command, cwd=root, env=environment, stdout=log, stderr=subprocess.STDOUT)
        if result.returncode:
            raise RuntimeError(f"training exited {result.returncode}; see {frame_dir / 'train.log'}")
        restore_mesh(normalized, restored, transform)
        shutil.copy2(transform, frame_dir / "transform.json")
        metrics = read_metrics(frame_dir / "train.log")
        atomic_json(frame_dir / "metrics.json", metrics)
        with lock:
            status["frames"][name].update(
                state="complete",
                gpu=gpu,
                finished_at=now(),
                outputs={
                    "normalized": str(normalized.relative_to(output_root)),
                    "original_scale": str(restored.relative_to(output_root)),
                    "transform": str((frame_dir / "transform.json").relative_to(output_root)),
                    "metrics": str((frame_dir / "metrics.json").relative_to(output_root)),
                },
                metrics=metrics,
            )
            status["updated_at"] = now()
            atomic_json(status_path, status)

    failures = []

    def train_gpu_batch(gpu: str, batch: list[Path]) -> list[tuple[str, str]]:
        batch_failures: list[tuple[str, str]] = []
        for source in batch:
            try:
                train(source, gpu)
            except Exception as error:
                batch_failures.append((source.stem, str(error)))
                with lock:
                    status["frames"][source.stem].update(
                        state="failed", error=str(error), finished_at=now()
                    )
                    status["updated_at"] = now()
                    atomic_json(status_path, status)
        return batch_failures

    batches = [sources[index::len(gpus)] for index in range(len(gpus))]
    with ThreadPoolExecutor(max_workers=len(gpus)) as executor:
        futures = {
            executor.submit(train_gpu_batch, gpu, batch): gpu
            for gpu, batch in zip(gpus, batches)
            if batch
        }
        for future in as_completed(futures):
            failures.extend(future.result())

    status["state"] = "failed" if failures else "complete"
    status["finished_at"] = now()
    status["updated_at"] = now()
    atomic_json(status_path, status)
    if failures:
        raise SystemExit("failed frames: " + ", ".join(name for name, _ in failures))


if __name__ == "__main__":
    main()
