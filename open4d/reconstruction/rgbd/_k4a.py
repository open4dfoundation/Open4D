"""Azure Kinect (K4A) calibration model in plain NumPy.

This is an independent reference for the OpenCV-based projector in
``_receiver``: it follows the Azure Kinect Sensor SDK
(``src/transformation/mode_specific_calibration.c`` and
``src/transformation/intrinsic_transformation.c``) rather than OpenCV, so the
tests can check rectification, camera modes and depth-to-colour alignment
against it. The synthetic benchmark also uses it to render raw, distorted
images for virtual cameras with a known calibration.

Factory calibration JSON stores, per camera, 15 Brown-Conrady
``ModelParameters`` normalized by the calibration resolution::

    cx, cy, fx, fy, k1, k2, k3, k4, k5, k6, codx, cody, p2, p1, metric_radius

and an ``Rt`` block mapping depth-camera points (metres) into that camera.
"""

from __future__ import annotations

from dataclasses import dataclass, replace

import numpy as np

#: Depth modes: (calibration image binned resolution, crop offset, output size),
#: as hard-coded in the SDK for a 1024x1024 depth calibration.
DEPTH_MODES = {
    "nfov_2x2binned": ((512, 512), (96, 90), (320, 288)),
    "nfov_unbinned": ((1024, 1024), (192, 180), (640, 576)),
    "wfov_2x2binned": ((512, 512), (0, 0), (512, 512)),
    "wfov_unbinned": ((1024, 1024), (0, 0), (1024, 1024)),
    "passive_ir": ((1024, 1024), (0, 0), (1024, 1024)),
}
#: Colour resolutions, as hard-coded in the SDK for a 4096x3072 calibration.
COLOR_MODES = {
    "720p": ((1280, 960), (0, 120), (1280, 720)),
    "1080p": ((1920, 1440), (0, 180), (1920, 1080)),
    "1440p": ((2560, 1920), (0, 240), (2560, 1440)),
    "1536p": ((2048, 1536), (0, 0), (2048, 1536)),
    "2160p": ((3840, 2880), (0, 360), (3840, 2160)),
    "3072p": ((4096, 3072), (0, 0), (4096, 3072)),
}
_NAMES = ("cx", "cy", "fx", "fy", "k1", "k2", "k3", "k4", "k5", "k6",
          "codx", "cody", "p2", "p1", "metric_radius")


@dataclass(frozen=True)
class Intrinsics:
    """One camera's Brown-Conrady parameters, in pixels once mode-specific."""

    cx: float
    cy: float
    fx: float
    fy: float
    k1: float = 0.0
    k2: float = 0.0
    k3: float = 0.0
    k4: float = 0.0
    k5: float = 0.0
    k6: float = 0.0
    codx: float = 0.0
    cody: float = 0.0
    p2: float = 0.0
    p1: float = 0.0
    metric_radius: float = 0.0

    @classmethod
    def from_model_parameters(cls, values) -> "Intrinsics":
        values = [float(value) for value in values]
        if len(values) < len(_NAMES):
            raise ValueError(f"K4A model parameters need {len(_NAMES)} values")
        return cls(*values[:len(_NAMES)])

    def model_parameters(self) -> list[float]:
        return [float(getattr(self, name)) for name in _NAMES]

    def mode_specific(self, binned, crop) -> "Intrinsics":
        """Scale normalized parameters to a mode, as the SDK does for an image.

        The SDK multiplies the normalized principal point and focal length by
        the binned calibration resolution, subtracts the crop offset and, for
        pixel-centred output, half a pixel.
        """
        (width, height), (crop_x, crop_y) = binned, crop
        return replace(self, cx=self.cx * width - crop_x - 0.5,
                       cy=self.cy * height - crop_y - 0.5,
                       fx=self.fx * width, fy=self.fy * height)

    @property
    def matrix(self) -> np.ndarray:
        return np.array([[self.fx, 0, self.cx], [0, self.fy, self.cy], [0, 0, 1]], dtype=np.float64)


def _distort(params: Intrinsics, x, y):
    """Distort normalized coordinates with the SDK's Brown-Conrady model."""
    xp, yp = x - params.codx, y - params.cody
    xp2, yp2, xyp = xp * xp, yp * yp, xp * yp
    rs = xp2 + yp2
    a = 1 + rs * (params.k1 + rs * (params.k2 + rs * params.k3))
    b = 1 + rs * (params.k4 + rs * (params.k5 + rs * params.k6))
    # The SDK uses a / b, or a alone where b is zero.
    with np.errstate(divide="ignore", invalid="ignore"):
        d = np.where(b != 0, a / np.where(b != 0, b, 1), a)
    xd = xp * d + (rs + 2 * xp2) * params.p2 + 2 * xyp * params.p1
    yd = yp * d + (rs + 2 * yp2) * params.p1 + 2 * xyp * params.p2
    valid = np.ones(np.shape(rs), dtype=bool)
    if params.metric_radius > 0:
        valid &= rs <= params.metric_radius ** 2
    return xd + params.codx, yd + params.cody, valid


def project(params: Intrinsics, points) -> tuple[np.ndarray, np.ndarray]:
    """Project camera-frame points (N, 3) in metres to pixels (N, 2) and validity."""
    points = np.asarray(points, dtype=np.float64)
    z = points[:, 2]
    with np.errstate(divide="ignore", invalid="ignore"):
        x, y = points[:, 0] / z, points[:, 1] / z
    xd, yd, valid = _distort(params, x, y)
    uv = np.column_stack((xd * params.fx + params.cx, yd * params.fy + params.cy))
    return uv, valid & (z > 0) & np.isfinite(uv).all(axis=1)


def undistort(params: Intrinsics, uv, iterations=20) -> tuple[np.ndarray, np.ndarray]:
    """Normalized ray coordinates (N, 2) for distorted pixels, by Newton's method.

    Like the SDK, this starts from the undistorted pinhole guess and refines it
    with a numerical Jacobian of the forward model.
    """
    uv = np.asarray(uv, dtype=np.float64)
    target_x = (uv[:, 0] - params.cx) / params.fx
    target_y = (uv[:, 1] - params.cy) / params.fy
    x, y = target_x.copy(), target_y.copy()
    step = 1e-7
    for _ in range(iterations):
        fx0, fy0, _ = _distort(params, x, y)
        rx, ry = fx0 - target_x, fy0 - target_y
        ax, ay, _ = _distort(params, x + step, y)
        bx, by, _ = _distort(params, x, y + step)
        j11, j21 = (ax - fx0) / step, (ay - fy0) / step
        j12, j22 = (bx - fx0) / step, (by - fy0) / step
        determinant = j11 * j22 - j12 * j21
        with np.errstate(divide="ignore", invalid="ignore"):
            dx = (j22 * rx - j12 * ry) / determinant
            dy = (-j21 * rx + j11 * ry) / determinant
        good = np.isfinite(dx) & np.isfinite(dy)
        x[good] -= dx[good]
        y[good] -= dy[good]
    fx0, fy0, valid = _distort(params, x, y)
    error = np.hypot((fx0 - target_x) * params.fx, (fy0 - target_y) * params.fy)
    return np.column_stack((x, y)), valid & (error < 1e-3)


def unproject(params: Intrinsics, uv, depth) -> tuple[np.ndarray, np.ndarray]:
    """Camera-frame points (N, 3) for distorted pixels and their Z depth in metres."""
    rays, valid = undistort(params, uv)
    depth = np.asarray(depth, dtype=np.float64)
    points = np.column_stack((rays * depth[:, None], depth))
    return points, valid & (depth > 0)


def transform(matrix, points) -> np.ndarray:
    """Apply a 4x4 rigid transform to points (N, 3)."""
    matrix = np.asarray(matrix, dtype=np.float64)
    return np.asarray(points, dtype=np.float64) @ matrix[:3, :3].T + matrix[:3, 3]


def depth_to_color_pixels(depth_params: Intrinsics, color_params: Intrinsics,
                          depth_to_color, uv, depth) -> tuple[np.ndarray, np.ndarray]:
    """Colour pixels of depth pixels, as k4a_calibration_2d_to_2d does."""
    points, valid = unproject(depth_params, uv, depth)
    pixels, inside = project(color_params, transform(depth_to_color, points))
    return pixels, valid & inside


def factory_calibration(depth: Intrinsics, color: Intrinsics, depth_to_color, *,
                        depth_sensor=(1024, 1024), color_sensor=(4096, 3072)) -> dict:
    """A factory calibration document in the layout the capture loader reads.

    ``depth`` and ``color`` hold normalized parameters (as stored by the
    device); ``depth_to_color`` maps depth-camera metres into the colour camera.
    """
    depth_to_color = np.asarray(depth_to_color, dtype=np.float64)

    def entry(purpose, params, sensor, matrix):
        return {
            "Purpose": purpose,
            "SensorWidth": int(sensor[0]),
            "SensorHeight": int(sensor[1]),
            "Intrinsics": {"ModelType": "CALIBRATION_LensDistortionModelBrownConrady",
                           "ModelParameterCount": len(_NAMES),
                           "ModelParameters": params.model_parameters()},
            "Rt": {"Rotation": matrix[:3, :3].ravel().tolist(),
                   "Translation": matrix[:3, 3].tolist()},
        }

    return {"CalibrationInformation": {"Cameras": [
        entry("CALIBRATION_CameraPurposeDepth", depth, depth_sensor, np.eye(4)),
        entry("CALIBRATION_CameraPurposePhotoVideo", color, color_sensor, depth_to_color),
    ]}}


def camera_entry(document: dict, purpose: str) -> dict:
    return next(item for item in document["CalibrationInformation"]["Cameras"]
                if item["Purpose"] == purpose)


def mode_intrinsics(document: dict, depth_mode: str = "nfov_unbinned",
                    color_mode: str = "720p") -> tuple[Intrinsics, Intrinsics, np.ndarray]:
    """Mode-specific depth and colour parameters from the SDK's mode tables.

    The tables assume the SDK's 1024x1024 depth and 4096x3072 colour
    calibrations; other sensors scale the binned resolution proportionally.
    """
    depth = camera_entry(document, "CALIBRATION_CameraPurposeDepth")
    color = camera_entry(document, "CALIBRATION_CameraPurposePhotoVideo")
    binned, crop, _ = DEPTH_MODES[depth_mode]
    scale = depth["SensorWidth"] / 1024
    depth_params = Intrinsics.from_model_parameters(
        depth["Intrinsics"]["ModelParameters"]).mode_specific(
            (binned[0] * scale, binned[1] * scale), (crop[0] * scale, crop[1] * scale))
    binned, crop, _ = COLOR_MODES[color_mode]
    color_params = Intrinsics.from_model_parameters(
        color["Intrinsics"]["ModelParameters"]).mode_specific(binned, crop)
    matrix = np.eye(4)
    matrix[:3, :3] = np.asarray(color["Rt"]["Rotation"], dtype=np.float64).reshape(3, 3)
    matrix[:3, 3] = color["Rt"]["Translation"]
    return depth_params, color_params, matrix
