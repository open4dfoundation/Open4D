"""Reconstruct a mesh at each timestamp from calibrated RGB-D images."""

from __future__ import annotations

import importlib
import math
import re

import numpy as np

from ...core import Frame, Sequence, TopologyMode, TriangleMesh

#: Initial voxel-block capacity of each frame's tensor volume. Open3D grows the
#: hash map when a frame touches more blocks, so this only trades memory for
#: rehashing time. A 16^3 block with TSDF, weight and colour takes 80 KiB.
DEFAULT_BLOCK_COUNT = 4096
_BLOCK_RESOLUTION = 16
#: ICP levels as multiples of voxel_size: points are downsampled to the level,
#: and correspondences and normals use twice the level.
_ICP_LEVELS = (8, 4, 2, 1)
_ICP_PASSES = 2
_DEVICE = re.compile(r"(cpu|cuda)(?::(\d+))?", re.IGNORECASE)


def _positive(value, name):
    if isinstance(value, bool) or not np.isscalar(value):
        raise ValueError(f"{name} must be a positive number")
    value = float(value)
    if not math.isfinite(value) or value <= 0:
        raise ValueError(f"{name} must be a positive number")
    return value


def _device(value):
    if value is None:
        return None
    if not isinstance(value, str):
        raise TypeError("device must be None or a string such as 'cuda:0'")
    match = _DEVICE.fullmatch(value.strip())
    if match is None:
        raise ValueError(f"device must be 'cpu', 'cuda' or 'cuda:N'; got {value!r}")
    return f"{match[1].upper()}:{int(match[2] or 0)}"


def _timestamps(values, frames):
    values = np.asarray(values)
    if values.dtype.kind not in "iuf" or values.shape != (frames,):
        raise ValueError(f"timestamps must be {frames} real numbers, one per frame")
    values = values.astype(np.float64)
    if not np.isfinite(values).all() or np.any(np.diff(values) <= 0):
        raise ValueError("timestamps must be finite and strictly increasing")
    return tuple(values.tolist())


def _meters(depth, depth_scale, depth_max):
    with np.errstate(over="ignore", under="ignore"):
        meters = depth.astype(np.float64) / depth_scale
    meters[meters >= depth_max] = 0
    return meters


class _RGBDProvider:
    topology = TopologyMode.CHANGING
    has_vertex_correspondence = False

    def __init__(self, depth, color, intrinsics, camera_poses, fps,
                 depth_scale, depth_max, voxel_size, truncation, *,
                 timestamps=None, device=None, block_count=None, metadata=None):
        self.depth = depth
        self.color = color
        self.intrinsics = intrinsics
        self.extrinsics = np.linalg.inv(camera_poses)
        self.fps = fps
        self.depth_scale = depth_scale
        self.depth_max = depth_max
        self.voxel_size = voxel_size
        self.truncation = truncation
        self.device = device
        self.block_count = block_count
        self.frame_count = len(depth)
        if timestamps is None:
            self.timestamps = tuple(index / fps for index in range(len(depth)))
            self.metadata = {"source": "rgbd", "meters_per_unit": 1.0, "fps": fps}
        else:
            # Measured times are irregular; Sequence.fps gives their mean rate.
            self.timestamps = timestamps
            self.metadata = {"source": "rgbd", "meters_per_unit": 1.0}
        if device is not None:
            self.metadata["device"] = device
        self.metadata.update(metadata or {})

    def get_frame(self, index):
        if self.device is not None:
            return self._get_tensor_frame(index)
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
            meters = _meters(depth, self.depth_scale, self.depth_max)
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

    def _get_tensor_frame(self, index):
        o3d = importlib.import_module("open3d")
        core = o3d.core
        device = core.Device(self.device)
        # Geometry only: Open3D 0.19's CUDA integration leaves the colour of some
        # weighted voxels at zero, so vertices are coloured from the images below.
        volume = o3d.t.geometry.VoxelBlockGrid(
            attr_names=("tsdf", "weight"),
            attr_dtypes=(core.float32, core.float32),
            attr_channels=((1), (1)),
            voxel_size=self.voxel_size,
            block_resolution=_BLOCK_RESOLUTION,
            block_count=self.block_count,
            device=device,
        )
        multiplier = self.truncation / self.voxel_size
        integrated = False
        for camera, depth in enumerate(self.depth[index]):
            # Depth is converted exactly as on the legacy path, then passed in metres.
            meters = _meters(depth, self.depth_scale, self.depth_max)
            if not meters.any():
                continue  # Open3D aborts when an image touches no block.
            image = o3d.t.geometry.Image(core.Tensor(
                np.ascontiguousarray(meters, dtype=np.float32))).to(device)
            fx, fy, cx, cy = self.intrinsics[camera]
            intrinsic = core.Tensor([[fx, 0, cx], [0, fy, cy], [0, 0, 1]], core.float64)
            extrinsic = core.Tensor(np.ascontiguousarray(self.extrinsics[index, camera]),
                                    core.float64)
            blocks = volume.compute_unique_block_coordinates(
                image, intrinsic, extrinsic, 1.0, self.depth_max, multiplier)
            volume.integrate(blocks, image, intrinsic, extrinsic,
                             1.0, self.depth_max, multiplier)
            integrated = True
        if not integrated:
            return Frame(index, self.timestamps[index], TriangleMesh(
                np.zeros((0, 3)), np.zeros((0, 3), dtype=np.uint32),
                colors=np.zeros((0, 3)) if self.color is not None else None,
            ))
        mesh = volume.extract_triangle_mesh(weight_threshold=0.5)
        positions = mesh.vertex.positions.cpu().numpy()
        colors = None if self.color is None else self._vertex_colors(index, positions)
        return Frame(index, self.timestamps[index], TriangleMesh(
            positions, mesh.triangle.indices.cpu().numpy(), colors=colors,
        ))

    def _vertex_colors(self, index, positions):
        """Average each vertex's colour over the cameras that see it.

        A camera sees a vertex when the vertex lies within the truncation band of
        that pixel's depth: the observations a TSDF colour volume averages.
        """
        total = np.zeros((len(positions), 3))
        count = np.zeros(len(positions))
        homogeneous = np.column_stack((positions, np.ones(len(positions))))
        for camera, depth in enumerate(self.depth[index]):
            meters = _meters(depth, self.depth_scale, self.depth_max)
            height, width = meters.shape
            local = homogeneous @ self.extrinsics[index, camera].T
            z = local[:, 2]
            fx, fy, cx, cy = self.intrinsics[camera]
            with np.errstate(divide="ignore", invalid="ignore"):
                u = np.rint(fx * local[:, 0] / z + cx)
                v = np.rint(fy * local[:, 1] / z + cy)
            inside = (z > 0) & (u >= 0) & (u < width) & (v >= 0) & (v < height)
            rows, columns = v[inside].astype(np.intp), u[inside].astype(np.intp)
            measured = meters[rows, columns]
            visible = (measured > 0) & (np.abs(z[inside] - measured) <= self.truncation)
            seen = np.flatnonzero(inside)[visible]
            total[seen] += self.color[index, camera][rows[visible], columns[visible]] / 255
            count[seen] += 1
        return total / np.maximum(count, 1)[:, None]


def _cloud(o3d, depth, intrinsics, depth_scale, depth_max):
    meters = _meters(depth, depth_scale, depth_max)
    rows, columns = np.nonzero(meters)
    z = meters[rows, columns]
    fx, fy, cx, cy = intrinsics
    cloud = o3d.geometry.PointCloud()
    cloud.points = o3d.utility.Vector3dVector(
        np.column_stack(((columns - cx) / fx * z, (rows - cy) / fy * z, z)))
    return cloud


def _refine_poses(o3d, depth, intrinsics, poses, depth_scale, depth_max, voxel_size):
    """Register each camera's first-frame depth to the other cameras' with point-to-plane ICP.

    Camera 0 stays fixed. Each other camera is aligned, in world coordinates, to
    the union of every other camera's points, so a ring is constrained by its
    neighbours rather than by the camera opposite; two passes let later
    corrections feed earlier ones. A correction is kept only if it improves the
    finest-level fit, so refinement never makes a camera worse than it was given.
    """
    registration = o3d.pipelines.registration
    clouds = [_cloud(o3d, image, intrinsics[camera], depth_scale, depth_max)
              for camera, image in enumerate(depth)]
    for camera, cloud in enumerate(clouds):
        if not cloud.has_points():
            raise ValueError(f"camera {camera} has no valid depth in the first frame; "
                             "pose refinement needs overlapping depth")
    refined = poses.copy()

    def world(camera, size):
        cloud = clouds[camera].voxel_down_sample(size)
        return cloud.transform(refined[camera])

    def target(camera, size):
        merged = o3d.geometry.PointCloud()
        for other in range(len(clouds)):
            if other != camera:
                merged += world(other, size)
        merged = merged.voxel_down_sample(size)
        merged.estimate_normals(o3d.geometry.KDTreeSearchParamHybrid(radius=2 * size, max_nn=30))
        return merged

    def score(camera, correction):
        fine = registration.evaluate_registration(world(camera, voxel_size), target(camera, voxel_size),
                                                  2 * voxel_size, correction)
        return fine.fitness, fine.inlier_rmse

    fits = {}
    for _ in range(_ICP_PASSES):
        for camera in range(1, len(clouds)):
            before = score(camera, np.eye(4))
            correction = np.eye(4)
            for level in _ICP_LEVELS:
                size = level * voxel_size
                correction = np.asarray(registration.registration_icp(
                    world(camera, size), target(camera, size), 2 * size, correction,
                    registration.TransformationEstimationPointToPlane(),
                    registration.ICPConvergenceCriteria(max_iteration=50),
                ).transformation)
            after = score(camera, correction)
            # Better means more inliers, or as many with a tighter fit.
            accepted = after[0] > before[0] or (after[0] == before[0] and after[1] < before[1])
            if accepted:
                refined[camera] = correction @ refined[camera]
            fitness, rmse = after if accepted else before
            fits[camera] = {"camera": camera, "fitness": float(fitness), "inlier_rmse": float(rmse),
                            "accepted": bool(accepted or fits.get(camera, {}).get("accepted", False))}
    for camera, fit in fits.items():
        if fit["fitness"] == 0:
            raise RuntimeError(f"ICP found no overlap between camera {camera} and the other cameras "
                               f"within {2 * voxel_size:g} m; check camera_poses")
    return refined, [fits[camera] for camera in sorted(fits)]


def reconstruct(depth, color=None, *, intrinsics, camera_poses=None, fps=None,
                timestamps=None, depth_scale=1000.0, depth_max=4.0, voxel_size=0.01,
                truncation=0.04, refine_poses=False, device=None,
                block_count=None) -> Sequence:
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

    Frames are fps apart (30 by default), or at the given timestamps in seconds
    (strictly increasing, one per frame); with timestamps, metadata has no "fps"
    and Sequence.fps reports their mean rate.

    refine_poses=True corrects each camera after the first once, from the first
    frame: point-to-plane ICP against the other cameras' points at 8, 4, 2 and 1
    times voxel_size (correspondences within twice each level), two passes, and a
    correction is kept only if it improves the fit. Every frame is then fused with
    those poses. Poses must be fixed across frames. Metadata records
    "refined_camera_poses" and per-camera "fitness", "inlier_rmse" and "accepted".
    ICP needs static structure the cameras share, such as a room's floor and
    walls; around a lone subject it can slide while its fitness still rises.

    device=None fuses on the CPU with Open3D's legacy ScalableTSDFVolume.
    "cpu", "cuda" or "cuda:N" uses the tensor VoxelBlockGrid on that device;
    block_count is its initial capacity in 16^3 voxel blocks, which Open3D
    grows when a frame needs more (default DEFAULT_BLOCK_COUNT).
    """
    if timestamps is not None and fps is not None:
        raise TypeError("timestamps and fps are mutually exclusive")
    fps = 30.0 if fps is None else _positive(fps, "fps")
    depth_scale = _positive(depth_scale, "depth_scale")
    depth_max = _positive(depth_max, "depth_max")
    voxel_size = _positive(voxel_size, "voxel_size")
    truncation = _positive(truncation, "truncation")
    if not isinstance(refine_poses, bool):
        raise TypeError("refine_poses must be bool")
    device = _device(device)
    if block_count is not None:
        if device is None:
            raise TypeError("block_count applies only with device=")
        if (isinstance(block_count, bool) or not isinstance(block_count, (int, np.integer))
                or block_count <= 0):
            raise ValueError("block_count must be a positive integer")
        block_count = int(block_count)
    elif device is not None:
        block_count = DEFAULT_BLOCK_COUNT
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
    if timestamps is not None:
        timestamps = _timestamps(timestamps, frames)
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
    refine = refine_poses and cameras > 1
    if refine and np.any(camera_poses != camera_poses[:1]):
        raise ValueError("refine_poses needs camera_poses fixed across frames (a rigid rig)")
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
    if device is not None and device.startswith("CUDA"):
        if not o3d.core.cuda.is_available():
            raise RuntimeError(
                f"device={device!r} needs an Open3D build with CUDA, but "
                "open3d.core.cuda.is_available() is False; use device='cpu' or install "
                "a CUDA-enabled Open3D 0.19")
        available = o3d.core.cuda.device_count()
        if int(device.split(":")[1]) >= available:
            raise RuntimeError(f"device={device!r} is not available; Open3D sees "
                               f"{available} CUDA device(s)")
    metadata = {}
    if refine:
        refined, fits = _refine_poses(o3d, depth[0], intrinsics, camera_poses[0],
                                      depth_scale, depth_max, voxel_size)
        camera_poses = np.broadcast_to(refined, (frames, cameras, 4, 4))
        metadata = {"refined_camera_poses": refined.tolist(), "pose_refinement": fits}
    return Sequence(_RGBDProvider(depth, color, intrinsics, camera_poses, fps,
                                 depth_scale, depth_max, voxel_size, truncation,
                                 timestamps=timestamps, device=device,
                                 block_count=block_count, metadata=metadata))
