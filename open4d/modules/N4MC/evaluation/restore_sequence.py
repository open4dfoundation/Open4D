"""Restore N4MC sequence reconstructions to the original mesh coordinates."""

from __future__ import annotations

import argparse
from pathlib import Path
import shutil

import numpy as np
import trimesh


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-dir", type=Path, required=True)
    parser.add_argument("--normalization", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    normalization = np.load(args.normalization)
    center = np.asarray(normalization["center"], dtype=np.float64)
    scale = float(normalization["scale"])
    source_frames = [str(name) for name in normalization["source_frames"]]
    if not np.isfinite(scale) or scale <= 0:
        raise ValueError(f"invalid normalization scale: {scale}")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    for index, source_name in enumerate(source_frames):
        frame_id = f"{index:04d}"
        input_path = args.input_dir / f"{frame_id}.ply"
        if not input_path.is_file():
            raise FileNotFoundError(input_path)
        mesh = trimesh.load(input_path, force="mesh", process=False)
        mesh.vertices = np.asarray(mesh.vertices) / scale + center
        output_name = f"{Path(source_name).stem}_reconstructed.ply"
        mesh.export(args.output_dir / output_name)

    shutil.copy2(args.normalization, args.output_dir / "normalization.npz")
    print(f"restored {len(source_frames)} frames to {args.output_dir}")


if __name__ == "__main__":
    main()
