"""One image-quality implementation, so the two methods' numbers can be compared.

The upstreams do not agree on PSNR. QUEEN rounds both images to 8 bits first and
takes one global MSE (`utils/image_utils.py`); 3DGStream takes a float MSE per
image and averages the per-image PSNRs (`utils/image_utils.py`). Those are
different numbers on the same pair of images, so comparing published figures
across the two papers directly is not sound.

This module reports both. `psnr` quantizes -- the right default when the ground
truth came from 8-bit video, and what QUEEN publishes -- and `psnr_float` is the
float form, per image, matching 3DGStream. SSIM needs no such care: the 11x11
Gaussian-window implementation is byte-identical in both upstreams, and is
reproduced here.
"""

from __future__ import annotations

from math import exp

import torch
import torch.nn.functional as F

_C1 = 0.01**2
_C2 = 0.03**2


def _as_batch(image: torch.Tensor) -> torch.Tensor:
    """Accept CHW or BCHW; return BCHW."""
    if image.dim() == 3:
        return image.unsqueeze(0)
    if image.dim() == 4:
        return image
    raise ValueError(f"expected CHW or BCHW, got shape {tuple(image.shape)}")


def psnr(rendered: torch.Tensor, truth: torch.Tensor, *, bits: int = 8) -> torch.Tensor:
    """PSNR after quantizing both images to `bits`. QUEEN's convention."""
    maxi = float(2**bits - 1)
    mse = ((torch.round(rendered * maxi) - torch.round(truth * maxi)) ** 2).mean()
    return 20 * torch.log10(maxi / torch.sqrt(mse))


def psnr_float(rendered: torch.Tensor, truth: torch.Tensor) -> torch.Tensor:
    """Per-image float PSNR, averaged. 3DGStream's convention."""
    a, b = _as_batch(rendered), _as_batch(truth)
    mse = ((a - b) ** 2).view(a.shape[0], -1).mean(1)
    return (20 * torch.log10(1.0 / torch.sqrt(mse))).mean()


def _gaussian(window_size: int, sigma: float) -> torch.Tensor:
    gauss = torch.tensor(
        [exp(-((x - window_size // 2) ** 2) / float(2 * sigma**2)) for x in range(window_size)]
    )
    return gauss / gauss.sum()


def _window(window_size: int, channels: int, like: torch.Tensor) -> torch.Tensor:
    line = _gaussian(window_size, 1.5).unsqueeze(1)
    square = line.mm(line.t()).float().unsqueeze(0).unsqueeze(0)
    return square.expand(channels, 1, window_size, window_size).contiguous().to(like)


def ssim(rendered: torch.Tensor, truth: torch.Tensor, *, window_size: int = 11) -> torch.Tensor:
    """SSIM as both upstreams compute it (3DGS's 11x11 Gaussian window)."""
    a, b = _as_batch(rendered), _as_batch(truth)
    channels = a.shape[1]
    window = _window(window_size, channels, a)
    pad = window_size // 2

    mu_a = F.conv2d(a, window, padding=pad, groups=channels)
    mu_b = F.conv2d(b, window, padding=pad, groups=channels)
    mu_a_sq, mu_b_sq, mu_ab = mu_a.pow(2), mu_b.pow(2), mu_a * mu_b

    var_a = F.conv2d(a * a, window, padding=pad, groups=channels) - mu_a_sq
    var_b = F.conv2d(b * b, window, padding=pad, groups=channels) - mu_b_sq
    cov = F.conv2d(a * b, window, padding=pad, groups=channels) - mu_ab

    numerator = (2 * mu_ab + _C1) * (2 * cov + _C2)
    denominator = (mu_a_sq + mu_b_sq + _C1) * (var_a + var_b + _C2)
    return (numerator / denominator).mean()


def lpips(rendered: torch.Tensor, truth: torch.Tensor, *, net: str = "vgg") -> torch.Tensor:
    """LPIPS via the upstream `lpipsPyTorch`, which is identical in both trees.

    Imported lazily and by path rather than reimplemented: LPIPS numbers depend on
    the exact weights and normalization, so reproducing published figures means
    running the same code the papers ran.
    """
    from .. import upstream_import

    module = upstream_import.module("queen", "lpipsPyTorch")
    return module.lpips(_as_batch(rendered), _as_batch(truth), net_type=net)


def evaluate(rendered: torch.Tensor, truth: torch.Tensor, *, with_lpips: bool = True) -> dict:
    """Every metric for one image pair, as plain floats for the manifest."""
    result = {
        "psnr": float(psnr(rendered, truth)),
        "psnr_float": float(psnr_float(rendered, truth)),
        "ssim": float(ssim(rendered, truth)),
    }
    if with_lpips:
        try:
            result["lpips"] = float(lpips(rendered, truth))
        except Exception as error:
            # LPIPS needs the upstream tree and its weights; a missing checkout
            # should degrade the report, not abort an evaluation.
            result["lpips_error"] = str(error)
    return result
