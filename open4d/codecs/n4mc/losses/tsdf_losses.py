from __future__ import annotations

import math

import torch
import torch.nn.functional as F


def reconstruction_loss(prediction: torch.Tensor, target: torch.Tensor, loss_type: str) -> torch.Tensor:
    if loss_type == "l1":
        return torch.mean(torch.abs(prediction - target))
    if loss_type == "smooth_l1":
        return F.smooth_l1_loss(prediction, target)
    raise ValueError(f"Unsupported reconstruction loss '{loss_type}'.")


def narrow_band_loss(
    prediction: torch.Tensor,
    target: torch.Tensor,
    threshold: float,
    mode: str = "hard",
    alpha: float = 8.0,
) -> torch.Tensor:
    abs_target = target.abs()
    if mode == "hard":
        weights = (abs_target <= threshold).float()
    elif mode == "soft":
        weights = torch.exp(-alpha * abs_target)
    else:
        raise ValueError(f"Unsupported narrow-band mode '{mode}'.")

    normalizer = weights.sum().clamp_min(1.0)
    return (weights * torch.abs(prediction - target)).sum() / normalizer


def sign_consistency_loss(
    prediction: torch.Tensor,
    target: torch.Tensor,
    temperature: float = 0.1,
) -> torch.Tensor:
    target_sign = torch.where(target >= 0.0, torch.ones_like(target), -torch.ones_like(target))
    logits = target_sign * prediction / temperature
    return F.softplus(-logits).mean()


def _gaussian_kernel_3d(
    channels: int,
    window_size: int,
    sigma: float,
    device: torch.device,
    dtype: torch.dtype,
) -> torch.Tensor:
    coords = torch.arange(window_size, device=device, dtype=dtype) - (window_size - 1) / 2.0
    gaussian_1d = torch.exp(-(coords.square()) / (2.0 * sigma * sigma))
    gaussian_1d = gaussian_1d / gaussian_1d.sum()
    kernel = (
        gaussian_1d[:, None, None]
        * gaussian_1d[None, :, None]
        * gaussian_1d[None, None, :]
    )
    kernel = kernel / kernel.sum()
    return kernel.view(1, 1, window_size, window_size, window_size).repeat(channels, 1, 1, 1, 1)


def ssim_loss(
    prediction: torch.Tensor,
    target: torch.Tensor,
    window_size: int = 5,
    sigma: float = 1.5,
) -> torch.Tensor:
    if prediction.shape != target.shape:
        raise ValueError("Prediction and target must share the same shape for SSIM loss.")
    channels = prediction.shape[1]
    kernel = _gaussian_kernel_3d(
        channels=channels,
        window_size=window_size,
        sigma=sigma,
        device=prediction.device,
        dtype=prediction.dtype,
    )
    padding = window_size // 2

    pred_norm = (prediction + 1.0) / 2.0
    target_norm = (target + 1.0) / 2.0

    mu_pred = F.conv3d(pred_norm, kernel, padding=padding, groups=channels)
    mu_target = F.conv3d(target_norm, kernel, padding=padding, groups=channels)

    mu_pred_sq = mu_pred.square()
    mu_target_sq = mu_target.square()
    mu_cross = mu_pred * mu_target

    sigma_pred = F.conv3d(pred_norm * pred_norm, kernel, padding=padding, groups=channels) - mu_pred_sq
    sigma_target = F.conv3d(target_norm * target_norm, kernel, padding=padding, groups=channels) - mu_target_sq
    sigma_cross = F.conv3d(pred_norm * target_norm, kernel, padding=padding, groups=channels) - mu_cross

    c1 = 0.01 ** 2
    c2 = 0.03 ** 2

    numerator = (2.0 * mu_cross + c1) * (2.0 * sigma_cross + c2)
    denominator = (mu_pred_sq + mu_target_sq + c1) * (sigma_pred + sigma_target + c2)
    ssim_value = (numerator / denominator.clamp_min(1e-8)).mean()
    return 1.0 - ssim_value


def compute_rd_loss(outputs: dict, target: torch.Tensor, config: dict) -> tuple[torch.Tensor, dict[str, torch.Tensor]]:
    prediction = outputs["reconstruction"].float()
    target = target.float()
    rec = reconstruction_loss(prediction, target, config.get("reconstruction", "l1"))
    band = narrow_band_loss(
        prediction=prediction,
        target=target,
        threshold=float(config.get("narrow_band_threshold", 0.1)),
        mode=config.get("narrow_band_mode", "hard"),
        alpha=float(config.get("narrow_band_alpha", 8.0)),
    )
    sign = sign_consistency_loss(
        prediction=prediction,
        target=target,
        temperature=float(config.get("sign_temperature", 0.1)),
    )
    '''
    ssim = ssim_loss(
        prediction=prediction,
        target=target,
        window_size=int(config.get("ssim_window_size", 5)),
        sigma=float(config.get("ssim_sigma", 1.5)),
    )
    '''
    rate = outputs["rate_bpv"].float()

    total = (
        float(config.get("lambda_rate", 1e-4)) * rate
        + float(config.get("lambda_rec", 1.0)) * rec
        + float(config.get("lambda_band", 1.0)) * band
        + float(config.get("lambda_sign", 0.1)) * sign
        #+ float(config.get("lambda_ssim", 0.1)) * ssim
    )
    return total, {
        "total_loss": total.detach(),
        "rate_bpv": rate.detach(),
        "rec_loss": rec.detach(),
        "band_loss": band.detach(),
        "sign_loss": sign.detach(),
        #"ssim_loss": ssim.detach(),
    }
