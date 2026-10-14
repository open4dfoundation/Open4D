"""Spherical-harmonics constants and evaluation.

Standard real-SH basis used throughout the 3D Gaussian Splatting literature
(Kerbl et al. 2023, itself following PlenOctree). Reimplemented locally so
`vega` has no import-time dependency on any sibling repo in this environment.
"""
import torch

C0 = 0.28209479177387814
C1 = 0.4886025119029199
C2 = [
    1.0925484305920792, -1.0925484305920792, 0.31539156525252005,
    -1.0925484305920792, 0.5462742152960396,
]
C3 = [
    -0.5900435899266435, 2.890611442640554, -0.4570457994644658,
    0.3731763325901154, -0.4570457994644658, 1.445305721320277,
    -0.5900435899266435,
]

MAX_SH_DEGREE = 3


def rgb_to_sh0(rgb: torch.Tensor) -> torch.Tensor:
    """Convert an RGB color in [0, 1] to the degree-0 SH (DC) coefficient."""
    return (rgb - 0.5) / C0


def sh0_to_rgb(sh0: torch.Tensor) -> torch.Tensor:
    return sh0 * C0 + 0.5


def eval_sh(deg: int, sh: torch.Tensor, dirs: torch.Tensor) -> torch.Tensor:
    """Evaluate spherical harmonics at unit directions.

    Args:
        deg: SH degree to evaluate (0..3).
        sh: (..., (deg_max+1)**2, C) coefficients (only the first
            (deg+1)**2 bands are used).
        dirs: (..., 3) unit view directions.
    Returns:
        (..., C) evaluated color contribution.
    """
    assert 0 <= deg <= MAX_SH_DEGREE
    result = C0 * sh[..., 0, :]
    if deg > 0:
        x, y, z = dirs[..., 0:1], dirs[..., 1:2], dirs[..., 2:3]
        result = (
            result
            - C1 * y * sh[..., 1, :]
            + C1 * z * sh[..., 2, :]
            - C1 * x * sh[..., 3, :]
        )
        if deg > 1:
            xx, yy, zz = x * x, y * y, z * z
            xy, yz, xz = x * y, y * z, x * z
            result = (
                result
                + C2[0] * xy * sh[..., 4, :]
                + C2[1] * yz * sh[..., 5, :]
                + C2[2] * (2.0 * zz - xx - yy) * sh[..., 6, :]
                + C2[3] * xz * sh[..., 7, :]
                + C2[4] * (xx - yy) * sh[..., 8, :]
            )
            if deg > 2:
                result = (
                    result
                    + C3[0] * y * (3 * xx - yy) * sh[..., 9, :]
                    + C3[1] * xy * z * sh[..., 10, :]
                    + C3[2] * y * (4 * zz - xx - yy) * sh[..., 11, :]
                    + C3[3] * z * (2 * zz - 3 * xx - 3 * yy) * sh[..., 12, :]
                    + C3[4] * x * (4 * zz - xx - yy) * sh[..., 13, :]
                    + C3[5] * z * (xx - yy) * sh[..., 14, :]
                    + C3[6] * x * (xx - 3 * yy) * sh[..., 15, :]
                )
    return result
