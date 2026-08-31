"""Loader for the ORBIT multi-camera RGBD capture rig.

Dataset layout (see `<level_dir>/manifest.json`):
- 4 fixed, pre-calibrated cameras (known intrinsics + camera_to_world), so no
  COLMAP/SfM step is needed.
- Per camera, per frame: an RGB PNG, a depth PNG, and a fused point cloud
  (`positions` in *camera* space, `colors` uint8) as an .npz.

This loader fuses the 4 cameras' per-frame point clouds into one world-space
point cloud (using the manifest's `camera_to_world`), turns that into an
initial `GaussianSet` (position from depth, color from the RGB point cloud,
scale from local point density, identity rotation, constant opacity — this
is a direct point-to-Gaussian initialization rather than a from-scratch SfM
+ densification training, since the fused RGBD point cloud is already dense
and geometrically accurate), and returns the 4 real calibrated cameras plus
the real captured RGB images to use as supervision for hierarchical color
encoding.

Vega's own contributions (segmentation, GOV structure, hierarchical color
encoding, dynamicity filtering, rendering pipeline) all operate downstream
of this loader, unmodified from the synthetic-data path.
"""
from __future__ import annotations

import json
import math
from pathlib import Path

import numpy as np
import torch
from PIL import Image

from vega.cameras import Camera
from vega.gaussians import GaussianSet
from vega.sh import rgb_to_sh0


def load_manifest(level_dir: Path) -> dict:
    with open(Path(level_dir) / "manifest.json") as f:
        return json.load(f)


def _object_meta(manifest: dict, scene_name: str) -> dict:
    for obj in manifest["objects"]:
        if obj["name"] == scene_name:
            return obj
    raise KeyError(f"scene {scene_name!r} not found; available: "
                   f"{[o['name'] for o in manifest['objects']]}")


def build_camera(cam_meta: dict, image_scale: float, znear: float = 0.05,
                  zfar: float = 10.0, device: str = "cuda") -> Camera:
    w_native, h_native = cam_meta["width"], cam_meta["height"]
    fx, fy = cam_meta["intrinsics"]["fx"], cam_meta["intrinsics"]["fy"]
    fovx = 2 * math.atan(w_native / (2 * fx))
    fovy = 2 * math.atan(h_native / (2 * fy))

    M = np.array(cam_meta["camera_to_world"], dtype=np.float64)
    R = M[:3, :3].astype(np.float32)      # camera-to-world rotation
    C = M[:3, 3].astype(np.float32)       # camera center in world space
    T = (-R.T @ C).astype(np.float32)     # world-to-camera translation

    w = max(1, int(round(w_native * image_scale)))
    h = max(1, int(round(h_native * image_scale)))
    return Camera(R=R, T=T, fovx=fovx, fovy=fovy, width=w, height=h,
                  znear=znear, zfar=zfar, device=device)


def _load_rgb(path: Path, image_scale: float, device: str) -> torch.Tensor:
    img = Image.open(path).convert("RGB")
    if image_scale != 1.0:
        w = max(1, int(round(img.width * image_scale)))
        h = max(1, int(round(img.height * image_scale)))
        img = img.resize((w, h), Image.BILINEAR)
    arr = torch.from_numpy(np.array(img)).float().permute(2, 0, 1) / 255.0
    return arr.to(device)


def points_to_gaussians(xyz_np: np.ndarray, rgb_np: np.ndarray, bounds_min: np.ndarray,
                         bounds_max: np.ndarray, device: str, sh_degree: int = 2) -> GaussianSet:
    n = xyz_np.shape[0]
    xyz = torch.as_tensor(xyz_np, dtype=torch.float32, device=device)

    # Per-point scale from local density (mean squared distance to nearest
    # neighbors), the same initialization the original 3DGS codebase uses —
    # not a single scene-wide average spacing. A uniform scale derived from
    # the *whole* bounding volume (which includes a lot of empty room air)
    # made every Gaussian far too large in the actually-dense regions (the
    # person, the ball), which is what made renders look like a handful of
    # giant blobs instead of a recognizable shape.
    try:
        if not xyz.is_cuda:
            raise RuntimeError("simple_knn's distCUDA2 only accepts CUDA tensors")
        from simple_knn._C import distCUDA2
        dist2 = torch.clamp_min(distCUDA2(xyz), 1e-8)
        scale = torch.sqrt(dist2).clamp_min(1e-4)
        scale_raw = torch.log(scale).unsqueeze(1).repeat(1, 3)
    except (ImportError, RuntimeError):
        # No simple_knn built, or a CPU tensor (tests): fall back to a single
        # scene-wide spacing derived from the bounding volume.
        volume = float(np.prod(np.maximum(bounds_max - bounds_min, 1e-3)))
        avg_spacing = (volume / max(n, 1)) ** (1.0 / 3.0)
        scale = max(avg_spacing * 1.2, 1e-3)
        scale_raw = torch.full((n, 3), math.log(scale), device=device)
    rot_raw = torch.zeros(n, 4, device=device)
    rot_raw[:, 0] = 1.0
    opacity = 0.8
    opacity_raw = torch.full((n, 1), math.log(opacity / (1 - opacity)), device=device)

    rgb = torch.as_tensor(rgb_np, dtype=torch.float32, device=device).clamp(0.02, 0.98)
    sh_dc = rgb_to_sh0(rgb).unsqueeze(1)
    n_rest = (sh_degree + 1) ** 2 - 1
    sh_rest = torch.zeros(n, n_rest, 3, device=device)
    object_id = torch.zeros(n, dtype=torch.long, device=device)  # filled in by segmentation

    return GaussianSet(xyz=xyz, scale_raw=scale_raw, rot_raw=rot_raw, opacity_raw=opacity_raw,
                        sh_dc=sh_dc, sh_rest=sh_rest, object_id=object_id, sh_degree=sh_degree)


def load_scene(level_dir: str, scene_name: str, frame_indices: list[int], device: str = "cuda",
               max_points_per_frame: int = 60_000, image_scale: float = 0.25, seed: int = 0):
    """Load `frame_indices` (1-based, matching the on-disk `_{frame:06d}`
    numbering) of `scene_name` from `level_dir`.

    Returns:
        frames: list[GaussianSet] (object_id all zero — run segmentation next)
        cameras: list[Camera], the 4 fixed calibrated cameras (shared by all frames)
        gt_images: list[list[Tensor]], gt_images[t][cam] is a (3, H, W) real photo
        bounds_min, bounds_max: torch (3,) tensors, the scene's known bounding box
    """
    level_dir = Path(level_dir)
    manifest = load_manifest(level_dir)
    obj = _object_meta(manifest, scene_name)
    cams_meta = obj["cameras"]
    n_cams = len(cams_meta)

    cameras = [build_camera(cm, image_scale, device=device) for cm in cams_meta]
    bounds_min = np.array(obj["bounds_min"], dtype=np.float32)
    bounds_max = np.array(obj["bounds_max"], dtype=np.float32)

    frames, gt_images = [], []
    rng = np.random.default_rng(seed)
    for fidx in frame_indices:
        pts_list, col_list, imgs = [], [], []
        for cam_idx in range(n_cams):
            npz_path = level_dir / obj["pointcloud_pattern"].format(camera=cam_idx, frame=fidx)
            d = np.load(npz_path)
            pos_cam = d["positions"].astype(np.float32)
            col = d["colors"].astype(np.float32) / 255.0

            R_c2w = np.array(cams_meta[cam_idx]["camera_to_world"], dtype=np.float64)[:3, :3]
            C = np.array(cams_meta[cam_idx]["camera_to_world"], dtype=np.float64)[:3, 3]
            pos_world = (pos_cam.astype(np.float64) @ R_c2w.T + C).astype(np.float32)

            pts_list.append(pos_world)
            col_list.append(col)

            rgb_path = level_dir / obj["rgb_pattern"].format(camera=cam_idx, frame=fidx)
            imgs.append(_load_rgb(rgb_path, image_scale, device))

        all_pts = np.concatenate(pts_list, axis=0)
        all_cols = np.concatenate(col_list, axis=0)
        n = all_pts.shape[0]
        if n > max_points_per_frame:
            idx = rng.choice(n, size=max_points_per_frame, replace=False)
            all_pts, all_cols = all_pts[idx], all_cols[idx]

        gs = points_to_gaussians(all_pts, all_cols, bounds_min, bounds_max, device)
        frames.append(gs)
        gt_images.append(imgs)

    return (frames, cameras, gt_images,
            torch.as_tensor(bounds_min, device=device), torch.as_tensor(bounds_max, device=device))


def even_frame_indices(source_frame_count: int, n_frames: int, start: int = 1) -> list[int]:
    """Evenly subsample `n_frames` frame indices from [start, start+source_frame_count)."""
    if n_frames >= source_frame_count:
        return list(range(start, start + source_frame_count))
    idx = np.linspace(0, source_frame_count - 1, n_frames)
    return sorted(set(int(round(i)) + start for i in idx))
