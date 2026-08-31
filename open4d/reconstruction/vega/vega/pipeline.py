"""View-adaptive rendering pipeline simulation — paper §6, Fig. 7.

Ties together object-level early culling (§6.2), priority-based task
scheduling (§6.3), hierarchical color decoding, and final rendering into a
per-frame simulation that tracks CPU / GPU / NPU time against a target
frame deadline, using the latency model measured in `vega.profiling`.

As documented at the top of the repo: the *decision logic* here (which
objects get culled, which get fully recomputed vs. reusing last frame's
result, which processor each is assigned to) is real and runs for real.
What's simulated is *time*: there's no phone to run the CPU/GPU/NPU tracks
on concurrently, so elapsed time per track is computed from the profiled
latency model rather than measured from actual concurrent execution.
"""
from __future__ import annotations

import dataclasses

import torch

from vega.cameras import Camera
from vega.color_encoding import HierarchicalColorModel
from vega.culling import early_cull_from_boxes
from vega.gaussians import GaussianSet
from vega.profiling import LatencyModel
from vega.rasterize import render, view_directions
from vega.scheduler import ObjectState, PriorityScheduler


@dataclasses.dataclass
class FrameResult:
    frame_idx: int
    image: torch.Tensor
    visible: list[int]
    culled: list[int]
    assignment: dict
    cpu_ms: float
    gpu_ms: float
    npu_ms: float
    frame_ms: float
    fps: float


class ViewAdaptiveRenderer:
    """Stateful across a playback session: tracks each object's "age" (frames
    since it was last fully recomputed) and caches its last-decoded colors,
    exactly like the "Previous Results" data flow in Fig. 7.
    """

    def __init__(self, color_model: HierarchicalColorModel, latency_model: LatencyModel,
                 t_deadline_ms: float = 1000.0 / 30.0, aging_coeff: float = 0.05):
        self.color_model = color_model
        self.latency_model = latency_model
        self.scheduler = PriorityScheduler(latency_model, t_deadline_ms, aging_coeff)
        self.t_deadline_ms = t_deadline_ms
        self.cache: dict[int, torch.Tensor] = {}
        self.age: dict[int, int] = {}

    def _decode(self, gaussians: GaussianSet, mask: torch.Tensor, camera: Camera,
                frame_idx: int, is_key: bool) -> torch.Tensor:
        pos = gaussians.get_xyz[mask]
        dirs = view_directions(camera, pos)
        with torch.no_grad():
            if is_key:
                return self.color_model.forward_key(pos, dirs)
            return self.color_model.forward_residual(pos, dirs, frame_idx)

    def render_frame(self, gaussians: GaussianSet, camera: Camera, frame_idx: int,
                      dyn_by_object: dict[int, float], is_key: bool,
                      bg: torch.Tensor) -> FrameResult:
        boxes = gaussians.object_bounding_boxes()
        visible_ids, culled_ids = early_cull_from_boxes(boxes, camera)

        for oid in gaussians.object_ids().tolist():
            self.age.setdefault(oid, 10_000)  # unseen objects force a full compute

        cam_center = camera.camera_center
        states = []
        for oid in visible_ids:
            bmin, bmax = boxes[oid]
            center = ((bmin + bmax) / 2).to(cam_center.device)
            dist = float((center - cam_center).norm().item())
            n = int(gaussians.object_mask(oid).sum().item())
            states.append(ObjectState(oid, n, dyn_by_object.get(oid, 0.0), dist, self.age[oid]))

        if is_key:
            assignment = {s.object_id: "gpu" for s in states}
        else:
            assignment = self.scheduler.schedule(states)

        device = gaussians.get_xyz.device
        colors_full = torch.zeros(len(gaussians), 3, device=device)
        t_cpu = t_gpu = t_npu = 0.0

        for oid in visible_ids:
            mask = gaussians.object_mask(oid)
            n = int(mask.sum().item())
            act = assignment.get(oid, "skip")
            if act == "skip" and oid in self.cache:
                colors_full[mask] = self.cache[oid]
                self.age[oid] += 1
                continue
            colors = self._decode(gaussians, mask, camera, frame_idx, is_key)
            colors_full[mask] = colors
            self.cache[oid] = colors.detach()
            self.age[oid] = 0
            if act == "cpu":
                t_cpu += self.latency_model.hash_cpu(n) + self.latency_model.sort_cpu(n)
            else:
                t_gpu += self.latency_model.hash_gpu(n) + self.latency_model.sort_gpu(n)
            t_npu += self.latency_model.mlp_gpu(n)  # NPU stand-in (§6.3: MLP always -> NPU)

        for oid in culled_ids:
            self.age[oid] = self.age.get(oid, 0) + 1

        vis_mask = torch.zeros(len(gaussians), dtype=torch.bool, device=device)
        for oid in visible_ids:
            vis_mask |= gaussians.object_mask(oid)
        sub_gaussians = gaussians.subset(vis_mask)
        sub_colors = colors_full[vis_mask]
        with torch.no_grad():
            out = render(camera, sub_gaussians, bg, colors_override=sub_colors)
        n_render = int(vis_mask.sum().item())
        t_gpu += self.latency_model.render_gpu(n_render)  # final render always GPU, after preprocessing

        frame_ms = max(t_cpu, t_gpu, t_npu)
        fps = 1000.0 / frame_ms if frame_ms > 0 else float("inf")

        return FrameResult(
            frame_idx=frame_idx, image=out["render"], visible=sorted(visible_ids),
            culled=sorted(culled_ids), assignment=assignment, cpu_ms=t_cpu, gpu_ms=t_gpu,
            npu_ms=t_npu, frame_ms=frame_ms, fps=fps,
        )
