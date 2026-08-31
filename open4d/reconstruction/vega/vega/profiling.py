"""Real per-task latency profiling for the priority-based task scheduler
(paper §6.3, Eq. 8-9).

There is no mobile device in this environment, so `T_CPU(O)` / `T_GPU(O)`
cannot be profiled on an actual phone SoC as the paper does (Galaxy
S24/S25). Instead, this module *actually measures* the four pipeline tasks
(hash lookup, MLP inference, sort, final render) on this workstation's own
CPU and GPU across a range of Gaussian counts, and fits a linear latency
model `T(n) = a*n + b` per (task, processor) — the same functional form the
paper assumes ("these values are proportional to the number of Gaussians in
an object, they can be pre-modeled through profiling on mobile devices",
§6.3). Swap this profiling step for on-device numbers later; the scheduler
in `vega.scheduler` only consumes the fitted model, not this module.

Hash lookups only have a CUDA implementation available here (`tinycudann`),
so a CPU hash-grid is reimplemented directly in torch (same multiresolution
hash-and-interpolate structure, executed eagerly on CPU tensors) purely to
get a genuine wall-clock CPU number for this task — it does not need to be
bit-exact with tinycudann's own hashing, only representative of the same
gather + trilinear-interpolate workload.
"""
from __future__ import annotations

import dataclasses
import itertools
import time

import numpy as np
import torch

from vega.cameras import Camera
from vega.color_encoding import ColorEncodingConfig, DEFAULT_COLOR_CONFIG, HierarchicalColorModel
from vega.gaussians import GaussianSet
from vega.rasterize import render


_CORNERS = list(itertools.product([0, 1], repeat=3))
_PRIMES = (1, 2654435761, 805459861)


def hash_lookup_cpu(positions_01: torch.Tensor, table: torch.Tensor, n_levels: int,
                     base_resolution: int, per_level_scale: float) -> torch.Tensor:
    """A CPU multiresolution hash-grid lookup (gather + trilinear interpolate
    per level), structurally equivalent to tinycudann's HashGrid, used only
    to get a genuine CPU latency measurement for this task."""
    outs = []
    table_size = table.shape[0]
    for lvl in range(n_levels):
        res = max(2, int(base_resolution * (per_level_scale ** lvl)))
        scaled = positions_01 * res
        floor = torch.floor(scaled)
        frac = scaled - floor
        acc = torch.zeros(positions_01.shape[0], table.shape[1])
        for corner in _CORNERS:
            offset = torch.tensor(corner, dtype=torch.float32)
            coord = (floor + offset).long()
            h = (coord[:, 0] * _PRIMES[0]) ^ (coord[:, 1] * _PRIMES[1]) ^ (coord[:, 2] * _PRIMES[2])
            idx = (h % table_size).abs()
            w = torch.where(offset.bool(), frac, 1 - frac).prod(dim=1, keepdim=True)
            acc = acc + w * table[idx]
        outs.append(acc)
    return torch.cat(outs, dim=1)


def _time_gpu(fn, n_repeats: int = 30) -> float:
    torch.cuda.synchronize()
    start = torch.cuda.Event(enable_timing=True)
    end = torch.cuda.Event(enable_timing=True)
    for _ in range(3):
        fn()
    torch.cuda.synchronize()
    start.record()
    for _ in range(n_repeats):
        fn()
    end.record()
    torch.cuda.synchronize()
    return start.elapsed_time(end) / n_repeats  # ms


def _time_cpu(fn, n_repeats: int = 10) -> float:
    for _ in range(2):
        fn()
    t0 = time.perf_counter()
    for _ in range(n_repeats):
        fn()
    return (time.perf_counter() - t0) * 1000.0 / n_repeats  # ms


@dataclasses.dataclass
class FittedLatency:
    slope_ms: float   # ms per Gaussian
    intercept_ms: float

    def __call__(self, n: int) -> float:
        return max(self.intercept_ms + self.slope_ms * n, 0.0)


def _fit_linear(ns: list[int], ts_ms: list[float]) -> FittedLatency:
    a, b = np.polyfit(ns, ts_ms, 1)
    return FittedLatency(slope_ms=float(a), intercept_ms=max(float(b), 0.0))


@dataclasses.dataclass
class LatencyModel:
    hash_cpu: FittedLatency
    hash_gpu: FittedLatency
    mlp_gpu: FittedLatency
    sort_cpu: FittedLatency
    sort_gpu: FittedLatency
    render_gpu: FittedLatency  # per-Gaussian marginal cost of the final render pass


def profile_latency_model(camera: Camera, config: ColorEncodingConfig = DEFAULT_COLOR_CONFIG,
                           sizes: list[int] = (1000, 5000, 10000, 20000, 50000),
                           device: str = "cuda") -> LatencyModel:
    sizes = list(sizes)
    color_model = HierarchicalColorModel(config).to(device)
    big_hash = color_model.big_hash
    mlp = color_model.mlp
    cpu_table = torch.randn(2 ** config.big_hash["log2_hashmap_size"], config.big_hash["n_features_per_level"])

    hash_cpu_ts, hash_gpu_ts, mlp_gpu_ts, sort_cpu_ts, sort_gpu_ts, render_ts = [], [], [], [], [], []

    for n in sizes:
        pos_gpu = torch.rand(n, 3, device=device)
        pos_cpu = torch.rand(n, 3)

        hash_gpu_ts.append(_time_gpu(lambda: big_hash(pos_gpu)))
        hash_cpu_ts.append(_time_cpu(lambda: hash_lookup_cpu(
            pos_cpu, cpu_table, config.big_hash["n_levels"],
            config.big_hash["base_resolution"], config.big_hash["per_level_scale"])))

        feat_gpu = torch.rand(n, mlp[0].in_features, device=device)
        mlp_gpu_ts.append(_time_gpu(lambda: mlp(feat_gpu)))

        keys_gpu = torch.rand(n, device=device)
        sort_gpu_ts.append(_time_gpu(lambda: torch.sort(keys_gpu)))
        keys_cpu = torch.rand(n)
        sort_cpu_ts.append(_time_cpu(lambda: torch.sort(keys_cpu)))

        gs = _random_gaussians(n, device)
        bg = torch.zeros(3, device=device)
        render_ts.append(_time_gpu(lambda: render(camera, gs, bg)))

    return LatencyModel(
        hash_cpu=_fit_linear(sizes, hash_cpu_ts),
        hash_gpu=_fit_linear(sizes, hash_gpu_ts),
        mlp_gpu=_fit_linear(sizes, mlp_gpu_ts),
        sort_cpu=_fit_linear(sizes, sort_cpu_ts),
        sort_gpu=_fit_linear(sizes, sort_gpu_ts),
        render_gpu=_fit_linear(sizes, render_ts),
    )


def _random_gaussians(n: int, device: str, sh_degree: int = 2) -> GaussianSet:
    xyz = (torch.rand(n, 3, device=device) - 0.5) * 2.0
    scale_raw = torch.full((n, 3), -3.5, device=device)
    rot_raw = torch.zeros(n, 4, device=device)
    rot_raw[:, 0] = 1.0
    opacity_raw = torch.full((n, 1), 1.5, device=device)
    sh_dc = torch.rand(n, 1, 3, device=device) * 0.5
    n_rest = (sh_degree + 1) ** 2 - 1
    sh_rest = torch.zeros(n, n_rest, 3, device=device)
    object_id = torch.zeros(n, dtype=torch.long, device=device)
    return GaussianSet(xyz=xyz, scale_raw=scale_raw, rot_raw=rot_raw, opacity_raw=opacity_raw,
                        sh_dc=sh_dc, sh_rest=sh_rest, object_id=object_id, sh_degree=sh_degree)
