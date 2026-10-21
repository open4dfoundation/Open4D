"""The part of the live receiver that load_capture needs, packaged.

``python/live_two_camera_fusion.py`` and ``python/protocol.py`` are scripts
that exist only in a source checkout; wheels and sdists leave them out. Saved
captures are rectified exactly as the receiver does it, so the OBP1 payload
constants, the factory-calibration camera models, colour alignment and the
transform reader are copied here verbatim in behaviour. The RGB-D tests compare
this module with the scripts so the two cannot drift apart.

The projector also takes a ``CameraMode`` (see ``_camera``) for captures that
use other image sizes, crops, colour resolutions or depth units; the default
mode reproduces the live receiver's fixed Orbbec setting exactly.

OpenCV is imported only when a projector is built or used.
"""

from __future__ import annotations

import importlib
import json
import os
from pathlib import Path

import numpy as np

# OBP1 payload descriptor values, as in python/protocol.py.
STREAM_COLOR = 1
STREAM_DEPTH = 2
CODEC_MJPEG = 1
CODEC_ZSTD = 2
FORMAT_DEPTH16_LE = 4
MAX_SINGLE_PAYLOAD = 12 * 1024 * 1024

# Depth camera geometry, as in python/live_two_camera_fusion.py.
WIDTH = 640
HEIGHT = 576
DEPTH_BYTES = WIDTH * HEIGHT * 2
NFOV_UNBINNED_CROP_X = 192.0
NFOV_UNBINNED_CROP_Y = 180.0


def default_serials() -> tuple[str, str]:
    """Camera serials the live receiver expects, from its environment variables."""
    return (os.environ.get("FOURD_CAMERA1_SERIAL", "CL8K14101EY"),
            os.environ.get("FOURD_CAMERA2_SERIAL", "CL8K14101J3"))


def camera_entry(path: Path, purpose: str) -> dict:
    data = json.loads(Path(path).read_text(encoding="utf-8-sig"))
    return next(
        item
        for item in data["CalibrationInformation"]["Cameras"]
        if item["Purpose"] == purpose
    )


def distortion(values: list[float]) -> np.ndarray:
    # OpenCV order (k1, k2, p1, p2, k3, k4, k5, k6) from Brown-Conrady parameters.
    return np.array([values[4], values[5], values[13], values[12],
                     values[6], values[7], values[8], values[9]], dtype=np.float64)


def depth_model(factory_path: Path, mode=None) -> tuple[np.ndarray, np.ndarray]:
    item = camera_entry(factory_path, "CALIBRATION_CameraPurposeDepth")
    values = item["Intrinsics"]["ModelParameters"]
    sensor_width = item["SensorWidth"]
    sensor_height = item["SensorHeight"]
    crop_x, crop_y = NFOV_UNBINNED_CROP_X, NFOV_UNBINNED_CROP_Y
    if mode is not None:
        # The binned calibration resolution, then the mode's crop (K4A model).
        sensor_width = sensor_width / mode.depth_binning
        sensor_height = sensor_height / mode.depth_binning
        crop_x, crop_y = mode.depth_crop
    matrix = np.array([
        [values[2] * sensor_width, 0,
         values[0] * sensor_width - crop_x - 0.5],
        [0, values[3] * sensor_height,
         values[1] * sensor_height - crop_y - 0.5],
        [0, 0, 1],
    ], dtype=np.float64)
    return matrix, distortion(values)


def color_model(factory_path: Path, mode=None) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    item = camera_entry(factory_path, "CALIBRATION_CameraPurposePhotoVideo")
    values = item["Intrinsics"]["ModelParameters"]
    sensor_width = item["SensorWidth"]
    sensor_height = item["SensorHeight"]
    width, height = (1280, 720) if mode is None else mode.color_size
    if mode is not None and mode.color_binned is not None:
        (binned_width, binned_height), (crop_x, crop_y) = mode.color_binned, mode.color_crop
        matrix = np.array([
            [values[2] * binned_width, 0, values[0] * binned_width - crop_x - 0.5],
            [0, values[3] * binned_height, values[1] * binned_height - crop_y - 0.5],
            [0, 0, 1],
        ], dtype=np.float64)
    else:
        # Scale the sensor to the image width and crop the height evenly.
        scale = float(width) / sensor_width
        crop_y = (sensor_height * scale - float(height)) / 2.0
        matrix = np.array([
            [values[2] * sensor_width * scale, 0, values[0] * sensor_width * scale - 0.5],
            [0, values[3] * sensor_height * scale,
             values[1] * sensor_height * scale - crop_y - 0.5],
            [0, 0, 1],
        ], dtype=np.float64)
    rt = item["Rt"]
    depth_to_color = np.eye(4, dtype=np.float64)
    depth_to_color[:3, :3] = np.asarray(rt["Rotation"], dtype=np.float64).reshape(3, 3)
    depth_to_color[:3, 3] = np.asarray(rt["Translation"], dtype=np.float64)
    return matrix, distortion(values), depth_to_color


class CameraProjector:
    """Rectify depth and align the camera's RGB image to depth geometry."""

    def __init__(self, factory_path: Path, mode=None):
        cv2 = importlib.import_module("cv2")
        self.width, self.height = (WIDTH, HEIGHT) if mode is None else mode.depth_size
        self.color_size = (1280, 720) if mode is None else mode.color_size
        self.depth_scale = 1000.0 if mode is None else mode.depth_scale
        self.depth_k, self.depth_d = depth_model(factory_path, mode)
        self.color_k, self.color_d, self.depth_to_color = color_model(factory_path, mode)
        self.map_x, self.map_y = cv2.initUndistortRectifyMap(
            self.depth_k, self.depth_d, None, self.depth_k, (self.width, self.height), cv2.CV_32FC1)
        y, x = np.mgrid[0:self.height, 0:self.width]
        self.x_factor = ((x.astype(np.float64) - self.depth_k[0, 2]) / self.depth_k[0, 0]).ravel()
        self.y_factor = ((y.astype(np.float64) - self.depth_k[1, 2]) / self.depth_k[1, 1]).ravel()

    @staticmethod
    def decode_color(color_jpeg: bytes, size: tuple[int, int] = (1280, 720)) -> np.ndarray:
        cv2 = importlib.import_module("cv2")
        color_bgr = cv2.imdecode(np.frombuffer(color_jpeg, dtype=np.uint8), cv2.IMREAD_COLOR)
        if color_bgr is None or color_bgr.shape[:2] != (size[1], size[0]):
            raise RuntimeError(f"unable to decode {size[0]}x{size[1]} camera MJPEG")
        return color_bgr

    def rectify(self, depth_raw: np.ndarray) -> np.ndarray:
        cv2 = importlib.import_module("cv2")
        return cv2.remap(depth_raw, self.map_x, self.map_y, cv2.INTER_NEAREST,
                         borderMode=cv2.BORDER_CONSTANT, borderValue=0)

    def color_coordinates(self, rectified_depth: np.ndarray) -> np.ndarray:
        """Colour-image pixel (u, v) of each rectified depth pixel; -1 where unseen."""
        cv2 = importlib.import_module("cv2")
        z = rectified_depth.astype(np.float64).ravel() / self.depth_scale
        points = np.column_stack((self.x_factor * z, self.y_factor * z, z))
        points_color = (self.depth_to_color[:3, :3] @ points.T).T + self.depth_to_color[:3, 3]
        projected, _ = cv2.projectPoints(points_color, np.zeros(3), np.zeros(3),
                                         self.color_k, self.color_d)
        uv = projected.reshape(self.height, self.width, 2).astype(np.float32)
        invalid = (rectified_depth == 0) | (points_color[:, 2].reshape(self.height, self.width) <= 0)
        uv[invalid] = -1
        return uv

    def prepare_from_bgr(self, depth_raw: np.ndarray,
                         color_bgr: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
        cv2 = importlib.import_module("cv2")
        rectified_depth = self.rectify(depth_raw)
        uv = self.color_coordinates(rectified_depth)
        aligned_bgr = cv2.remap(color_bgr, uv[:, :, 0], uv[:, :, 1], cv2.INTER_LINEAR,
                                borderMode=cv2.BORDER_CONSTANT, borderValue=(0, 0, 0))
        aligned_rgb = cv2.cvtColor(aligned_bgr, cv2.COLOR_BGR2RGB)
        return np.ascontiguousarray(rectified_depth), np.ascontiguousarray(aligned_rgb)


def load_transform(path: Path) -> np.ndarray:
    path = Path(path)
    if path.suffix.lower() == ".json":
        data = json.loads(path.read_text())
        for key in ("global_j3_depth_to_ey_depth", "j3_depth_to_ey_depth"):
            if key in data:
                value = np.asarray(data[key], dtype=np.float64)
                break
        else:
            raise RuntimeError(f"no J3-to-EY matrix found in {path}")
    else:
        value = np.loadtxt(path, dtype=np.float64)
    if value.shape != (4, 4) or not np.all(np.isfinite(value)):
        raise RuntimeError("J3-to-EY transform must be a finite 4x4 matrix")
    return value
