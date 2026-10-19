"""Hierarchical color encoding — paper §5.2, Fig. 6.

Key frames: position + view direction -> big hash table -> MLP -> RGB.
Residual frames: the tiny hash table's embedding (for this frame) is added
to the (frozen) big hash's embedding from the key frame, then fed through
the same MLP. Only the tiny hash is trained for residual frames; only the
big hash + MLP are trained for key frames — matching the paper's
description of the training process in §5.2.

Training objective: §5.2 frames this as *compressing* the SH coefficients
that a (separately, already-trained) 3DGS scene already has — "the SH
coefficients... are represented as relatively small hash tables and MLP
models" — i.e. distillation of already-known per-Gaussian colors into a
compact implicit function, not from-scratch photometric novel-view
synthesis. So training here directly regresses each Gaussian's own known
color rather than differentiating through a full render against captured
images.

The regression target is the Gaussian's *full* SH evaluated at the sampled
view direction (`eval_sh(...) + 0.5`, the same value the rasterizer computes),
not the view-independent DC term alone. An earlier version used the DC term
while still feeding random directions in, which taught the model that its
direction input must be ignored — the SphericalHarmonics encoding became noise
the MLP had to learn to suppress, and every higher-order coefficient the
upstream reconstruction produced was discarded. Measured on basketball frame 21
(decoded render vs the SH render it is reproducing, 800 iters): 50.3 dB
against the full-SH target vs 49.3/49.6 dB across two DC-target runs. The gain
is ~1 dB rather than large because view dependence is a small part of the
signal here (mean |sh_rest| is ~1% of mean |sh_dc| after refinement), but the
direction input is at least no longer actively misleading. (An earlier version of this module trained against
full-image rendering loss directly; on real capture data with mostly-black
backgrounds that reliably collapsed to a "predict black everywhere" local
optimum — dominant background MSE with no per-point supervision signal.
Point-wise regression against the known target color sidesteps that
entirely and is both more sample-efficient and truer to the paper's framing
of this stage as compression.)

Sized (via `DEFAULT_COLOR_CONFIG` below) in the spirit of the paper's
reported footprint: big hash ~19MB, tiny hash ~0.1MB, MLP ~0.01MB — the
paper quotes 24MB / 0.125MB / 0.03MB (§7), with the same ~200x big:tiny
ratio that motivates the hierarchy.
"""
from __future__ import annotations

import dataclasses

import torch
import torch.nn as nn
import torch.nn.functional as F
import tinycudann as tcnn

from vega.gaussians import GaussianSet
from vega.sh import eval_sh


@dataclasses.dataclass
class ColorEncodingConfig:
    big_hash: dict
    tiny_hash: dict
    dir_encoding: dict
    mlp: dict


DEFAULT_COLOR_CONFIG = ColorEncodingConfig(
    big_hash=dict(otype="HashGrid", n_levels=12, n_features_per_level=2,
                  log2_hashmap_size=18, base_resolution=16, per_level_scale=1.5),
    # n_levels must match big_hash (their embeddings are summed elementwise
    # in forward_residual), so the size reduction comes entirely from a much
    # smaller log2_hashmap_size (fewer entries per level).
    #
    # 2^12 rather than 2^10. At 2^10 the table holds 1024 entries per level,
    # which for the ~70k Gaussians of an ORBIT subject collides badly enough to
    # be visible as speckle: residual frames landed ~14 dB below key frames
    # against the SH colours they are meant to reproduce. It is a capacity
    # limit, not a training one — raising residual_iters 300 -> 1500 at 2^10
    # bought +0.25 dB, while widening the table fixed it outright. Measured on
    # basketball (residual PSNR vs the SH ceiling, and the resulting 30-frame
    # bitstream):
    #     2^10   48 KB   40.0 dB    64.8 MB
    #     2^12  192 KB   44.9 dB    73.3 MB
    #     2^13  384 KB   48.9 DB    83.8 MB
    #     2^14  768 KB   52.5 dB   104.1 MB
    # 2^12 is also the closest of these to the ~0.125 MB tiny-hash footprint
    # the paper reports in §7 — 2^10 was well under that budget, so this is
    # more faithful to the paper, not less. Tunable via --tiny-hash-log2.
    tiny_hash=dict(otype="HashGrid", n_levels=12, n_features_per_level=2,
                   log2_hashmap_size=12, base_resolution=16, per_level_scale=1.5),
    dir_encoding=dict(otype="SphericalHarmonics", degree=4),
    # A plain float32 torch MLP rather than tcnn's fused fp16 MLP: with real
    # capture data (very dark backgrounds, small bright foreground subjects)
    # the fp16 FullyFusedMLP reliably collapsed within a handful of
    # iterations (ReLU units saturating dead, Sigmoid output underflowing to
    # exactly 0.0 and staying there — loss stopped changing bit-for-bit
    # regardless of how many more iterations ran). Keeping the hash grids in
    # tcnn (their CUDA kernels, not prone to this) but decoding with a small
    # standard nn.Sequential avoids the precision collapse; ~0.03MB either way.
    mlp=dict(n_neurons=48, n_hidden_layers=2),
)


class HierarchicalColorModel(nn.Module):
    """One instance per Group-of-Volumes (GOV): trained once on the key
    frame, then reused (with a fresh tiny hash) for every residual frame in
    the group.
    """

    def __init__(self, config: ColorEncodingConfig = DEFAULT_COLOR_CONFIG,
                 bbox_min: torch.Tensor = None, bbox_max: torch.Tensor = None):
        super().__init__()
        self.config = config
        self.big_hash = tcnn.Encoding(n_input_dims=3, encoding_config=config.big_hash)
        self.dir_enc = tcnn.Encoding(n_input_dims=3, encoding_config=config.dir_encoding)
        mlp_in = self.big_hash.n_output_dims + self.dir_enc.n_output_dims
        h = config.mlp["n_neurons"]
        layers = [nn.Linear(mlp_in, h), nn.ReLU(inplace=True)]
        for _ in range(config.mlp["n_hidden_layers"] - 1):
            layers += [nn.Linear(h, h), nn.ReLU(inplace=True)]
        layers += [nn.Linear(h, 3), nn.Sigmoid()]
        self.mlp = nn.Sequential(*layers)
        self.register_buffer("bbox_min", bbox_min if bbox_min is not None else torch.zeros(3))
        self.register_buffer("bbox_max", bbox_max if bbox_max is not None else torch.ones(3))
        self._tiny_hashes: dict[int, tcnn.Encoding] = {}
        self._key_trained = False

    # ---- normalization -------------------------------------------------
    def _normalize(self, pos: torch.Tensor) -> torch.Tensor:
        span = (self.bbox_max - self.bbox_min).clamp_min(1e-6)
        return ((pos - self.bbox_min) / span).clamp(0.0, 1.0)

    def new_tiny_hash(self, frame_idx: int) -> tcnn.Encoding:
        enc = tcnn.Encoding(n_input_dims=3, encoding_config=self.config.tiny_hash).to(self.big_hash.params.device)
        self._tiny_hashes[frame_idx] = enc
        return enc

    def drop_tiny_hash(self, frame_idx: int):
        self._tiny_hashes.pop(frame_idx, None)

    # ---- forward ---------------------------------------------------------
    def _decode(self, pos_embedding: torch.Tensor, dirs: torch.Tensor) -> torch.Tensor:
        dir_emb = self.dir_enc(dirs)
        feat = torch.cat([pos_embedding.float(), dir_emb.float()], dim=1)
        return self.mlp(feat).float()

    def forward_key(self, pos: torch.Tensor, dirs: torch.Tensor) -> torch.Tensor:
        pos_n = self._normalize(pos)
        emb = self.big_hash(pos_n)
        return self._decode(emb, dirs)

    def forward_residual(self, pos: torch.Tensor, dirs: torch.Tensor, frame_idx: int) -> torch.Tensor:
        pos_n = self._normalize(pos)
        big_emb = self.big_hash(pos_n).float()
        tiny_emb = self._tiny_hashes[frame_idx](pos_n).float()
        return self._decode(big_emb + tiny_emb, dirs)

    # ---- training (point-wise color distillation) ------------------------
    @staticmethod
    def _random_dirs(n: int, device) -> torch.Tensor:
        d = torch.randn(n, 3, device=device)
        return F.normalize(d, dim=-1)

    def _sh_target(self, features: torch.Tensor, deg: int, dirs: torch.Tensor) -> torch.Tensor:
        """The colour the rasterizer would produce for these Gaussians seen
        from `dirs` — SH evaluated at that direction, plus the 0.5 offset the
        3DGS convention carries (see `vega.sh.sh0_to_rgb`)."""
        return (eval_sh(deg, features, dirs) + 0.5).clamp(0.0, 1.0)

    def train_key(self, gaussians: GaussianSet, n_iters: int = 300, lr: float = 1e-2,
                  batch_size: int | None = 8192) -> list[float]:
        pos = gaussians.get_xyz.detach()
        features = gaussians.get_features.detach()
        deg = gaussians.sh_degree
        n = pos.shape[0]

        for p in self.big_hash.parameters():
            p.requires_grad_(True)
        for p in self.mlp.parameters():
            p.requires_grad_(True)
        opt = torch.optim.Adam(list(self.big_hash.parameters()) + list(self.mlp.parameters()), lr=lr)

        losses = []
        for _ in range(n_iters):
            if batch_size and batch_size < n:
                idx = torch.randint(0, n, (batch_size,), device=pos.device)
                pos_b, feat_b = pos[idx], features[idx]
            else:
                pos_b, feat_b = pos, features
            dirs_b = self._random_dirs(pos_b.shape[0], pos_b.device)
            target_b = self._sh_target(feat_b, deg, dirs_b)
            opt.zero_grad(set_to_none=True)
            pred = self.forward_key(pos_b, dirs_b)
            loss = F.mse_loss(pred, target_b)
            loss.backward()
            opt.step()
            losses.append(loss.item())

        self._key_trained = True
        for p in self.big_hash.parameters():
            p.requires_grad_(False)
        # The MLP is frozen here too, and stays frozen for every residual
        # frame in the GOV. This is required for correctness, not just
        # fidelity to §5.2: exactly one MLP is transmitted per GOV, shared by
        # every frame. If residual training kept nudging it, each frame would
        # be trained against a *different* MLP state than the single one the
        # client eventually receives, so every frame except the last would
        # decode against a model that had since drifted away from it.
        for p in self.mlp.parameters():
            p.requires_grad_(False)
        return losses

    def train_residual(self, gaussians: GaussianSet, frame_idx: int, n_iters: int = 150,
                        lr: float = 1e-2, batch_size: int | None = 8192) -> list[float]:
        """Train only this frame's tiny hash; the big hash and MLP stay fixed.

        (An earlier version also fine-tuned the shared MLP at a reduced
        learning rate, to dodge a dead-gradient problem seen when the decoder
        was tcnn's fp16 FullyFusedMLP driven by a full-image rendering loss.
        Both of those have since been replaced — float32 torch MLP,
        point-wise color regression — and the dead-gradient issue went with
        them, while the MLP drift it introduced was corrupting every frame
        but the last. Measured on 10 frames of `basketball`: per-frame
        decoded-color PSNR ramped 17->30 dB with a drifting MLP, versus a
        flat ~25-27 dB across residual frames once frozen.)
        """
        assert self._key_trained, "train_key() must run before any residual frame"
        pos = gaussians.get_xyz.detach()
        features = gaussians.get_features.detach()
        deg = gaussians.sh_degree
        n = pos.shape[0]

        tiny = self.new_tiny_hash(frame_idx)
        opt = torch.optim.Adam(tiny.parameters(), lr=lr)

        losses = []
        for _ in range(n_iters):
            if batch_size and batch_size < n:
                idx = torch.randint(0, n, (batch_size,), device=pos.device)
                pos_b, feat_b = pos[idx], features[idx]
            else:
                pos_b, feat_b = pos, features
            dirs_b = self._random_dirs(pos_b.shape[0], pos_b.device)
            target_b = self._sh_target(feat_b, deg, dirs_b)
            opt.zero_grad(set_to_none=True)
            pred = self.forward_residual(pos_b, dirs_b, frame_idx)
            loss = F.mse_loss(pred, target_b)
            loss.backward()
            opt.step()
            losses.append(loss.item())
        return losses

    # ---- size accounting ---------------------------------------------
    def big_hash_bytes(self) -> int:
        return self.big_hash.params.numel() * self.big_hash.params.element_size()

    def mlp_bytes(self) -> int:
        # fp16-quantized for the on-wire/on-device footprint (matches how
        # the paper reports MLP size, and how it would actually be shipped
        # to a mobile client) even though training keeps it in fp32.
        return sum(p.numel() for p in self.mlp.parameters()) * 2

    def tiny_hash_bytes(self, frame_idx: int) -> int:
        t = self._tiny_hashes[frame_idx]
        return t.params.numel() * t.params.element_size()
