from __future__ import annotations

from contextlib import nullcontext
from pathlib import Path
import time

import numpy as np
import torch
from torch.utils.data import DataLoader

from data import TSDFVolumeDataset
from evaluation.metrics import (
    compute_compression_metrics,
    compute_mesh_metrics,
    compute_voxel_metrics,
    reconstruct_mesh_from_tsdf,
)
from losses import compute_rd_loss
from models import TSDFCompressionAutoencoder


def resolve_device(requested: str | None) -> torch.device:
    if requested in {None, "auto"}:
        return torch.device("cuda" if torch.cuda.is_available() else "cpu")
    return torch.device(requested)


def build_dataloaders(config: dict) -> tuple[TSDFVolumeDataset, TSDFVolumeDataset, DataLoader, DataLoader]:
    dataset_cfg = config["data"]
    train_dataset = TSDFVolumeDataset.from_mapping(dataset_cfg, split="train")
    val_dataset = TSDFVolumeDataset.from_mapping(dataset_cfg, split="val")

    batch_size = int(dataset_cfg.get("batch_size", 1))
    workers = int(dataset_cfg.get("workers", 0))
    pin_memory = bool(dataset_cfg.get("pin_memory", torch.cuda.is_available()))
    loader_kwargs = {
        "batch_size": batch_size,
        "num_workers": workers,
        "pin_memory": pin_memory,
        "persistent_workers": workers > 0,
    }

    train_loader = DataLoader(train_dataset, shuffle=True, **loader_kwargs)
    val_loader = DataLoader(val_dataset, shuffle=False, **loader_kwargs)
    return train_dataset, val_dataset, train_loader, val_loader


def build_model(config: dict, device: torch.device) -> TSDFCompressionAutoencoder:
    model_cfg = config["model"]
    model = TSDFCompressionAutoencoder(
        in_channels=int(model_cfg.get("in_channels", 1)),
        out_channels=int(model_cfg.get("out_channels", 1)),
        hidden_channels=tuple(model_cfg.get("hidden_channels", [24, 48, 96])),
        latent_channels=int(model_cfg.get("embed_dim", model_cfg.get("latent_channels", 24))),
        embed_hwd=model_cfg.get("embed_hwd"),
        quantization_mode=model_cfg.get("quantization_mode", "ste"),
        prior_scale_init=float(model_cfg.get("prior_scale_init", 1.0)),
    )
    return model.to(device)


def _autocast_context(device: torch.device, enabled: bool):
    if not enabled or device.type != "cuda":
        return nullcontext()
    return torch.amp.autocast(device_type="cuda", dtype=torch.float16)


def prepare_training_input(
    batch_tsdf: torch.Tensor,
    data_cfg: dict,
    generator: torch.Generator | None,
) -> torch.Tensor:
    del data_cfg
    del generator
    return batch_tsdf


@torch.no_grad()
def reconstruct_volume(
    model: torch.nn.Module,
    volume: torch.Tensor,
    config: dict,
    device: torch.device,
) -> dict[str, torch.Tensor | float]:
    use_amp = bool(config.get("training", {}).get("amp", False))

    model.eval()
    with _autocast_context(device, use_amp):
        outputs = model(volume.unsqueeze(0).to(device))
    return {
        "reconstruction": outputs["reconstruction"][0].float().cpu(),
        "quantized_latent": outputs["quantized_latent"][0].cpu(),
        "latent": outputs["latent"][0].float().cpu(),
        "rate_bits": float(outputs["rate_bits"].item()),
        "rate_bpv": float(outputs["rate_bpv"].item()),
        "original_shape": outputs["original_shape"].cpu(),
        "padded_shape": outputs["padded_shape"].cpu(),
        "bottleneck_shape": outputs["bottleneck_shape"].cpu(),
    }


@torch.no_grad()
def run_validation(
    model: torch.nn.Module,
    dataloader: DataLoader,
    config: dict,
    device: torch.device,
    output_dir: Path | None = None,
    save_predictions: bool = False,
) -> dict[str, float]:
    model.eval()
    totals: dict[str, list[float]] = {}

    for batch in dataloader:
        batch_tsdf = batch["tsdf"]
        for sample_index in range(batch_tsdf.shape[0]):
            print(f"Processing sample {sample_index + 1}/{batch_tsdf.shape[0]}")
            target = batch_tsdf[sample_index]
            reconstructed = reconstruct_volume(model, target, config, device)
            prediction = reconstructed["reconstruction"]
            total_bits = float(reconstructed["rate_bits"])
            quantized_latent = reconstructed["quantized_latent"]
            latent = reconstructed["latent"]
            original_shape = reconstructed["original_shape"]
            padded_shape = reconstructed["padded_shape"]
            bottleneck_shape = reconstructed["bottleneck_shape"]

            outputs = {
                "reconstruction": prediction.unsqueeze(0).to(device),
                "rate_bpv": torch.tensor(
                    total_bits / max(int(np.prod(target.shape[1:])), 1),
                    device=device,
                    dtype=torch.float32,
                ),
            }
            total_loss, rd_terms = compute_rd_loss(outputs, target.unsqueeze(0).to(device), config["loss"])
            voxel_metrics = compute_voxel_metrics(
                prediction=prediction,
                target=target,
                narrow_band_threshold=float(config["loss"].get("narrow_band_threshold", 0.1)),
            )
            compression = compute_compression_metrics(
                total_bits=total_bits,
                volume_shape=target.shape,
                raw_bits_per_value=int(config.get("evaluation", {}).get("raw_bits_per_value", 32)),
            )

            gt_mesh = reconstruct_mesh_from_tsdf(target)
            pred_mesh = reconstruct_mesh_from_tsdf(prediction)
            mesh_metrics = compute_mesh_metrics(
                pred_mesh=pred_mesh,
                gt_mesh=gt_mesh,
                num_surface_samples=int(config.get("evaluation", {}).get("mesh_num_samples", 2048)),
            )

            metrics = {
                "total_loss": float(total_loss.item()),
                "rate_bpv": float(rd_terms["rate_bpv"].item()),
                "rec_loss": float(rd_terms["rec_loss"].item()),
                "band_loss": float(rd_terms["band_loss"].item()),
                "sign_loss": float(rd_terms["sign_loss"].item()),
                #"ssim_loss": float(rd_terms["ssim_loss"].item()),
                **voxel_metrics,
                **compression,
                **mesh_metrics,
            }

            for key, value in metrics.items():
                totals.setdefault(key, []).append(float(value))

            if output_dir is not None and save_predictions:
                frame_id = batch["frame_id"][sample_index]
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
                if pred_mesh is not None:
                    pred_mesh.export(output_dir / f"{frame_id}.obj")
                if gt_mesh is not None:
                    gt_mesh.export(output_dir / f"gt_{frame_id}.obj")

    aggregated = {key: float(np.mean(values)) for key, values in totals.items()}
    return aggregated


def format_metrics(metrics: dict[str, float]) -> str:
    ordered = sorted(metrics.items())
    return ", ".join(f"{key}={value:.6f}" for key, value in ordered)


def checkpoint_payload(
    model: torch.nn.Module,
    optimizer: torch.optim.Optimizer,
    scaler: torch.amp.GradScaler | None,
    epoch: int,
    config: dict,
    best_metric: float,
) -> dict:
    payload = {
        "model": model.state_dict(),
        "optimizer": optimizer.state_dict(),
        "epoch": epoch,
        "config": config,
        "best_metric": best_metric,
        "saved_at": time.time(),
    }
    if scaler is not None:
        payload["scaler"] = scaler.state_dict()
    return payload
