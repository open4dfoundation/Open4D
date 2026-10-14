"""Serialization of an encoded Vega GOV sequence to disk (paper §7: "the
server stores the video in GOV units and transmits video chunks to the
client via HTTP").

Layout on disk (one directory per encoded sequence):

    color_model.pt        shared big hash + MLP + bbox (one per GOV; for
                           simplicity in this prototype we use a single GOV
                           spanning the whole encoded sequence — see
                           `vega.encoder`)
    topology.pt            per-Gaussian object_id (assumed fixed across the
                           sequence — see `vega.filtering` docstring)
    frame_0000.pt, ...     one chunk per frame: frame_type, group_id,
                           transmitted non-color attributes, tiny hash
                           weights (residual only), and size accounting
    manifest.json          per-frame metadata + sizes (for the HTTP chunk
                           server / the offline eval scripts)
"""
from __future__ import annotations

import dataclasses
import json
from pathlib import Path

import torch

from vega.color_encoding import HierarchicalColorModel
from vega.filtering import FilterPlan
from vega.gaussians import GaussianSet


@dataclasses.dataclass
class FrameChunk:
    frame_idx: int
    frame_type: str          # "key" | "residual"
    group_id: int
    transmitted_objects: list[int]
    reused_objects: list[int]
    color_bytes: int
    non_color_bytes: int
    non_color: dict          # {"xyz":..., "scale_raw":..., "rot_raw":..., "opacity_raw":..., "object_id":...} for transmitted gaussians only
    tiny_hash_state: dict | None  # tcnn Encoding state_dict, residual frames only

    @property
    def total_bytes(self) -> int:
        return self.color_bytes + self.non_color_bytes


def make_key_chunk(frame_idx: int, group_id: int, gaussians: GaussianSet,
                    color_model: HierarchicalColorModel) -> FrameChunk:
    all_objs = gaussians.object_ids().tolist()
    non_color = {
        "xyz": gaussians.xyz.detach().cpu(),
        "scale_raw": gaussians.scale_raw.detach().cpu(),
        "rot_raw": gaussians.rot_raw.detach().cpu(),
        "opacity_raw": gaussians.opacity_raw.detach().cpu(),
        "object_id": gaussians.object_id.detach().cpu(),
    }
    from vega.filtering import non_color_bytes_per_gaussian
    non_color_bytes = len(gaussians) * non_color_bytes_per_gaussian()
    color_bytes = color_model.big_hash_bytes() + color_model.mlp_bytes()
    return FrameChunk(frame_idx, "key", group_id, all_objs, [], color_bytes,
                       non_color_bytes, non_color, None)


def make_residual_chunk(frame_idx: int, group_id: int, gaussians: GaussianSet,
                         plan: FilterPlan, color_model: HierarchicalColorModel) -> FrameChunk:
    mask = torch.zeros(len(gaussians), dtype=torch.bool, device=gaussians.xyz.device)
    for oid in plan.transmitted_objects:
        mask |= gaussians.object_mask(oid)
    non_color = {
        "xyz": gaussians.xyz[mask].detach().cpu(),
        "scale_raw": gaussians.scale_raw[mask].detach().cpu(),
        "rot_raw": gaussians.rot_raw[mask].detach().cpu(),
        "opacity_raw": gaussians.opacity_raw[mask].detach().cpu(),
        "object_id": gaussians.object_id[mask].detach().cpu(),
    }
    from vega.filtering import non_color_bytes_per_gaussian
    non_color_bytes = int(mask.sum().item()) * non_color_bytes_per_gaussian()
    color_bytes = color_model.tiny_hash_bytes(frame_idx)
    tiny_state = {k: v.detach().cpu() for k, v in
                  color_model._tiny_hashes[frame_idx].state_dict().items()}
    return FrameChunk(frame_idx, "residual", group_id, sorted(plan.transmitted_objects),
                       sorted(plan.reused_objects), color_bytes, non_color_bytes,
                       non_color, tiny_state)


def write_bitstream(outdir: str, color_model: HierarchicalColorModel, chunks: list[FrameChunk]):
    outdir = Path(outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    torch.save({
        "state_dict": color_model.state_dict(),
        "config": color_model.config,
    }, outdir / "color_model.pt")

    manifest = {"frames": []}
    for c in chunks:
        fname = f"frame_{c.frame_idx:04d}.pt"
        torch.save({
            "frame_idx": c.frame_idx, "frame_type": c.frame_type, "group_id": c.group_id,
            "transmitted_objects": c.transmitted_objects, "reused_objects": c.reused_objects,
            "non_color": c.non_color, "tiny_hash_state": c.tiny_hash_state,
        }, outdir / fname)
        manifest["frames"].append({
            "frame_idx": c.frame_idx, "frame_type": c.frame_type, "group_id": c.group_id,
            "file": fname, "color_bytes": c.color_bytes, "non_color_bytes": c.non_color_bytes,
            "total_bytes": c.total_bytes,
        })
    with open(outdir / "manifest.json", "w") as f:
        json.dump(manifest, f, indent=2)
    return outdir / "manifest.json"


def load_manifest(outdir: str) -> dict:
    with open(Path(outdir) / "manifest.json") as f:
        return json.load(f)


def load_frame_chunk(outdir: str, frame_idx: int) -> dict:
    return torch.load(Path(outdir) / f"frame_{frame_idx:04d}.pt", weights_only=False)
