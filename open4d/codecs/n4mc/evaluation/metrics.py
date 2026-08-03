from __future__ import annotations

import math

import numpy as np
import torch
import trimesh
from skimage import measure


def _to_numpy_volume(volume: torch.Tensor | np.ndarray) -> np.ndarray:
    if isinstance(volume, torch.Tensor):
        array = volume.detach().cpu().numpy()
    else:
        array = np.asarray(volume)
    if array.ndim == 4 and array.shape[0] == 1:
        return array[0]
    if array.ndim == 3:
        return array
    raise ValueError(f"Expected TSDF volume with shape (1, D, H, W) or (D, H, W), got {array.shape}.")


def compute_voxel_metrics(
    prediction: torch.Tensor,
    target: torch.Tensor,
    narrow_band_threshold: float,
) -> dict[str, float]:
    pred = prediction.detach().cpu()
    gt = target.detach().cpu()
    error = pred - gt
    mae = float(torch.mean(torch.abs(error)).item())
    mse = float(torch.mean(error.square()).item())
    mask = (gt.abs() <= narrow_band_threshold).float()
    mask_sum = float(mask.sum().item())
    if mask_sum > 0:
        narrow_band_mae = float((mask * error.abs()).sum().item() / mask_sum)
    else:
        narrow_band_mae = 0.0
    sign_accuracy = float((torch.sign(pred) == torch.sign(gt)).float().mean().item())
    psnr = float(-10.0 * math.log10(max(mse, 1e-12)))
    return {
        "mae": mae,
        "mse": mse,
        "psnr": psnr,
        "narrow_band_mae": narrow_band_mae,
        "sign_accuracy": sign_accuracy,
    }


def compute_compression_metrics(
    total_bits: float,
    volume_shape: tuple[int, ...] | torch.Size,
    raw_bits_per_value: int,
) -> dict[str, float]:
    shape = tuple(int(v) for v in volume_shape)
    num_voxels = int(np.prod(shape[1:])) if len(shape) == 4 else int(np.prod(shape))
    raw_bits = float(num_voxels * raw_bits_per_value)
    return {
        "bits_per_volume": float(total_bits),
        "bits_per_voxel": float(total_bits / max(num_voxels, 1)),
        "compression_ratio": float(raw_bits / max(total_bits, 1e-9)),
    }


def reconstruct_mesh_from_tsdf(volume: torch.Tensor | np.ndarray) -> trimesh.Trimesh | None:
    tsdf = _to_numpy_volume(volume)
    if tsdf.min() > 0.0 or tsdf.max() < 0.0:
        return None
    try:
        spacing = tuple(2.0 / max(dim - 1, 1) for dim in tsdf.shape)
        vertices, faces, normals, _ = measure.marching_cubes(tsdf, level=0.0, spacing=spacing)
    except ValueError:
        return None

    vertices -= 1.0
    mesh = trimesh.Trimesh(vertices=vertices, faces=faces, vertex_normals=normals, process=False)
    if mesh.vertices.shape[0] == 0 or mesh.faces.shape[0] == 0:
        return None
    return mesh


def _nearest_squared_distances(points_a: np.ndarray, points_b: np.ndarray) -> np.ndarray:
    diffs = points_a[:, None, :] - points_b[None, :, :]
    squared = np.sum(diffs * diffs, axis=-1)
    return np.min(squared, axis=1)


def compute_chamfer_distance(pred_mesh: trimesh.Trimesh, gt_mesh: trimesh.Trimesh, num_samples: int) -> float:
    pred_points, _ = trimesh.sample.sample_surface(pred_mesh, num_samples)
    gt_points, _ = trimesh.sample.sample_surface(gt_mesh, num_samples)
    pred_to_gt = _nearest_squared_distances(pred_points.astype(np.float32), gt_points.astype(np.float32))
    gt_to_pred = _nearest_squared_distances(gt_points.astype(np.float32), pred_points.astype(np.float32))
    return float(pred_to_gt.mean() + gt_to_pred.mean())


def compute_normal_consistency(pred_mesh: trimesh.Trimesh, gt_mesh: trimesh.Trimesh, num_samples: int) -> float:
    pred_points, pred_faces = trimesh.sample.sample_surface(pred_mesh, num_samples)
    gt_points, gt_faces = trimesh.sample.sample_surface(gt_mesh, num_samples)
    pred_normals = pred_mesh.face_normals[pred_faces]
    gt_normals = gt_mesh.face_normals[gt_faces]

    distances = np.sum((pred_points[:, None, :] - gt_points[None, :, :]) ** 2, axis=-1)
    pred_match = gt_normals[np.argmin(distances, axis=1)]
    gt_match = pred_normals[np.argmin(distances, axis=0)]

    pred_consistency = np.abs(np.sum(pred_normals * pred_match, axis=1)).mean()
    gt_consistency = np.abs(np.sum(gt_normals * gt_match, axis=1)).mean()
    return float(0.5 * (pred_consistency + gt_consistency))


def compute_mesh_metrics(
    pred_mesh: trimesh.Trimesh | None,
    gt_mesh: trimesh.Trimesh | None,
    num_surface_samples: int,
) -> dict[str, float]:
    if pred_mesh is None or gt_mesh is None:
        return {
            "chamfer_distance": float("nan"),
            "normal_consistency": float("nan"),
            "pred_mesh_vertices": 0.0,
            "gt_mesh_vertices": 0.0,
        }
    return {
        "chamfer_distance": compute_chamfer_distance(pred_mesh, gt_mesh, num_surface_samples),
        "normal_consistency": compute_normal_consistency(pred_mesh, gt_mesh, num_surface_samples),
        "pred_mesh_vertices": float(len(pred_mesh.vertices)),
        "gt_mesh_vertices": float(len(gt_mesh.vertices)),
    }
