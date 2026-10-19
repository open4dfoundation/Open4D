"""Image-quality metrics and the Vega training loss (paper Eq. 2)."""
from __future__ import annotations

import torch
import torch.nn.functional as F


def psnr(pred: torch.Tensor, gt: torch.Tensor) -> torch.Tensor:
    mse = F.mse_loss(pred, gt)
    return -10.0 * torch.log10(mse.clamp_min(1e-10))


def _gaussian_kernel_1d(window_size: int, sigma: float, device, dtype) -> torch.Tensor:
    coords = torch.arange(window_size, device=device, dtype=dtype) - window_size // 2
    g = torch.exp(-(coords ** 2) / (2 * sigma ** 2))
    return g / g.sum()


def _ssim_window(window_size: int, channels: int, device, dtype) -> torch.Tensor:
    k1d = _gaussian_kernel_1d(window_size, sigma=1.5, device=device, dtype=dtype)
    k2d = k1d.unsqueeze(1) @ k1d.unsqueeze(0)
    window = k2d.expand(channels, 1, window_size, window_size).contiguous()
    return window


def ssim(pred: torch.Tensor, gt: torch.Tensor, window_size: int = 11) -> torch.Tensor:
    """Standard single-scale SSIM (Wang et al. 2004), computed with a
    depthwise Gaussian-window convolution. Expects (C, H, W) or (B, C, H, W)
    tensors in [0, 1].
    """
    if pred.dim() == 3:
        pred = pred.unsqueeze(0)
        gt = gt.unsqueeze(0)
    C = pred.shape[1]
    window = _ssim_window(window_size, C, pred.device, pred.dtype)
    pad = window_size // 2

    mu1 = F.conv2d(pred, window, padding=pad, groups=C)
    mu2 = F.conv2d(gt, window, padding=pad, groups=C)
    mu1_sq, mu2_sq, mu1_mu2 = mu1 * mu1, mu2 * mu2, mu1 * mu2

    sigma1_sq = F.conv2d(pred * pred, window, padding=pad, groups=C) - mu1_sq
    sigma2_sq = F.conv2d(gt * gt, window, padding=pad, groups=C) - mu2_sq
    sigma12 = F.conv2d(pred * gt, window, padding=pad, groups=C) - mu1_mu2

    c1, c2 = 0.01 ** 2, 0.03 ** 2
    ssim_map = ((2 * mu1_mu2 + c1) * (2 * sigma12 + c2)) / (
        (mu1_sq + mu2_sq + c1) * (sigma1_sq + sigma2_sq + c2)
    )
    return ssim_map.mean()


def vega_loss(pred: torch.Tensor, gt: torch.Tensor, alpha: float = 0.8, beta: float = 0.2) -> torch.Tensor:
    """Eq. 2: L(I) = alpha * MSE(I_gt, I) + beta * (1 - SSIM(I_gt, I))."""
    mse = F.mse_loss(pred, gt)
    s = ssim(pred, gt)
    return alpha * mse + beta * (1 - s)


def sse(pred: torch.Tensor, gt: torch.Tensor) -> torch.Tensor:
    """Eq. 6: sum of squared errors, used as the distortion D(i) in the GOV
    rate-distortion optimization."""
    return ((gt - pred) ** 2).sum()
