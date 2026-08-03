from __future__ import annotations

import torch
import torch.nn as nn


class LatentQuantizer(nn.Module):
    def __init__(self, mode: str = "ste"):
        super().__init__()
        if mode not in {"ste", "noise"}:
            raise ValueError(f"Unsupported quantization mode '{mode}'.")
        self.mode = mode

    def forward(self, latent: torch.Tensor) -> torch.Tensor:
        if self.training:
            if self.mode == "noise":
                noise = torch.empty_like(latent).uniform_(-0.5, 0.5)
                return latent + noise
            return latent + (torch.round(latent) - latent).detach()
        return torch.round(latent)
