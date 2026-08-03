from __future__ import annotations

import math

import torch
import torch.nn as nn
import torch.nn.functional as F


class GaussianEntropyModel(nn.Module):
    def __init__(self, channels: int, scale_init: float = 1.0, min_scale: float = 1e-3):
        super().__init__()
        initial_value = math.log(math.exp(scale_init) - 1.0)
        self.log_scales = nn.Parameter(torch.full((channels,), initial_value, dtype=torch.float32))
        self.min_scale = min_scale

    def _channel_scales(self, latent: torch.Tensor) -> torch.Tensor:
        scales = F.softplus(self.log_scales) + self.min_scale
        shape = [1, -1] + [1] * (latent.ndim - 2)
        return scales.view(*shape)

    def forward(self, quantized_latent: torch.Tensor) -> torch.Tensor:
        scales = self._channel_scales(quantized_latent)
        sqrt_two = math.sqrt(2.0)
        upper = (quantized_latent + 0.5) / (scales * sqrt_two)
        lower = (quantized_latent - 0.5) / (scales * sqrt_two)
        likelihood = 0.5 * (torch.erf(upper) - torch.erf(lower))
        likelihood = likelihood.clamp_min(1e-9)
        return -torch.log2(likelihood)
