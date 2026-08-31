"""Top-level Vega encoder (paper §4-5, Fig. 5 left half): wires together
segmentation, GOV planning, hierarchical color encoding, dynamicity
estimation, and non-color filtering into one `encode_sequence` call.

Ground truth for distortion (Eq. 6) and for dynamicity estimation (Eq. 1-2)
is obtained by rendering each frame's *own* known attributes (SH color +
geometry — whatever an upstream reconstruction, real or synthetic, already
produced) from a shared evaluation camera and treating that as the "captured
photo" the encoding is trying to reproduce. This keeps distortion/dynamicity
well-defined uniformly for both the synthetic scene and the ORBIT dataset,
independent of how many real calibrated cameras happen to be available.
"""
from __future__ import annotations

import dataclasses
import math

import torch

from vega.bitstream import FrameChunk, make_key_chunk, make_residual_chunk
from vega.cameras import Camera, look_at_RT
from vega.color_encoding import ColorEncodingConfig, DEFAULT_COLOR_CONFIG, HierarchicalColorModel
from vega.dynamicity import estimate_dynamicity
from vega.filtering import apply_filtering, plan_filtering
from vega.gaussians import GaussianSet
from vega.gov import DEFAULT_LAMBDA, plan_and_encode
from vega.metrics import sse
from vega.rasterize import render, view_directions


def default_eval_camera(bbox_min: torch.Tensor, bbox_max: torch.Tensor, device: str,
                         width: int = 256, height: int = 256, fov_deg: float = 55.0) -> Camera:
    center = ((bbox_min + bbox_max) / 2).detach().cpu().numpy()
    extent = (bbox_max - bbox_min).detach().cpu().numpy()
    radius = float(max(extent.max() * 1.6, 1e-2))
    import numpy as np
    eye = center + np.array([radius, radius * 0.3, radius])
    R, T = look_at_RT(eye.astype("float32"), center.astype("float32"))
    fov = math.radians(fov_deg)
    return Camera(R=R, T=T, fovx=fov, fovy=fov, width=width, height=height, device=device)


@dataclasses.dataclass
class EncodeResult:
    chunks: list[FrameChunk]
    color_model: HierarchicalColorModel
    group_ids: list[int]
    frame_costs: list
    dynamicity_log: list[dict]
    reconstructed: list[GaussianSet]   # what a client would actually render, per frame
    psnr_db: list[float]


@dataclasses.dataclass
class VegaEncoderConfig:
    color_config: ColorEncodingConfig = dataclasses.field(default_factory=lambda: DEFAULT_COLOR_CONFIG)
    dyn_threshold: float = 0.5
    gov_lambda: float = DEFAULT_LAMBDA
    gov_window: int = 5
    gov_max_group_len: int | None = None  # hard cap on GOV length; None = pure RD
    key_iters: int = 300
    key_lr: float = 1e-2
    residual_iters: int = 150
    residual_lr: float = 1e-2
    dyn_iters: int = 20
    dyn_lr: float = 5e-3


def encode_sequence(frames: list[GaussianSet], bbox_min: torch.Tensor, bbox_max: torch.Tensor,
                     eval_camera: Camera | None = None, config: VegaEncoderConfig = None) -> EncodeResult:
    from vega.metrics import psnr as psnr_fn

    config = config or VegaEncoderConfig()
    device = frames[0].get_xyz.device
    bg = torch.zeros(3, device=device)
    eval_cam = eval_camera or default_eval_camera(bbox_min, bbox_max, device)

    color_model = HierarchicalColorModel(config.color_config, bbox_min=bbox_min, bbox_max=bbox_max).to(device)

    group_counter = [-1]
    key_gs_by_group: dict[int, GaussianSet] = {}
    dynamicity_log: list[dict] = []
    reconstructed: list[GaussianSet] = []
    psnr_log: list[float] = []

    def render_oracle(gs: GaussianSet) -> torch.Tensor:
        # Detached: this stands in for a fixed "captured photo" (ground
        # truth), reused as a target across many training iterations —
        # it must not carry its own autograd graph (the rasterizer always
        # builds one via the screenspace-points trick, which would otherwise
        # only survive a single `.backward()` call).
        with torch.no_grad():
            return render(eval_cam, gs, bg)["render"].clone()

    def render_with_model(gs: GaussianSet, color_fn) -> torch.Tensor:
        with torch.no_grad():
            dirs = view_directions(eval_cam, gs.get_xyz)
            colors = color_fn(gs.get_xyz, dirs)
            return render(eval_cam, gs, bg, colors_override=colors)["render"]

    def encode_key(i: int):
        gs = frames[i]
        color_model.train_key(gs, n_iters=config.key_iters, lr=config.key_lr)
        group_counter[0] += 1
        gid = group_counter[0]
        key_gs_by_group[gid] = gs

        gt_img = render_oracle(gs)
        pred_img = render_with_model(gs, color_model.forward_key)
        distortion = sse(pred_img, gt_img).item()
        chunk = make_key_chunk(i, gid, gs, color_model)

        dynamicity_log.append({"frame_idx": i, "frame_type": "key", "group_id": gid})
        reconstructed.append(gs)
        psnr_log.append(psnr_fn(pred_img, gt_img).item())
        return float(chunk.total_bytes), distortion, chunk

    def encode_residual(i: int):
        gid = group_counter[0]
        key_gs = key_gs_by_group[gid]
        true_gs = frames[i]

        gt_img = render_oracle(true_gs)
        working_gs = key_gs.detach()
        dyn_result = estimate_dynamicity(working_gs, [eval_cam], [gt_img], bg,
                                          n_iters=config.dyn_iters, lr=config.dyn_lr,
                                          threshold=config.dyn_threshold)
        plan = plan_filtering(true_gs.object_ids().tolist(), dyn_result.dynamic_objects)
        recon_gs = apply_filtering(true_gs, key_gs, plan)

        color_model.train_residual(true_gs, frame_idx=i, n_iters=config.residual_iters, lr=config.residual_lr)
        pred_img = render_with_model(recon_gs, lambda pos, dirs: color_model.forward_residual(pos, dirs, i))
        distortion = sse(pred_img, gt_img).item()
        chunk = make_residual_chunk(i, gid, true_gs, plan, color_model)

        dynamicity_log.append({
            "frame_idx": i, "frame_type": "residual", "group_id": gid,
            "per_object_dyn": dyn_result.per_object_dyn,
            "dynamic_objects": sorted(dyn_result.dynamic_objects),
        })
        reconstructed.append(recon_gs)
        psnr_log.append(psnr_fn(pred_img, gt_img).item())
        return float(chunk.total_bytes), distortion, chunk

    frame_costs, chunks, group_ids = plan_and_encode(
        len(frames), encode_key, encode_residual, lam=config.gov_lambda,
        window_size=config.gov_window, max_group_len=config.gov_max_group_len)

    return EncodeResult(
        chunks=chunks, color_model=color_model, group_ids=group_ids,
        frame_costs=frame_costs, dynamicity_log=dynamicity_log,
        reconstructed=reconstructed, psnr_db=psnr_log,
    )
