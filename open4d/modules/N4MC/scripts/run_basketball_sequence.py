"""Train N4MC on basketball and reconstruct the complete ten-frame sequence."""

from __future__ import annotations

from datetime import datetime, timezone
import json
from pathlib import Path
import shutil
import subprocess
import sys

import numpy as np


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "outputs" / "basketball_sequence_n4mc"
STATUS = OUTPUT / "status.json"


def now() -> str:
    return datetime.now(timezone.utc).isoformat()


def write_json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n")
    temporary.replace(path)


def run(command: list[str]) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def main() -> None:
    OUTPUT.mkdir(parents=True, exist_ok=True)
    status = {
        "state": "training",
        "started_at": now(),
        "updated_at": now(),
        "train_config": "configs/train_basketball.yaml",
        "eval_config": "configs/eval_basketball.yaml",
        "frames": 10,
    }
    write_json(STATUS, status)
    before = set((ROOT / "outputs").glob("basketball_full_default_300_*"))

    try:
        run([sys.executable, "-m", "training.train", "--config", "configs/train_basketball.yaml"])
        candidates = set((ROOT / "outputs").glob("basketball_full_default_300_*")) - before
        run_dir = max(candidates, key=lambda path: path.stat().st_mtime)
        checkpoint = run_dir / "best.pt"
        if not checkpoint.is_file():
            raise FileNotFoundError(checkpoint)

        status.update(state="reconstructing", updated_at=now(), training_run=str(run_dir.relative_to(ROOT)))
        write_json(STATUS, status)
        normalized = OUTPUT / "normalized"
        if normalized.exists():
            shutil.rmtree(normalized)
        run(
            [
                sys.executable,
                "-m",
                "evaluation.reconstruct",
                "--config",
                "configs/eval_basketball.yaml",
                "--checkpoint",
                str(checkpoint),
                "--split",
                "test",
                "--output-dir",
                str(normalized),
            ]
        )

        status.update(state="restoring", updated_at=now(), checkpoint=str(checkpoint.relative_to(ROOT)))
        write_json(STATUS, status)
        original_scale = OUTPUT / "original_scale"
        if original_scale.exists():
            shutil.rmtree(original_scale)
        run(
            [
                sys.executable,
                "-m",
                "evaluation.restore_sequence",
                "--input-dir",
                str(normalized),
                "--normalization",
                "datasets/basketball_normalized/normalization.npz",
                "--output-dir",
                str(original_scale),
            ]
        )

        metrics = json.loads((normalized / "summary.json").read_text())
        source_frames = np.load(
            ROOT / "datasets/basketball_normalized/normalization.npz"
        )["source_frames"]
        frames = []
        for index, source_name in enumerate(source_frames):
            frame_id = f"{index:04d}"
            output_name = f"{Path(str(source_name)).stem}_reconstructed.ply"
            frame_metrics = metrics[frame_id]
            frames.append(
                {
                    "frame_id": frame_id,
                    "source": str(source_name),
                    "normalized_mesh": f"normalized/{frame_id}.ply",
                    "original_scale_mesh": f"original_scale/{output_name}",
                    "latent_pack": f"normalized/{frame_id}_latent_pack.npz",
                    "metrics": frame_metrics,
                }
            )
        aggregate = {
            "frames": len(frames),
            "mean_chamfer_distance": float(
                np.mean([frame["metrics"]["chamfer_distance"] for frame in frames])
            ),
            "mean_normal_consistency": float(
                np.mean([frame["metrics"]["normal_consistency"] for frame in frames])
            ),
            "mean_bits_per_volume": float(
                np.mean([frame["metrics"]["bits_per_volume"] for frame in frames])
            ),
        }
        write_json(OUTPUT / "summary.json", {"aggregate": aggregate, "frames": frames})
        shutil.copy2(ROOT / "configs/train_basketball.yaml", OUTPUT / "train_config.yaml")
        shutil.copy2(ROOT / "configs/eval_basketball.yaml", OUTPUT / "eval_config.yaml")
        status.update(
            state="complete",
            updated_at=now(),
            finished_at=now(),
            aggregate=aggregate,
            outputs={
                "summary": "summary.json",
                "normalized": "normalized",
                "original_scale": "original_scale",
            },
        )
        write_json(STATUS, status)
    except Exception as error:
        status.update(state="failed", updated_at=now(), error=str(error))
        write_json(STATUS, status)
        raise


if __name__ == "__main__":
    main()
