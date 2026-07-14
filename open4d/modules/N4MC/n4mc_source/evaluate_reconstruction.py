"""Small, deterministic geometry evaluation for pipeline smoke tests and reports."""
from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
import trimesh
from natsort import natsorted
from scipy.spatial import cKDTree


EXTENSIONS = {".obj", ".ply", ".stl"}


def mesh_files(directory: Path) -> list[Path]:
    return [Path(p) for p in natsorted(
        [str(p) for p in directory.iterdir() if p.suffix.lower() in EXTENSIONS]
    )]


def load_mesh(path: Path) -> trimesh.Trimesh | None:
    try:
        mesh = trimesh.load_mesh(path, process=False)
        if isinstance(mesh, trimesh.Scene):
            mesh = mesh.dump(concatenate=True)
        if len(mesh.vertices) == 0 or len(mesh.faces) == 0:
            return None
        return mesh
    except (ValueError, IndexError):
        return None


def evaluate(original_dir: Path, decoded_dir: Path, samples: int, seed: int = 0) -> dict:
    originals, decoded = mesh_files(original_dir), mesh_files(decoded_dir)
    if len(originals) != len(decoded):
        raise ValueError(f"frame count mismatch: {len(originals)} original, {len(decoded)} decoded")
    rng = np.random.default_rng(seed)
    frames = []
    for index, (source_path, decoded_path) in enumerate(zip(originals, decoded)):
        source, reconstruction = load_mesh(source_path), load_mesh(decoded_path)
        if source is None:
            raise ValueError(f"source mesh is empty or unreadable: {source_path}")
        if reconstruction is None:
            frames.append({"index": index, "source": source_path.name,
                           "decoded": decoded_path.name, "status": "empty",
                           "rmse": None, "symmetric_chamfer": None})
            continue
        # trimesh uses NumPy's global RNG; seed each frame for reproducibility.
        np.random.seed(int(rng.integers(0, 2**31 - 1)))
        a, _ = trimesh.sample.sample_surface(source, samples)
        b, _ = trimesh.sample.sample_surface(reconstruction, samples)
        d_ab = cKDTree(b).query(a, workers=-1)[0]
        d_ba = cKDTree(a).query(b, workers=-1)[0]
        rmse = float(np.sqrt((np.mean(d_ab ** 2) + np.mean(d_ba ** 2)) / 2))
        chamfer = float(np.mean(d_ab ** 2) + np.mean(d_ba ** 2))
        frames.append({"index": index, "source": source_path.name,
                       "decoded": decoded_path.name, "status": "ok", "rmse": rmse,
                       "symmetric_chamfer": chamfer})
    valid = [frame for frame in frames if frame["status"] == "ok"]
    return {
        "frames": frames,
        "frame_count": len(frames),
        "valid_frame_count": len(valid),
        "empty_frame_count": len(frames) - len(valid),
        "samples_per_mesh": samples,
        "mean_rmse": float(np.mean([f["rmse"] for f in valid])) if valid else None,
        "mean_symmetric_chamfer": float(np.mean([f["symmetric_chamfer"] for f in valid])) if valid else None,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Evaluate decoded N4MC geometry")
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--decoded", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--samples", type=int, default=10000)
    args = parser.parse_args()
    report = evaluate(args.original, args.decoded, args.samples)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({k: v for k, v in report.items() if k != "frames"}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
