"""PSNR / SSIM / LPIPS against a held-out capture, scored on the subject.

Two choices here decide whether the numbers mean anything, and both have to
hold for anything this gets compared against:

**The reference is a captured image, not our own render.** Scoring a filtered
render against the unfiltered one measures how well filtering preserves *our
reconstruction*, which flatters it -- errors the NeRF already had cancel out.
Scoring against the real camera measures how good the delivered content is,
which is the question.

**Metrics are computed on the subject's bounding box, not the whole frame.**
The corpus renders a body over a background that is identically white in both
images. Including it inflates PSNR without bound (~90% of the frame is a
perfect match) and pins SSIM near 1, so the interesting variation disappears
into the average. The crop is taken from the reference matte, unioned over the
frames being scored so it does not move between them.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable, Optional, Sequence, Tuple

import numpy as np

from . import rerf_env


@dataclass(frozen=True)
class Quality:
    psnr: float
    ssim: float
    lpips: float

    def as_dict(self) -> dict:
        return {"psnr": self.psnr, "ssim": self.ssim, "lpips": self.lpips}


def silhouette_box(mattes: Iterable[np.ndarray], pad: int = 8) -> Tuple[int, int, int, int]:
    """``(top, left, bottom, right)`` covering every matte, with a small pad.

    ``pad`` keeps a margin of background inside the crop so a filtering
    artefact that spills just outside the silhouette still lands in the score.
    """
    top, left = np.inf, np.inf
    bottom, right = -np.inf, -np.inf
    height = width = 0
    for matte in mattes:
        height, width = matte.shape[:2]
        rows = np.flatnonzero(matte.any(axis=1))
        columns = np.flatnonzero(matte.any(axis=0))
        if rows.size == 0 or columns.size == 0:
            continue
        top, bottom = min(top, rows[0]), max(bottom, rows[-1])
        left, right = min(left, columns[0]), max(right, columns[-1])
    if not np.isfinite(top):
        raise ValueError("every matte is empty; nothing to score")
    return (
        int(max(top - pad, 0)),
        int(max(left - pad, 0)),
        int(min(bottom + pad + 1, height)),
        int(min(right + pad + 1, width)),
    )


def crop(image: np.ndarray, box: Tuple[int, int, int, int]) -> np.ndarray:
    top, left, bottom, right = box
    return image[top:bottom, left:right]


class QualityScorer:
    """Scores renders against references. Holds the LPIPS network."""

    def __init__(self, net: str = "alex", device: str = "cuda"):
        rerf_env.activate()
        with rerf_env.rerf_cwd():
            import torch
            from lib import utils

        self._torch = torch
        self._utils = utils
        self._device = device
        self._net = net

    def score(self, prediction: np.ndarray, reference: np.ndarray,
              box: Optional[Sequence[int]] = None) -> Quality:
        if prediction.shape != reference.shape:
            raise ValueError(
                f"shape mismatch: rendered {prediction.shape} vs reference {reference.shape}"
            )
        if box is not None:
            prediction = crop(prediction, tuple(box))
            reference = crop(reference, tuple(box))
        prediction = np.clip(prediction.astype(np.float64), 0.0, 1.0)
        reference = np.clip(reference.astype(np.float64), 0.0, 1.0)

        error = float(np.mean((prediction - reference) ** 2))
        psnr = float("inf") if error <= 0.0 else float(-10.0 * np.log10(error))
        # ReRF's own SSIM and LPIPS, so these are the numbers upstream reports.
        ssim = float(self._utils.rgb_ssim(prediction, reference, max_val=1.0))
        lpips = float(
            self._utils.rgb_lpips(
                prediction.astype(np.float32),
                reference.astype(np.float32),
                net_name=self._net,
                device=self._device,
            )
        )
        return Quality(psnr=psnr, ssim=ssim, lpips=lpips)
