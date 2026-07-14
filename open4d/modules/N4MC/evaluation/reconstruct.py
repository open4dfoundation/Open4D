from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import torch

from data import TSDFVolumeDataset
from evaluation.metrics import compute_mesh_metrics, reconstruct_mesh_from_tsdf
from training.common import build_model, reconstruct_volume, resolve_device
from utils import ensure_dir, load_config, save_json
from utils.config import apply_overrides


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Reconstruct TSDF volumes and meshes.")
    parser.add_argument("--config", required=True, help="Path to a YAML config file.")
    parser.add_argument("--checkpoint", required=True, help="Checkpoint produced by training.")
    parser.add_argument("--split", default="val", choices=["train", "val", "test"])
    parser.add_argument("--output-dir", default="outputs/reconstruction")
    parser.add_argument(
        "--set",
        dest="overrides",
        action="append",
        default=[],
        help="Override config values, for example: --set data.limit=1",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    config = apply_overrides(load_config(args.config), args.overrides)
    device = resolve_device(config.get("training", {}).get("device"))

    dataset = TSDFVolumeDataset.from_mapping(config["data"], split=args.split)
    checkpoint = torch.load(args.checkpoint, map_location=device)
    model = build_model(config, device)
    model.load_state_dict(checkpoint["model"])
    model.eval()

    output_dir = ensure_dir(args.output_dir)
    summary: dict[str, dict[str, float]] = {}

    for sample in dataset:
        target = sample["tsdf"]
        reconstructed = reconstruct_volume(model, target, config, device)
        prediction = reconstructed["reconstruction"]
        total_bits = float(reconstructed["rate_bits"])
        quantized_latent = reconstructed["quantized_latent"]
        latent = reconstructed["latent"]
        original_shape = reconstructed["original_shape"]
        padded_shape = reconstructed["padded_shape"]
        bottleneck_shape = reconstructed["bottleneck_shape"]
        frame_id = sample["frame_id"]

        np.savez_compressed(output_dir / f"{frame_id}.npz", sdf=prediction.numpy())
        np.save(output_dir / f"{frame_id}_quantized_latent.npy", quantized_latent.numpy())
        np.savez_compressed(
            output_dir / f"{frame_id}_latent_pack.npz",
            latent=latent.numpy(),
            quantized_latent=quantized_latent.numpy(),
            original_shape=original_shape.numpy(),
            padded_shape=padded_shape.numpy(),
            bottleneck_shape=bottleneck_shape.numpy(),
        )

        pred_mesh = reconstruct_mesh_from_tsdf(prediction)
        gt_mesh = reconstruct_mesh_from_tsdf(target)
        if pred_mesh is not None:
            pred_mesh.export(output_dir / f"{frame_id}.ply")
        metrics = compute_mesh_metrics(
            pred_mesh=pred_mesh,
            gt_mesh=gt_mesh,
            num_surface_samples=int(config.get("evaluation", {}).get("mesh_num_samples", 2048)),
        )
        metrics["bits_per_volume"] = float(total_bits)
        summary[frame_id] = metrics

    save_json(summary, output_dir / "summary.json")


if __name__ == "__main__":
    main()
