from __future__ import annotations

from typing import Sequence

import torch
import torch.nn as nn
import torch.nn.functional as F

from .entropy import GaussianEntropyModel
from .quantization import LatentQuantizer


def _as_3tuple(value: int | Sequence[int] | None) -> tuple[int, int, int] | None:
    if value is None:
        return None
    if isinstance(value, int):
        return (value, value, value)
    values = tuple(int(v) for v in value)
    if len(values) != 3:
        raise ValueError(f"Expected 3 latent spatial dims, received {values}.")
    return values


def _conv_block(in_channels: int, out_channels: int, stride: int) -> nn.Sequential:
    return nn.Sequential(
        nn.Conv3d(in_channels, out_channels, kernel_size=3, stride=stride, padding=1),
        nn.GroupNorm(num_groups=1, num_channels=out_channels),
        nn.GELU(),
        nn.Conv3d(out_channels, out_channels, kernel_size=3, stride=1, padding=1),
        nn.GroupNorm(num_groups=1, num_channels=out_channels),
        nn.GELU(),
    )


def _deconv_block(in_channels: int, out_channels: int) -> nn.Sequential:
    return nn.Sequential(
        nn.ConvTranspose3d(
            in_channels,
            out_channels,
            kernel_size=4,
            stride=2,
            padding=1,
        ),
        nn.GroupNorm(num_groups=1, num_channels=out_channels),
        nn.GELU(),
        nn.Conv3d(out_channels, out_channels, kernel_size=3, stride=1, padding=1),
        nn.GroupNorm(num_groups=1, num_channels=out_channels),
        nn.GELU(),
    )


class Encoder3D(nn.Module):
    def __init__(
        self,
        in_channels: int,
        hidden_channels: Sequence[int],
        latent_channels: int,
        embed_hwd: int | Sequence[int] | None = None,
    ):
        super().__init__()
        channels = [in_channels, *hidden_channels]
        blocks = []
        for in_ch, out_ch in zip(channels[:-1], channels[1:]):
            blocks.append(_conv_block(in_ch, out_ch, stride=2))
        self.blocks = nn.Sequential(*blocks)
        pooled_shape = _as_3tuple(embed_hwd)
        self.pool = nn.Identity() if pooled_shape is None else nn.AdaptiveAvgPool3d(pooled_shape)
        self.to_latent = nn.Conv3d(hidden_channels[-1], latent_channels, kernel_size=3, padding=1)

    def forward(self, x: torch.Tensor) -> tuple[torch.Tensor, tuple[int, int, int]]:
        bottleneck = self.blocks(x)
        bottleneck_shape = tuple(int(v) for v in bottleneck.shape[-3:])
        pooled = self.pool(bottleneck)
        return self.to_latent(pooled), bottleneck_shape


class Decoder3D(nn.Module):
    def __init__(
        self,
        latent_channels: int,
        hidden_channels: Sequence[int],
        out_channels: int,
    ):
        super().__init__()
        reversed_hidden = list(hidden_channels[::-1])
        blocks = []
        in_channels = latent_channels
        for out_channels_block in reversed_hidden:
            blocks.append(_deconv_block(in_channels, out_channels_block))
            in_channels = out_channels_block
        self.blocks = nn.Sequential(*blocks)
        self.to_output = nn.Conv3d(in_channels, out_channels, kernel_size=3, padding=1)

    def forward(self, latent: torch.Tensor, bottleneck_shape: tuple[int, int, int]) -> torch.Tensor:
        if tuple(int(v) for v in latent.shape[-3:]) != tuple(int(v) for v in bottleneck_shape):
            latent = F.interpolate(latent, size=bottleneck_shape, mode="trilinear", align_corners=False)
        return torch.tanh(self.to_output(self.blocks(latent)))


class TSDFCompressionAutoencoder(nn.Module):
    def __init__(
        self,
        in_channels: int = 1,
        out_channels: int = 1,
        hidden_channels: Sequence[int] = (24, 48, 96),
        latent_channels: int = 24,
        embed_hwd: int | Sequence[int] | None = None,
        quantization_mode: str = "ste",
        prior_scale_init: float = 1.0,
    ):
        super().__init__()
        if not hidden_channels:
            raise ValueError("hidden_channels must not be empty.")
        self.downsample_factor = 2 ** len(hidden_channels)
        self.encoder = Encoder3D(in_channels, hidden_channels, latent_channels, embed_hwd=embed_hwd)
        self.quantizer = LatentQuantizer(mode=quantization_mode)
        self.entropy_model = GaussianEntropyModel(latent_channels, scale_init=prior_scale_init)
        self.decoder = Decoder3D(latent_channels, hidden_channels, out_channels)
        self.embed_hwd = _as_3tuple(embed_hwd)

    def _pad_input(self, x: torch.Tensor) -> tuple[torch.Tensor, tuple[int, int, int]]:
        spatial_shape = x.shape[-3:]
        pad_d = (self.downsample_factor - spatial_shape[0] % self.downsample_factor) % self.downsample_factor
        pad_h = (self.downsample_factor - spatial_shape[1] % self.downsample_factor) % self.downsample_factor
        pad_w = (self.downsample_factor - spatial_shape[2] % self.downsample_factor) % self.downsample_factor
        if pad_d == 0 and pad_h == 0 and pad_w == 0:
            return x, spatial_shape
        padded = F.pad(x, (0, pad_w, 0, pad_h, 0, pad_d), mode="replicate")
        return padded, spatial_shape

    def encode(self, x: torch.Tensor) -> dict[str, torch.Tensor]:
        padded_x, original_shape = self._pad_input(x)
        padded_shape = tuple(int(v) for v in padded_x.shape[-3:])
        latent, bottleneck_shape = self.encoder(padded_x)
        quantized = self.quantizer(latent)
        return {
            "latent": latent,
            "quantized_latent": quantized,
            "original_shape": torch.tensor(original_shape, device=x.device, dtype=torch.long),
            "padded_shape": torch.tensor(padded_shape, device=x.device, dtype=torch.long),
            "bottleneck_shape": torch.tensor(bottleneck_shape, device=x.device, dtype=torch.long),
        }

    def decode_quantized_latent(
        self,
        quantized_latent: torch.Tensor,
        bottleneck_shape: torch.Tensor | Sequence[int],
        original_shape: torch.Tensor | Sequence[int],
    ) -> torch.Tensor:
        bottleneck_shape_tuple = tuple(int(v) for v in bottleneck_shape)
        original_shape_tuple = tuple(int(v) for v in original_shape)
        reconstruction = self.decoder(quantized_latent, bottleneck_shape=bottleneck_shape_tuple)
        return reconstruction[
            ...,
            : original_shape_tuple[0],
            : original_shape_tuple[1],
            : original_shape_tuple[2],
        ]

    def forward(self, x: torch.Tensor) -> dict[str, torch.Tensor]:
        encoded = self.encode(x)
        latent = encoded["latent"]
        quantized = encoded["quantized_latent"]
        original_shape = tuple(int(v) for v in encoded["original_shape"].tolist())
        bottleneck_shape = tuple(int(v) for v in encoded["bottleneck_shape"].tolist())
        reconstruction = self.decode_quantized_latent(quantized, bottleneck_shape, original_shape)
        rate_bits_tensor = self.entropy_model(quantized)
        rate_bits = rate_bits_tensor.sum()
        num_voxels = x.shape[0] * x.shape[2] * x.shape[3] * x.shape[4]
        rate_bpv = rate_bits / max(num_voxels, 1)
        return {
            "reconstruction": reconstruction,
            "latent": latent,
            "quantized_latent": quantized,
            "rate_bits": rate_bits,
            "rate_bpv": rate_bpv,
            "rate_bits_tensor": rate_bits_tensor,
            "original_shape": encoded["original_shape"],
            "padded_shape": encoded["padded_shape"],
            "bottleneck_shape": encoded["bottleneck_shape"],
        }
