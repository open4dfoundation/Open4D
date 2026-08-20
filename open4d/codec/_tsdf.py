"""Canonical triangle-sequence to TSDF preparation for volume codecs."""

from __future__ import annotations

from importlib import import_module
from pathlib import Path

import numpy as np

from open4d.core import Sequence

from ._protocol import CodecError


def _backend():
    try:
        return import_module("point_cloud_utils")
    except ImportError as error:
        raise CodecError("TSDF codecs need point-cloud-utils; install open4d[klt]") from error


def _outside_positive(sdf: np.ndarray) -> np.ndarray:
    boundary = np.concatenate((
        sdf[0].ravel(), sdf[-1].ravel(), sdf[:, 0].ravel(), sdf[:, -1].ravel(),
        sdf[:, :, 0].ravel(), sdf[:, :, -1].ravel(),
    ))
    return -sdf if np.median(boundary) < 0 else sdf


def write_tsdf_sequence(
    sequence: Sequence, destination: Path, *, resolution: int, truncation_voxels: float = 3
) -> dict:
    """Voxelize every canonical mesh in one shared normalized coordinate system."""
    if not len(sequence):
        raise CodecError("a TSDF codec cannot encode an empty sequence")
    if resolution < 7:
        raise ValueError("resolution must be at least 7")
    lower, upper = np.full(3, np.inf), np.full(3, -np.inf)
    for frame in sequence:
        if not len(frame.geometry.positions):
            raise CodecError("TSDF codecs cannot encode an empty mesh frame")
        lower = np.minimum(lower, frame.geometry.positions.min(0))
        upper = np.maximum(upper, frame.geometry.positions.max(0))
    extent = float(np.max(upper - lower))
    if not np.isfinite(extent) or extent <= 0:
        raise CodecError("TSDF codecs need a sequence with non-zero spatial extent")
    center, scale = (lower + upper) / 2, 2 / extent

    axis = np.linspace(-1.1, 1.1, resolution + 1, dtype=np.float32)
    grid = np.stack(np.meshgrid(axis, axis, axis, indexing="ij"), axis=-1)
    points = grid.reshape(-1, 3)
    truncation = truncation_voxels * 2.2 / resolution
    destination.mkdir(parents=True, exist_ok=True)
    pcu = _backend()
    for ordinal, frame in enumerate(sequence):
        mesh = frame.geometry
        positions = np.asarray((mesh.positions - center) * scale, dtype=np.float32)
        triangles = np.asarray(mesh.triangles, dtype=np.int32)
        sdf, _, _ = pcu.signed_distance_to_mesh(points, positions, triangles)
        sdf = _outside_positive(sdf.reshape(grid.shape[:3]))
        sdf = np.clip(sdf / truncation, -1, 1).astype(np.float32)
        np.savez_compressed(destination / f"{ordinal:06d}.npz", sdf=sdf[..., None])
    return {"center": center.tolist(), "scale": scale, "resolution": resolution}
