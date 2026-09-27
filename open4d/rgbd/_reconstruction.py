"""Reconstruct a mesh at each timestamp from calibrated RGB-D images."""

from __future__ import annotations

import importlib
import math

import numpy as np

from ..core import Frame, Sequence, TopologyMode, TriangleMesh


def _positive(value, name):
    if isinstance(value, bool) or not np.isscalar(value):
        raise ValueError(f"{name} must be a positive number")
    value = float(value)
    if not math.isfinite(value) or value <= 0:
        raise ValueError(f"{name} must be a positive number")
    return value


class _RGBDProvider:
    topology = TopologyMode.CHANGING
    has_vertex_correspondence = False

    def __init__(self, depth, color, intrinsics, camera_poses, fps,
                 depth_scale, depth_max, voxel_size, truncation):
        self.depth = depth
        self.color = color
        self.intrinsics = intrinsics
        self.extrinsics = np.linalg.inv(camera_poses)
        self.fps = fps
        self.depth_scale = depth_scale
        self.depth_max = depth_max
        self.voxel_size = voxel_size
        self.truncation = truncation
        self.frame_count = len(depth)
        self.timestamps = tuple(index / fps for index in range(len(depth)))
        self.metadata = {"source": "rgbd", "meters_per_unit": 1.0, "fps": fps}

    def get_frame(self, index):
        o3d = importlib.import_module("open3d")
        integration = o3d.pipelines.integration
        volume = integration.ScalableTSDFVolume(
            voxel_length=self.voxel_size,
            sdf_trunc=self.truncation,
            color_type=integration.TSDFVolumeColorType.RGB8,
        )
        for camera, depth in enumerate(self.depth[index]):
            height, width = depth.shape
            color = (np.zeros((height, width, 3), dtype=np.uint8)
                     if self.color is None else self.color[index, camera])
            with np.errstate(over="ignore", under="ignore"):
                meters = depth.astype(np.float64) / self.depth_scale
            meters[meters >= self.depth_max] = 0
            rgbd = o3d.geometry.RGBDImage.create_from_color_and_depth(
                o3d.geometry.Image(np.ascontiguousarray(color)),
                o3d.geometry.Image(np.ascontiguousarray(meters, dtype=np.float32)),
                depth_scale=1.0,
                depth_trunc=self.depth_max,
                convert_rgb_to_intensity=False,
            )
            fx, fy, cx, cy = self.intrinsics[camera]
            intrinsic = o3d.camera.PinholeCameraIntrinsic(width, height, fx, fy, cx, cy)
            volume.integrate(rgbd, intrinsic, self.extrinsics[index, camera])
        mesh = volume.extract_triangle_mesh()
        return Frame(index, self.timestamps[index], TriangleMesh(
            np.asarray(mesh.vertices), np.asarray(mesh.triangles),
            colors=np.asarray(mesh.vertex_colors) if self.color is not None else None,
        ))


def reconstruct(depth, color=None, *, intrinsics, camera_poses=None, fps=30.0,
                depth_scale=1000.0, depth_max=4.0, voxel_size=0.01,
                truncation=0.04) -> Sequence:
    """Turn aligned depth and RGB images into a lazy mesh sequence.

    Depth has shape (frames, height, width), or (frames, cameras, height, width).
    Zero depth means missing data. RGB uses the same shape plus a final axis of
    three uint8 channels. Images must be undistorted and aligned beforehand.
    Intrinsics are (fx, fy, cx, cy) in pixels, one tuple per camera if needed.
    Camera poses map camera coordinates to world coordinates in metres: one
    4x4 matrix, one per camera, or one per frame and camera. A single moving
    camera can use (frames, 4, 4). The default is a stationary camera at origin.
    Depth is in millimetres by default; use depth_scale=1 for metres. Voxel size,
    truncation and depth_max are in metres. Each timestamp gets a fresh volume.
    """
    fps = _positive(fps, "fps")
    depth_scale = _positive(depth_scale, "depth_scale")
    depth_max = _positive(depth_max, "depth_max")
    voxel_size = _positive(voxel_size, "voxel_size")
    truncation = _positive(truncation, "truncation")
    depth = np.asarray(depth)
    if color is not None:
        color = np.asarray(color)
        if color.shape != (*depth.shape, 3) or color.dtype != np.uint8:
            raise ValueError("color must match depth dimensions with three uint8 RGB channels")
    if depth.ndim == 3:
        depth = depth[:, None]
        if color is not None:
            color = color[:, None]
    if depth.ndim != 4 or any(size == 0 for size in depth.shape):
        raise ValueError("depth must have shape (frames, height, width) or (frames, cameras, height, width)")
    if depth.dtype.kind not in "uif" or not np.isfinite(depth).all() or np.any(depth < 0):
        raise ValueError("depth must contain finite nonnegative numbers")
    frames, cameras = depth.shape[:2]
    intrinsics = np.asarray(intrinsics, dtype=np.float64)
    if intrinsics.shape == (4,):
        intrinsics = np.broadcast_to(intrinsics, (cameras, 4))
    if (intrinsics.shape != (cameras, 4) or not np.isfinite(intrinsics).all()
            or np.any(intrinsics[:, :2] <= 0)):
        raise ValueError("intrinsics must contain (fx, fy, cx, cy) with positive focal lengths for each camera")
    if camera_poses is None:
        if cameras > 1:
            raise ValueError("camera_poses is required for multiple cameras")
        camera_poses = np.eye(4)
    camera_poses = np.asarray(camera_poses, dtype=np.float64)
    if cameras == 1 and camera_poses.shape == (frames, 4, 4):
        camera_poses = camera_poses[:, None]
    try:
        camera_poses = np.broadcast_to(camera_poses, (frames, cameras, 4, 4))
    except ValueError as error:
        raise ValueError("camera_poses must be 4x4 matrices for each camera or each frame and camera") from error
    rotation = camera_poses[..., :3, :3]
    if (not np.isfinite(camera_poses).all()
            or not np.allclose(camera_poses[..., 3, :], [0, 0, 0, 1])
            or not np.allclose(rotation @ np.swapaxes(rotation, -1, -2), np.eye(3), atol=1e-5)
            or not np.allclose(np.linalg.det(rotation), 1, atol=1e-5)):
        raise ValueError("camera_poses must be rigid camera-to-world transforms")
    try:
        o3d = importlib.import_module("open3d")
    except ModuleNotFoundError as error:
        if error.name != "open3d":
            raise
        raise ModuleNotFoundError(
            "RGB-D reconstruction needs Open3D. Install open4d[open3d].",
            name="open3d",
        ) from error
    if o3d.__version__.split(".")[:2] != ["0", "19"]:
        raise RuntimeError(
            f"RGB-D reconstruction requires Open3D 0.19.x; found {o3d.__version__}. "
            "Open3D 0.20's legacy TSDF integration rescales floating-point depth "
            "and can silently return empty meshes. "
            "Install a supported runtime with: python -m pip install 'open3d>=0.19,<0.20'"
        )
    return Sequence(_RGBDProvider(depth, color, intrinsics, camera_poses, fps,
                                 depth_scale, depth_max, voxel_size, truncation))
