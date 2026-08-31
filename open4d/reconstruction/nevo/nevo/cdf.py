"""The per-voxel importance CDF, and the check the paper's claim rests on.

NeVo justifies visibility-aware filtering with Figure 7: the CDF of non-empty
voxels' importance is long-tailed, and "with a threshold of 0.025, ~60% of
voxels could be removed from delivery" while SSIM stays above 0.98.

Two poolings, because they are different claims and only one of them supports
filtering:

per-viewport (default)
    Every (voxel, viewport) pair is one sample. This is the fraction of the
    grid a *single* fetch can skip, so it is the one that converts to a
    bandwidth saving, and the one to compare against the paper's ~60%.

per-frame
    A voxel is scored by its best importance over all viewports tested. This
    is the fraction that is unimportant from *every* angle -- content that
    could be dropped from storage, not just from one delivery. Necessarily a
    smaller fraction.
"""
from __future__ import annotations

import json
from typing import Dict, List, Optional, Sequence

import numpy as np

PAPER_THRESHOLD = 0.025
PAPER_REMOVABLE_FRACTION = 0.60

DEFAULT_THRESHOLDS = (0.005, 0.01, 0.015, 0.02, 0.025, 0.03, 0.04, 0.05, 0.1, 0.2, 0.5)


DEFAULT_BINS = 400_000
"""Histogram resolution used by :class:`ImportanceAccumulator`.

Weights live in [0, 1] by construction, so 400k uniform bins put every edge
2.5e-6 apart -- and, deliberately, put an exact edge on 0.025 (bin 10000), the
threshold the paper's claim is stated at. Nothing is estimated at that point.
"""


class ImportanceAccumulator:
    """Streams per-frame scores into a CDF without holding them all.

    At ``block_size=1`` a single object's scores are ``frames x viewports x
    occupied_entries`` floats -- tens of gigabytes for a 24-frame sequence at
    300 viewports. Only the distribution is wanted, so bin as we go. The
    per-frame pooling keeps one max per voxel per frame instead, which is
    small enough to hold exactly.
    """

    def __init__(self, bins: int = DEFAULT_BINS):
        if bins < 1000:
            raise ValueError("bins must be >= 1000 to resolve the tail")
        self.bins = int(bins)
        self.counts = np.zeros(self.bins, dtype=np.int64)
        self.total = 0
        self.sum = 0.0
        self.maximum = 0.0

    def add(self, values: np.ndarray) -> None:
        flat = np.asarray(values, dtype=np.float64).reshape(-1)
        if flat.size == 0:
            return
        # Weights cannot exceed 1 (they are a partition of a pixel's opacity),
        # but clip rather than trust it: a NaN or a >1 from a pathological
        # checkpoint would otherwise land out of range and vanish silently.
        if not np.all(np.isfinite(flat)):
            raise ValueError("importance scores contain non-finite values")
        index = np.clip((flat * self.bins).astype(np.int64), 0, self.bins - 1)
        self.counts += np.bincount(index, minlength=self.bins)
        self.total += int(flat.size)
        self.sum += float(flat.sum())
        self.maximum = max(self.maximum, float(flat.max()))

    def fraction_below(self, threshold: float) -> float:
        """Exact when ``threshold * bins`` is an integer, else rounded down to
        the nearest bin edge."""
        if self.total == 0:
            return float("nan")
        edge = int(np.floor(threshold * self.bins))
        edge = max(0, min(self.bins, edge))
        return float(self.counts[:edge].sum() / self.total)

    def quantile(self, q: float) -> float:
        if self.total == 0:
            return float("nan")
        target = q * self.total
        cumulative = np.cumsum(self.counts)
        index = int(np.searchsorted(cumulative, target, side="left"))
        return float(min(index, self.bins - 1) / self.bins)

    @property
    def mean(self) -> float:
        return self.sum / self.total if self.total else float("nan")

    @property
    def never_hit_fraction(self) -> float:
        """Share of scored voxels that no surviving sample ever touched.

        These land in bin 0 and are the *free* part of the removable fraction:
        a voxel outside this viewport's frustum, or fully behind an opaque
        surface, contributes nothing at all rather than a little. Splitting it
        out matters because it is the part a cheap frustum test would already
        find -- what neural visibility buys over that is the mass between bin 0
        and the threshold.
        """
        if self.total == 0:
            return float("nan")
        return float(self.counts[0] / self.total)

    def curve(self, points: int = 512) -> Dict[str, List[float]]:
        if self.total == 0:
            return {"importance": [], "cdf": []}
        cumulative = np.cumsum(self.counts) / self.total
        # Sample the curve where it moves: uniform sampling in *probability*
        # rather than in importance, so the near-vertical tail is not one point.
        probabilities = np.linspace(0.0, 1.0, points)
        positions = np.searchsorted(cumulative, probabilities, side="left")
        positions = np.clip(positions, 0, self.bins - 1)
        return {
            "importance": (positions / self.bins).tolist(),
            "cdf": cumulative[positions].tolist(),
        }

    def summary(
        self,
        *,
        pooling: str,
        block_size: int,
        assignment: str,
        frames: int,
        viewports: int,
        occupied_blocks: int,
        total_blocks: int,
        thresholds: Sequence[float] = DEFAULT_THRESHOLDS,
        extra: Optional[Dict[str, object]] = None,
    ) -> Dict[str, object]:
        observed = self.fraction_below(PAPER_THRESHOLD)
        payload = {
            "pooling": pooling,
            "block_size": block_size,
            "assignment": assignment,
            "frames": frames,
            "viewports_per_frame": viewports,
            "total_blocks": total_blocks,
            "occupied_blocks": occupied_blocks,
            "scored_samples": self.total,
            "histogram_bins": self.bins,
            "fraction_below": {str(t): self.fraction_below(t) for t in thresholds},
            "quantiles": {str(q): self.quantile(q) for q in (0.1, 0.25, 0.5, 0.75, 0.9, 0.99)},
            "mean": self.mean,
            "max": self.maximum,
            "never_hit_fraction": self.never_hit_fraction,
            "paper_check": {
                "threshold": PAPER_THRESHOLD,
                "paper_fraction_below": PAPER_REMOVABLE_FRACTION,
                "observed_fraction_below": observed,
                "difference": observed - PAPER_REMOVABLE_FRACTION,
                "long_tailed": bool(observed >= 0.5),
            },
        }
        if extra:
            payload.update(extra)
        return payload


def write_report(path, summaries: Sequence[Dict[str, object]], curves: Dict[str, object]) -> None:
    payload = {"summaries": list(summaries), "curves": curves}
    with open(path, "w") as handle:
        json.dump(payload, handle, indent=1)


def plot(curves: Dict[str, Dict[str, List[float]]], path, title: str) -> None:
    """Write a Figure-7-style CDF plot. Optional: needs matplotlib."""
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    figure, axis = plt.subplots(figsize=(5.0, 3.2))
    for label, curve in curves.items():
        axis.plot(curve["importance"], curve["cdf"], label=label, linewidth=1.6)
    axis.axvline(PAPER_THRESHOLD, color="0.5", linestyle="--", linewidth=1.0)
    axis.annotate(
        "0.025",
        xy=(PAPER_THRESHOLD, 0.04),
        xytext=(PAPER_THRESHOLD + 0.02, 0.04),
        color="0.35",
        fontsize=8,
    )
    axis.set_xlabel("Voxel importance  (max $T_i\\,\\alpha_i$)")
    axis.set_ylabel("CDF")
    axis.set_xlim(0.0, 1.0)
    axis.set_ylim(0.0, 1.0)
    axis.grid(alpha=0.25, linewidth=0.6)
    axis.set_title(title, fontsize=9)
    axis.legend(fontsize=7, loc="lower right")
    figure.tight_layout()
    figure.savefig(path, dpi=200)
    plt.close(figure)
