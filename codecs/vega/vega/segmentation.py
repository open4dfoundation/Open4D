"""Object-level scene segmentation ("Gaussian Grouping", paper §5.1).

The paper segments a 3DGS scene into semantically meaningful objects using
Gaussian Grouping [48] (a SAM-+ video-tracking-based method). That model is
not available in this environment, so this module provides:

1. A pass-through for upstream object ids (e.g. produced by a real Gaussian
   Grouping run, or by any per-Gaussian instance-segmentation pipeline).
2. A real, runnable fallback: unsupervised spatial-temporal clustering
   (k-means over Gaussian position, with greedy centroid matching across
   frames to keep object ids temporally stable). This is the thing that
   actually executes in this repo's demos, and is validated against the
   synthetic scene's ground-truth object ids in `tests/test_segmentation.py`.

Swap in real Gaussian Grouping labels later by populating
`GaussianSet.object_id` upstream and skipping `segment_sequence` entirely.
"""
from __future__ import annotations

import torch

from vega.gaussians import GaussianSet


def kmeans(points: torch.Tensor, k: int, n_iters: int = 50, seed: int = 0,
           init_centers: torch.Tensor | None = None):
    """Minimal k-means (Lloyd's algorithm) implemented directly in torch so
    segmentation has no extra dependency (e.g. scikit-learn) requirement.
    """
    device = points.device
    n = points.shape[0]
    g = torch.Generator(device="cpu").manual_seed(seed)
    if init_centers is not None:
        centers = init_centers.clone().to(device)
    else:
        idx = torch.randperm(n, generator=g)[:k]
        centers = points[idx].clone()

    labels = torch.zeros(n, dtype=torch.long, device=device)
    for _ in range(n_iters):
        dists = torch.cdist(points, centers)  # (n, k)
        new_labels = dists.argmin(dim=1)
        if torch.equal(new_labels, labels) and _ > 0:
            labels = new_labels
            break
        labels = new_labels
        for c in range(k):
            mask = labels == c
            if mask.any():
                centers[c] = points[mask].mean(dim=0)
    return labels, centers


def match_centers(prev_centers: torch.Tensor, new_centers: torch.Tensor) -> torch.Tensor:
    """Greedy nearest-centroid matching so cluster ids stay temporally
    consistent across frames (cheap stand-in for object tracking)."""
    k = prev_centers.shape[0]
    dists = torch.cdist(new_centers, prev_centers)  # (k_new, k_prev)
    mapping = torch.full((k,), -1, dtype=torch.long)
    used_prev = set()
    order = dists.min(dim=1).values.argsort()
    for new_idx in order.tolist():
        row = dists[new_idx]
        for prev_idx in row.argsort().tolist():
            if prev_idx not in used_prev:
                mapping[new_idx] = prev_idx
                used_prev.add(prev_idx)
                break
    return mapping


def segment_sequence(frames: list[GaussianSet], k: int, n_iters: int = 30,
                      seed: int = 0) -> list[GaussianSet]:
    """Cluster each frame's Gaussians into `k` objects by position, keeping
    object ids consistent across frames via centroid matching.

    Returns new GaussianSet objects with `object_id` replaced by the
    unsupervised assignment (all other tensors are shared/unchanged).
    """
    out = []
    prev_centers = None
    for gs in frames:
        pts = gs.get_xyz.detach()
        init = prev_centers if prev_centers is not None else None
        labels, centers = kmeans(pts, k=k, n_iters=n_iters, seed=seed, init_centers=init)
        if prev_centers is not None:
            # mapping[new_cluster_idx] = stable_object_id (nearest-centroid match
            # against last frame's *stable-ordered* centers)
            mapping = match_centers(prev_centers, centers)
            labels = mapping[labels.cpu()].to(labels.device)
            # Reorder this frame's centers into stable-id slots so next frame's
            # matching (and k-means init) is against stable ids, not raw
            # cluster indices that can be renumbered arbitrarily each frame.
            stable_centers = torch.empty_like(centers)
            stable_centers[mapping] = centers
            centers = stable_centers
        prev_centers = centers
        new_gs = GaussianSet(
            xyz=gs.xyz, scale_raw=gs.scale_raw, rot_raw=gs.rot_raw,
            opacity_raw=gs.opacity_raw, sh_dc=gs.sh_dc, sh_rest=gs.sh_rest,
            object_id=labels, sh_degree=gs.sh_degree,
        )
        out.append(new_gs)
    return out


def clustering_purity(pred_labels: torch.Tensor, true_labels: torch.Tensor) -> float:
    """Fraction of points whose cluster majority-vote label matches the
    ground truth label (standard clustering-purity metric)."""
    pred_labels = pred_labels.cpu()
    true_labels = true_labels.cpu()
    correct = 0
    for c in pred_labels.unique().tolist():
        mask = pred_labels == c
        if mask.any():
            majority = true_labels[mask].mode().values.item()
            correct += (true_labels[mask] == majority).sum().item()
    return correct / len(pred_labels)
