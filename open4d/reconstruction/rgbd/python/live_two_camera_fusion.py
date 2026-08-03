#!/usr/bin/env python3
"""Live full-scene fusion for two synchronized Orbbec Femto Bolt cameras.

The Windows sender delivers one framed packet containing RGB and depth from
both cameras.  This receiver displays a fused colored point cloud immediately
and rebuilds a full-scene TSDF mesh from a short rolling temporal window.

No spatial crop, connected-component filtering, decimation, or geometry
cleanup is performed.  Only invalid/out-of-range sensor depth is excluded.
"""

from __future__ import annotations

import argparse
import dataclasses
import ipaddress
import json
import os
import queue
import signal
import socket
import statistics
import threading
import time
import warnings
from collections import deque
from pathlib import Path

import cv2
import numpy as np
import open3d as o3d
import zstandard

import protocol


EY_SERIAL = os.environ.get("FOURD_CAMERA1_SERIAL", "CL8K14101EY")
J3_SERIAL = os.environ.get("FOURD_CAMERA2_SERIAL", "CL8K14101J3")
WIDTH = 640
HEIGHT = 576
DEPTH_BYTES = WIDTH * HEIGHT * 2
NFOV_UNBINNED_CROP_X = 192.0
NFOV_UNBINNED_CROP_Y = 180.0
MIN_DEPTH_M = 0.5
MAX_DEPTH_M = 3.86


@dataclasses.dataclass(frozen=True)
class DecodedPair:
    number: int
    sync_error_us: int
    ey_color_jpeg: bytes
    ey_depth: np.ndarray
    j3_color_jpeg: bytes
    j3_depth: np.ndarray


@dataclasses.dataclass(frozen=True)
class PreparedPair:
    number: int
    sync_error_us: int
    ey_depth: np.ndarray
    ey_color: np.ndarray
    j3_depth: np.ndarray
    j3_color: np.ndarray


@dataclasses.dataclass(frozen=True)
class MeshResult:
    mesh: o3d.geometry.TriangleMesh
    source_pair: int
    input_frames: int
    build_seconds: float
    backend: str
    fusion_mode: str
    merge_mode: str
    merge_seconds: float
    partial_vertices: tuple[int, int]
    partial_triangles: tuple[int, int]


def camera_entry(path: Path, purpose: str) -> dict:
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    return next(
        item
        for item in data["CalibrationInformation"]["Cameras"]
        if item["Purpose"] == purpose
    )


def distortion(values: list[float]) -> np.ndarray:
    return np.array(
        [
            values[4],
            values[5],
            values[13],
            values[12],
            values[6],
            values[7],
            values[8],
            values[9],
        ],
        dtype=np.float64,
    )


def depth_model(factory_path: Path) -> tuple[np.ndarray, np.ndarray]:
    item = camera_entry(factory_path, "CALIBRATION_CameraPurposeDepth")
    values = item["Intrinsics"]["ModelParameters"]
    sensor_width = item["SensorWidth"]
    sensor_height = item["SensorHeight"]
    matrix = np.array(
        [
            [
                values[2] * sensor_width,
                0,
                values[0] * sensor_width - NFOV_UNBINNED_CROP_X - 0.5,
            ],
            [
                0,
                values[3] * sensor_height,
                values[1] * sensor_height - NFOV_UNBINNED_CROP_Y - 0.5,
            ],
            [0, 0, 1],
        ],
        dtype=np.float64,
    )
    return matrix, distortion(values)


def color_model(
    factory_path: Path,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    item = camera_entry(factory_path, "CALIBRATION_CameraPurposePhotoVideo")
    values = item["Intrinsics"]["ModelParameters"]
    sensor_width = item["SensorWidth"]
    sensor_height = item["SensorHeight"]
    scale = 1280.0 / sensor_width
    crop_y = (sensor_height * scale - 720.0) / 2.0
    matrix = np.array(
        [
            [
                values[2] * sensor_width * scale,
                0,
                values[0] * sensor_width * scale - 0.5,
            ],
            [
                0,
                values[3] * sensor_height * scale,
                values[1] * sensor_height * scale - crop_y - 0.5,
            ],
            [0, 0, 1],
        ],
        dtype=np.float64,
    )
    rt = item["Rt"]
    depth_to_color = np.eye(4, dtype=np.float64)
    depth_to_color[:3, :3] = np.asarray(
        rt["Rotation"], dtype=np.float64
    ).reshape(3, 3)
    depth_to_color[:3, 3] = np.asarray(
        rt["Translation"], dtype=np.float64
    )
    return matrix, distortion(values), depth_to_color


class CameraProjector:
    """Rectify depth and align the camera's RGB image to depth geometry."""

    def __init__(self, factory_path: Path):
        self.depth_k, self.depth_d = depth_model(factory_path)
        self.color_k, self.color_d, self.depth_to_color = color_model(
            factory_path
        )
        self.map_x, self.map_y = cv2.initUndistortRectifyMap(
            self.depth_k,
            self.depth_d,
            None,
            self.depth_k,
            (WIDTH, HEIGHT),
            cv2.CV_32FC1,
        )
        y, x = np.mgrid[0:HEIGHT, 0:WIDTH]
        self.x_factor = (
            (x.astype(np.float64) - self.depth_k[0, 2])
            / self.depth_k[0, 0]
        ).ravel()
        self.y_factor = (
            (y.astype(np.float64) - self.depth_k[1, 2])
            / self.depth_k[1, 1]
        ).ravel()
        self.intrinsic = o3d.camera.PinholeCameraIntrinsic(
            WIDTH,
            HEIGHT,
            float(self.depth_k[0, 0]),
            float(self.depth_k[1, 1]),
            float(self.depth_k[0, 2]),
            float(self.depth_k[1, 2]),
        )
        self._sample_cache: dict[
            int, tuple[np.ndarray, np.ndarray, np.ndarray]
        ] = {}

    @staticmethod
    def decode_color(color_jpeg: bytes) -> np.ndarray:
        color_bgr = cv2.imdecode(
            np.frombuffer(color_jpeg, dtype=np.uint8), cv2.IMREAD_COLOR
        )
        if color_bgr is None or color_bgr.shape[:2] != (720, 1280):
            raise RuntimeError("unable to decode 1280x720 camera MJPEG")
        return color_bgr

    def prepare_from_bgr(
        self, depth_raw: np.ndarray, color_bgr: np.ndarray
    ) -> tuple[np.ndarray, np.ndarray]:
        rectified_depth = cv2.remap(
            depth_raw,
            self.map_x,
            self.map_y,
            cv2.INTER_NEAREST,
            borderMode=cv2.BORDER_CONSTANT,
            borderValue=0,
        )
        z = rectified_depth.astype(np.float64).ravel() / 1000.0
        points = np.column_stack(
            (self.x_factor * z, self.y_factor * z, z)
        )
        points_color = (
            self.depth_to_color[:3, :3] @ points.T
        ).T + self.depth_to_color[:3, 3]
        projected, _ = cv2.projectPoints(
            points_color,
            np.zeros(3),
            np.zeros(3),
            self.color_k,
            self.color_d,
        )
        uv = projected.reshape(HEIGHT, WIDTH, 2).astype(np.float32)
        invalid = (rectified_depth == 0) | (
            points_color[:, 2].reshape(HEIGHT, WIDTH) <= 0
        )
        uv[invalid] = -1
        aligned_bgr = cv2.remap(
            color_bgr,
            uv[:, :, 0],
            uv[:, :, 1],
            cv2.INTER_LINEAR,
            borderMode=cv2.BORDER_CONSTANT,
            borderValue=(0, 0, 0),
        )
        aligned_rgb = cv2.cvtColor(aligned_bgr, cv2.COLOR_BGR2RGB)
        return (
            np.ascontiguousarray(rectified_depth),
            np.ascontiguousarray(aligned_rgb),
        )

    def sampled_geometry(
        self,
        depth_raw: np.ndarray,
        color_bgr: np.ndarray,
        stride: int,
    ) -> o3d.geometry.PointCloud:
        if stride not in self._sample_cache:
            y, x = np.mgrid[0:HEIGHT:stride, 0:WIDTH:stride]
            pixels = np.column_stack((x.ravel(), y.ravel())).astype(
                np.float32
            )
            normalized = cv2.undistortPoints(
                pixels.reshape(-1, 1, 2),
                self.depth_k,
                self.depth_d,
            ).reshape(-1, 2)
            rays = np.column_stack(
                (
                    normalized[:, 0],
                    normalized[:, 1],
                    np.ones(len(normalized)),
                )
            )
            self._sample_cache[stride] = (x.ravel(), y.ravel(), rays)
        x, y, rays = self._sample_cache[stride]
        z = depth_raw[y, x].astype(np.float64) / 1000.0
        valid = (z >= MIN_DEPTH_M) & (z <= MAX_DEPTH_M)
        points = rays[valid] * z[valid, None]
        points_color = (
            self.depth_to_color[:3, :3] @ points.T
        ).T + self.depth_to_color[:3, 3]
        projected, _ = cv2.projectPoints(
            points_color,
            np.zeros(3),
            np.zeros(3),
            self.color_k,
            self.color_d,
        )
        uv = np.rint(projected.reshape(-1, 2)).astype(np.int32)
        color_valid = (
            (points_color[:, 2] > 0)
            & (uv[:, 0] >= 0)
            & (uv[:, 0] < color_bgr.shape[1])
            & (uv[:, 1] >= 0)
            & (uv[:, 1] < color_bgr.shape[0])
        )
        points = points[color_valid]
        uv = uv[color_valid]
        colors = (
            color_bgr[uv[:, 1], uv[:, 0]][:, ::-1].astype(np.float64)
            / 255.0
        )
        cloud = o3d.geometry.PointCloud()
        cloud.points = o3d.utility.Vector3dVector(points)
        cloud.colors = o3d.utility.Vector3dVector(colors)
        return cloud


def make_rgbd(depth: np.ndarray, color: np.ndarray) -> o3d.geometry.RGBDImage:
    return o3d.geometry.RGBDImage.create_from_color_and_depth(
        o3d.geometry.Image(np.ascontiguousarray(color)),
        o3d.geometry.Image(np.ascontiguousarray(depth)),
        depth_scale=1000.0,
        depth_trunc=MAX_DEPTH_M,
        convert_rgb_to_intensity=False,
    )


def make_fused_cloud(
    pair: DecodedPair,
    ey_color_bgr: np.ndarray,
    j3_color_bgr: np.ndarray,
    ey_projector: CameraProjector,
    j3_projector: CameraProjector,
    j3_to_ey: np.ndarray,
    stride: int,
    voxel: float,
) -> o3d.geometry.PointCloud:
    ey = ey_projector.sampled_geometry(
        pair.ey_depth, ey_color_bgr, stride
    )
    j3 = j3_projector.sampled_geometry(
        pair.j3_depth, j3_color_bgr, stride
    )
    j3.transform(j3_to_ey)
    fused = ey + j3
    if voxel > 0:
        fused = fused.voxel_down_sample(voxel)
    return fused


def temporal_median(values: list[np.ndarray]) -> np.ndarray:
    stack = np.stack(values).astype(np.float32)
    stack[stack == 0] = np.nan
    with warnings.catch_warnings():
        warnings.filterwarnings(
            "ignore", message="All-NaN slice encountered", category=RuntimeWarning
        )
        with np.errstate(invalid="ignore"):
            result = np.nanmedian(stack, axis=0)
    output = np.zeros((HEIGHT, WIDTH), dtype=np.uint16)
    valid = (
        np.isfinite(result)
        & (result >= MIN_DEPTH_M * 1000.0)
        & (result <= MAX_DEPTH_M * 1000.0)
    )
    output[valid] = np.rint(result[valid]).astype(np.uint16)
    return output


def resolve_mesh_device(requested: str) -> o3d.core.Device:
    value = requested.strip()
    if value.lower() == "auto":
        if o3d.core.cuda.is_available():
            value = f"CUDA:{os.environ.get('FOURD_CUDA_DEVICE', '0')}"
        else:
            value = "CPU:0"
    device = o3d.core.Device(value)
    if str(device).startswith("CUDA") and not o3d.core.cuda.is_available():
        raise RuntimeError(
            f"CUDA mesh device requested ({device}), but Open3D CUDA "
            "support is unavailable"
        )
    return device


def build_mesh_cpu(
    frames: list[PreparedPair],
    ey_projector: CameraProjector,
    j3_projector: CameraProjector,
    j3_to_ey: np.ndarray,
    voxel: float,
    truncation: float,
) -> o3d.geometry.TriangleMesh:
    # The rolling median reduces temporal noise. A pixel is retained whenever
    # at least one frame contains a valid measurement; there is no support
    # threshold or scene crop.
    ey_depth = temporal_median([item.ey_depth for item in frames])
    j3_depth = temporal_median([item.j3_depth for item in frames])
    volume = o3d.pipelines.integration.ScalableTSDFVolume(
        voxel_length=voxel,
        sdf_trunc=truncation,
        color_type=o3d.pipelines.integration.TSDFVolumeColorType.RGB8,
    )
    volume.integrate(
        make_rgbd(ey_depth, frames[-1].ey_color),
        ey_projector.intrinsic,
        np.eye(4),
    )
    volume.integrate(
        make_rgbd(j3_depth, frames[-1].j3_color),
        j3_projector.intrinsic,
        np.linalg.inv(j3_to_ey),
    )
    mesh = volume.extract_triangle_mesh()
    mesh.compute_vertex_normals()
    return mesh


def build_single_mesh_cpu(
    depth: np.ndarray,
    color: np.ndarray,
    projector: CameraProjector,
    extrinsic: np.ndarray,
    voxel: float,
    truncation: float,
) -> o3d.geometry.TriangleMesh:
    volume = o3d.pipelines.integration.ScalableTSDFVolume(
        voxel_length=voxel,
        sdf_trunc=truncation,
        color_type=o3d.pipelines.integration.TSDFVolumeColorType.RGB8,
    )
    volume.integrate(
        make_rgbd(depth, color),
        projector.intrinsic,
        extrinsic,
    )
    return volume.extract_triangle_mesh()


def create_cuda_volume(
    voxel: float,
    device: o3d.core.Device,
    block_count: int,
) -> o3d.t.geometry.VoxelBlockGrid:
    dtype = o3d.core.Dtype
    return o3d.t.geometry.VoxelBlockGrid(
        attr_names=("tsdf", "weight", "color"),
        attr_dtypes=(dtype.Float32, dtype.Float32, dtype.Float32),
        attr_channels=((1), (1), (3)),
        voxel_size=voxel,
        block_resolution=16,
        block_count=block_count,
        device=device,
    )


def integrate_cuda_camera(
    volume: o3d.t.geometry.VoxelBlockGrid,
    depth_np: np.ndarray,
    color_np: np.ndarray,
    intrinsic_np: np.ndarray,
    extrinsic_np: np.ndarray,
    truncation_multiplier: float,
    device: o3d.core.Device,
) -> None:
    dtype = o3d.core.Dtype
    depth = o3d.t.geometry.Image(
        o3d.core.Tensor(np.ascontiguousarray(depth_np))
    ).to(device)
    color = o3d.t.geometry.Image(
        o3d.core.Tensor(np.ascontiguousarray(color_np))
    ).to(device)
    intrinsic = o3d.core.Tensor(
        np.ascontiguousarray(intrinsic_np), dtype.Float64
    )
    extrinsic = o3d.core.Tensor(
        np.ascontiguousarray(extrinsic_np), dtype.Float64
    )
    blocks = volume.compute_unique_block_coordinates(
        depth,
        intrinsic,
        extrinsic,
        1000.0,
        MAX_DEPTH_M,
        truncation_multiplier,
    )
    volume.integrate(
        blocks,
        depth,
        color,
        intrinsic,
        intrinsic,
        extrinsic,
        1000.0,
        MAX_DEPTH_M,
        truncation_multiplier,
    )


def extract_cuda_mesh(
    volume: o3d.t.geometry.VoxelBlockGrid,
) -> o3d.geometry.TriangleMesh:
    mesh = volume.extract_triangle_mesh(weight_threshold=0.5).to_legacy()
    colors = np.asarray(mesh.vertex_colors)
    if colors.size:
        np.clip(colors, 0.0, 1.0, out=colors)
    return mesh


def build_mesh_cuda(
    frames: list[PreparedPair],
    ey_projector: CameraProjector,
    j3_projector: CameraProjector,
    j3_to_ey: np.ndarray,
    voxel: float,
    truncation: float,
    device: o3d.core.Device,
    block_count: int,
) -> o3d.geometry.TriangleMesh:
    ey_depth = temporal_median([item.ey_depth for item in frames])
    j3_depth = temporal_median([item.j3_depth for item in frames])
    volume = create_cuda_volume(voxel, device, block_count)
    truncation_multiplier = max(1.0, truncation / voxel)
    camera_inputs = (
        (
            ey_depth,
            frames[-1].ey_color,
            ey_projector.depth_k,
            np.eye(4, dtype=np.float64),
        ),
        (
            j3_depth,
            frames[-1].j3_color,
            j3_projector.depth_k,
            np.linalg.inv(j3_to_ey),
        ),
    )
    for depth_np, color_np, intrinsic_np, extrinsic_np in camera_inputs:
        integrate_cuda_camera(
            volume,
            depth_np,
            color_np,
            intrinsic_np,
            extrinsic_np,
            truncation_multiplier,
            device,
        )
    o3d.core.cuda.synchronize()
    mesh = extract_cuda_mesh(volume)
    o3d.core.cuda.synchronize()
    mesh.compute_vertex_normals()
    return mesh


def merge_partial_meshes(
    ey_mesh: o3d.geometry.TriangleMesh,
    j3_mesh: o3d.geometry.TriangleMesh,
    merge_mode: str,
    weld_radius: float,
) -> tuple[o3d.geometry.TriangleMesh, float]:
    started = time.perf_counter()
    merged = ey_mesh + j3_mesh
    if merge_mode == "weld":
        merged.merge_close_vertices(weld_radius)
        merged.remove_degenerate_triangles()
        merged.remove_duplicated_triangles()
    elif merge_mode != "concatenate":
        raise ValueError(f"unknown mesh merge mode: {merge_mode}")
    merged.compute_vertex_normals()
    return merged, time.perf_counter() - started


def build_independent_meshes(
    frames: list[PreparedPair],
    ey_projector: CameraProjector,
    j3_projector: CameraProjector,
    j3_to_ey: np.ndarray,
    voxel: float,
    truncation: float,
    device: o3d.core.Device,
    block_count: int,
) -> tuple[o3d.geometry.TriangleMesh, o3d.geometry.TriangleMesh]:
    ey_depth = temporal_median([item.ey_depth for item in frames])
    j3_depth = temporal_median([item.j3_depth for item in frames])
    ey_extrinsic = np.eye(4, dtype=np.float64)
    j3_extrinsic = np.linalg.inv(j3_to_ey)
    if str(device).startswith("CUDA"):
        truncation_multiplier = max(1.0, truncation / voxel)
        partials = []
        for depth, color, projector, extrinsic in (
            (ey_depth, frames[-1].ey_color, ey_projector, ey_extrinsic),
            (j3_depth, frames[-1].j3_color, j3_projector, j3_extrinsic),
        ):
            volume = create_cuda_volume(voxel, device, block_count)
            integrate_cuda_camera(
                volume,
                depth,
                color,
                projector.depth_k,
                extrinsic,
                truncation_multiplier,
                device,
            )
            o3d.core.cuda.synchronize()
            partials.append(extract_cuda_mesh(volume))
        o3d.core.cuda.synchronize()
        return partials[0], partials[1]
    return (
        build_single_mesh_cpu(
            ey_depth,
            frames[-1].ey_color,
            ey_projector,
            ey_extrinsic,
            voxel,
            truncation,
        ),
        build_single_mesh_cpu(
            j3_depth,
            frames[-1].j3_color,
            j3_projector,
            j3_extrinsic,
            voxel,
            truncation,
        ),
    )


def build_mesh_with_metrics(
    frames: list[PreparedPair],
    ey_projector: CameraProjector,
    j3_projector: CameraProjector,
    j3_to_ey: np.ndarray,
    voxel: float,
    truncation: float,
    device: o3d.core.Device | None = None,
    block_count: int = 20000,
    fusion_mode: str = "shared-tsdf",
    merge_mode: str = "concatenate",
    weld_radius: float = 0.003,
) -> tuple[
    o3d.geometry.TriangleMesh,
    float,
    tuple[int, int],
    tuple[int, int],
]:
    if device is None:
        device = resolve_mesh_device("auto")
    if fusion_mode == "independent-merge":
        ey_mesh, j3_mesh = build_independent_meshes(
            frames,
            ey_projector,
            j3_projector,
            j3_to_ey,
            voxel,
            truncation,
            device,
            block_count,
        )
        partial_vertices = (
            len(ey_mesh.vertices),
            len(j3_mesh.vertices),
        )
        partial_triangles = (
            len(ey_mesh.triangles),
            len(j3_mesh.triangles),
        )
        mesh, merge_seconds = merge_partial_meshes(
            ey_mesh, j3_mesh, merge_mode, weld_radius
        )
        return mesh, merge_seconds, partial_vertices, partial_triangles
    if fusion_mode != "shared-tsdf":
        raise ValueError(f"unknown mesh fusion mode: {fusion_mode}")
    if str(device).startswith("CUDA"):
        mesh = build_mesh_cuda(
            frames,
            ey_projector,
            j3_projector,
            j3_to_ey,
            voxel,
            truncation,
            device,
            block_count,
        )
    else:
        mesh = build_mesh_cpu(
            frames,
            ey_projector,
            j3_projector,
            j3_to_ey,
            voxel,
            truncation,
        )
    return mesh, 0.0, (0, 0), (0, 0)


def build_mesh(
    frames: list[PreparedPair],
    ey_projector: CameraProjector,
    j3_projector: CameraProjector,
    j3_to_ey: np.ndarray,
    voxel: float,
    truncation: float,
    device: o3d.core.Device | None = None,
    block_count: int = 20000,
    fusion_mode: str = "shared-tsdf",
    merge_mode: str = "concatenate",
    weld_radius: float = 0.003,
) -> o3d.geometry.TriangleMesh:
    return build_mesh_with_metrics(
        frames,
        ey_projector,
        j3_projector,
        j3_to_ey,
        voxel,
        truncation,
        device,
        block_count,
        fusion_mode,
        merge_mode,
        weld_radius,
    )[0]


class MeshWorker:
    def __init__(
        self,
        ey_projector: CameraProjector,
        j3_projector: CameraProjector,
        transform: np.ndarray,
        voxel: float,
        truncation: float,
        output_path: Path,
        device: str = "auto",
        block_count: int = 20000,
        fusion_mode: str = "shared-tsdf",
        merge_mode: str = "concatenate",
        weld_radius: float = 0.003,
    ):
        self.ey_projector = ey_projector
        self.j3_projector = j3_projector
        self.transform = transform
        self.voxel = voxel
        self.truncation = truncation
        self.output_path = output_path
        self.device = resolve_mesh_device(device)
        self.block_count = block_count
        self.fusion_mode = fusion_mode
        self.merge_mode = merge_mode
        self.weld_radius = weld_radius
        self.requests: queue.Queue[list[PreparedPair] | None] = queue.Queue(
            maxsize=1
        )
        self.results: queue.Queue[MeshResult] = queue.Queue(maxsize=1)
        self.error: str | None = None
        self.thread = threading.Thread(target=self._run, name="mesh-builder")
        self.thread.start()

    def request(self, frames: list[PreparedPair]) -> bool:
        try:
            self.requests.put_nowait(frames)
            return True
        except queue.Full:
            return False

    def _run(self) -> None:
        while True:
            frames = self.requests.get()
            if frames is None:
                return
            try:
                started = time.perf_counter()
                (
                    mesh,
                    merge_seconds,
                    partial_vertices,
                    partial_triangles,
                ) = build_mesh_with_metrics(
                    frames,
                    self.ey_projector,
                    self.j3_projector,
                    self.transform,
                    self.voxel,
                    self.truncation,
                    self.device,
                    self.block_count,
                    self.fusion_mode,
                    self.merge_mode,
                    self.weld_radius,
                )
                build_seconds = time.perf_counter() - started
                self.output_path.parent.mkdir(parents=True, exist_ok=True)
                temporary = self.output_path.with_name(
                    self.output_path.stem + ".tmp.ply"
                )
                if o3d.io.write_triangle_mesh(
                    str(temporary),
                    mesh,
                    write_ascii=False,
                    compressed=True,
                ):
                    temporary.replace(self.output_path)
                result = MeshResult(
                    mesh=mesh,
                    source_pair=frames[-1].number,
                    input_frames=len(frames),
                    build_seconds=build_seconds,
                    backend=str(self.device),
                    fusion_mode=self.fusion_mode,
                    merge_mode=self.merge_mode,
                    merge_seconds=merge_seconds,
                    partial_vertices=partial_vertices,
                    partial_triangles=partial_triangles,
                )
                while True:
                    try:
                        self.results.get_nowait()
                    except queue.Empty:
                        break
                self.results.put_nowait(result)
            except Exception as error:
                self.error = repr(error)
                print(f"Mesh worker error: {error}", flush=True)

    def close(self) -> None:
        while True:
            try:
                self.requests.get_nowait()
            except queue.Empty:
                break
        self.requests.put(None)
        self.thread.join(timeout=15.0)
        if self.thread.is_alive():
            print("Mesh worker did not stop within 15 seconds", flush=True)


class FrameServer:
    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.stop = threading.Event()
        self.frames: queue.Queue[DecodedPair] = queue.Queue(maxsize=1)
        self.thread = threading.Thread(
            target=self._run, name="rgbd-receiver", daemon=True
        )
        self.decompressor = zstandard.ZstdDecompressor()
        self.received = 0
        self.display_dropped = 0
        self.connections = 0
        self.protocol_errors = 0
        self.sync_errors: list[int] = []
        self.started = time.perf_counter()
        self.ready = threading.Event()
        self.fatal_error: str | None = None

    def start(self) -> None:
        self.thread.start()
        if not self.ready.wait(10.0):
            raise RuntimeError("receiver did not become ready")
        if self.fatal_error:
            raise RuntimeError(self.fatal_error)

    def _validate_and_decode(
        self, frame: protocol.ReceivedFrame
    ) -> DecodedPair:
        expected = {
            (EY_SERIAL, protocol.STREAM_COLOR),
            (EY_SERIAL, protocol.STREAM_DEPTH),
            (J3_SERIAL, protocol.STREAM_COLOR),
            (J3_SERIAL, protocol.STREAM_DEPTH),
        }
        if (
            {(item.serial, item.stream_type) for item in frame.payloads}
            != expected
            or len(frame.payloads) != 4
        ):
            raise protocol.ProtocolError("unexpected payload set")
        if not (
            frame.flags & protocol.FLAG_HARDWARE_SYNC
            and frame.flags & protocol.FLAG_DEVICE_TIMESTAMPS
        ):
            raise protocol.ProtocolError("required synchronization flags absent")
        calculated = (
            frame.j3_timestamp_us
            - frame.ey_timestamp_us
            - self.args.delay_usec
        )
        if calculated != frame.sync_error_us:
            raise protocol.ProtocolError("inconsistent synchronization metadata")
        if abs(calculated) > self.args.sync_tolerance_usec:
            raise protocol.ProtocolError("frame exceeds synchronization tolerance")

        values: dict[tuple[str, int], bytes | np.ndarray] = {}
        for item in frame.payloads:
            key = (item.serial, item.stream_type)
            if item.stream_type == protocol.STREAM_COLOR:
                if (
                    item.codec != protocol.CODEC_MJPEG
                    or item.width != 1280
                    or item.height != 720
                    or not item.data.startswith(b"\xff\xd8")
                ):
                    raise protocol.ProtocolError("invalid color payload")
                values[key] = item.data
            elif item.stream_type == protocol.STREAM_DEPTH:
                if (
                    item.codec != protocol.CODEC_ZSTD
                    or item.width != WIDTH
                    or item.height != HEIGHT
                    or item.raw_length != DEPTH_BYTES
                ):
                    raise protocol.ProtocolError("invalid depth payload")
                raw = self.decompressor.decompress(
                    item.data, max_output_size=DEPTH_BYTES
                )
                if len(raw) != DEPTH_BYTES:
                    raise protocol.ProtocolError("invalid decompressed depth size")
                values[key] = np.frombuffer(raw, dtype="<u2").reshape(
                    HEIGHT, WIDTH
                ).copy()
            else:
                raise protocol.ProtocolError("unknown stream type")
        return DecodedPair(
            number=frame.pair_number,
            sync_error_us=frame.sync_error_us,
            ey_color_jpeg=values[(EY_SERIAL, protocol.STREAM_COLOR)],
            ey_depth=values[(EY_SERIAL, protocol.STREAM_DEPTH)],
            j3_color_jpeg=values[(J3_SERIAL, protocol.STREAM_COLOR)],
            j3_depth=values[(J3_SERIAL, protocol.STREAM_DEPTH)],
        )

    def _publish(self, pair: DecodedPair) -> None:
        try:
            self.frames.put_nowait(pair)
        except queue.Full:
            try:
                self.frames.get_nowait()
            except queue.Empty:
                pass
            self.frames.put_nowait(pair)
            self.display_dropped += 1

    def _handle(self, connection: socket.socket) -> None:
        connection.settimeout(self.args.socket_timeout)
        while not self.stop.is_set():
            frame = protocol.receive_frame(connection)
            pair = self._validate_and_decode(frame)
            received_ns = time.time_ns()
            connection.sendall(
                protocol.encode_ack(frame.pair_number, received_ns)
            )
            self.received += 1
            self.sync_errors.append(abs(pair.sync_error_us))
            self._publish(pair)
            if (
                self.args.max_pairs
                and self.received >= self.args.max_pairs
            ):
                self.stop.set()
                return

    def _run(self) -> None:
        server: socket.socket | None = None
        try:
            address = ipaddress.ip_address(self.args.bind)
            if not address.is_loopback and not self.args.allow_nonloopback:
                raise RuntimeError(
                    "refusing non-loopback bind without --allow-nonloopback"
                )
            server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            server.bind((self.args.bind, self.args.port))
            server.listen(1)
            server.settimeout(1.0)
            self.ready.set()
            print(
                f"Live fusion receiver listening on "
                f"{self.args.bind}:{self.args.port}",
                flush=True,
            )
            while not self.stop.is_set():
                try:
                    connection, address = server.accept()
                except socket.timeout:
                    continue
                self.connections += 1
                print(f"Sender connected from {address[0]}", flush=True)
                with connection:
                    try:
                        self._handle(connection)
                    except EOFError:
                        print("Sender disconnected; waiting for reconnect", flush=True)
                    except (
                        OSError,
                        socket.timeout,
                        protocol.ProtocolError,
                        zstandard.ZstdError,
                    ) as error:
                        self.protocol_errors += 1
                        print(f"Stream connection error: {error}", flush=True)
        except Exception as error:
            self.fatal_error = repr(error)
            self.stop.set()
            self.ready.set()
        finally:
            if server is not None:
                server.close()

    def close(self) -> None:
        self.stop.set()
        self.thread.join(timeout=2.0)

    def report(self) -> dict:
        elapsed = time.perf_counter() - self.started
        return {
            "received_pairs": self.received,
            "display_queue_replacements": self.display_dropped,
            "connections": self.connections,
            "protocol_errors": self.protocol_errors,
            "elapsed_seconds": elapsed,
            "receive_rate_hz": self.received / elapsed if elapsed else 0.0,
            "absolute_sync_error_median_us": (
                statistics.median(self.sync_errors)
                if self.sync_errors
                else None
            ),
            "absolute_sync_error_max_us": (
                max(self.sync_errors) if self.sync_errors else None
            ),
            "fatal_error": self.fatal_error,
        }


class LiveViewer:
    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.stop = False
        self.mode = "both"
        self.cloud = o3d.geometry.PointCloud()
        self.mesh = o3d.geometry.TriangleMesh()
        self.cloud_added = False
        self.mesh_added = False
        self.view_initialized = False
        self.visualizer: o3d.visualization.VisualizerWithKeyCallback | None = None

    def request_stop(self, *_args) -> None:
        self.stop = True

    def create(self) -> None:
        if self.args.headless:
            return
        visualizer = o3d.visualization.VisualizerWithKeyCallback()
        if not visualizer.create_window(
            window_name="Two-Camera Femto Bolt Live Fusion",
            width=1440,
            height=900,
        ):
            raise RuntimeError(
                "unable to create Open3D window; use --headless for diagnostics"
            )
        self.visualizer = visualizer
        options = visualizer.get_render_option()
        options.background_color = np.asarray([0.025, 0.03, 0.045])
        options.point_size = self.args.point_size
        options.mesh_show_back_face = True
        visualizer.register_key_callback(ord("1"), self._point_mode)
        visualizer.register_key_callback(ord("2"), self._mesh_mode)
        visualizer.register_key_callback(ord("3"), self._both_mode)
        visualizer.register_key_callback(ord("S"), self._save)
        print(
            "Viewer keys: 1 point cloud, 2 mesh, 3 both, "
            "S save current outputs, Q/Esc quit",
            flush=True,
        )

    def _set_mode(self, mode: str) -> bool:
        self.mode = mode
        if self.visualizer is None:
            return False
        show_cloud = mode in ("points", "both")
        show_mesh = mode in ("mesh", "both")
        if show_cloud and not self.cloud_added and len(self.cloud.points):
            self.visualizer.add_geometry(self.cloud, reset_bounding_box=False)
            self.cloud_added = True
        elif not show_cloud and self.cloud_added:
            self.visualizer.remove_geometry(
                self.cloud, reset_bounding_box=False
            )
            self.cloud_added = False
        if show_mesh and not self.mesh_added and len(self.mesh.triangles):
            self.visualizer.add_geometry(self.mesh, reset_bounding_box=False)
            self.mesh_added = True
        elif not show_mesh and self.mesh_added:
            self.visualizer.remove_geometry(
                self.mesh, reset_bounding_box=False
            )
            self.mesh_added = False
        return False

    def _point_mode(self, _visualizer) -> bool:
        return self._set_mode("points")

    def _mesh_mode(self, _visualizer) -> bool:
        return self._set_mode("mesh")

    def _both_mode(self, _visualizer) -> bool:
        return self._set_mode("both")

    def _save(self, _visualizer) -> bool:
        self.save_cloud()
        print("Saved current full-scene point cloud and latest mesh", flush=True)
        return False

    def save_cloud(self) -> None:
        self.args.output_dir.mkdir(parents=True, exist_ok=True)
        o3d.io.write_point_cloud(
            str(self.args.output_dir / "latest_live_full_scene_pointcloud.ply"),
            self.cloud,
            write_ascii=False,
            compressed=True,
        )

    def update_cloud(self, value: o3d.geometry.PointCloud) -> None:
        self.cloud.points = value.points
        self.cloud.colors = value.colors
        if self.visualizer is None:
            return
        if self.mode in ("points", "both"):
            if not self.cloud_added:
                self.visualizer.add_geometry(
                    self.cloud, reset_bounding_box=not self.view_initialized
                )
                self.cloud_added = True
            else:
                self.visualizer.update_geometry(self.cloud)
            if not self.view_initialized:
                self.visualizer.reset_view_point(True)
                self.view_initialized = True

    def update_mesh(self, value: o3d.geometry.TriangleMesh) -> None:
        if self.visualizer is not None and self.mesh_added:
            self.visualizer.remove_geometry(
                self.mesh, reset_bounding_box=False
            )
            self.mesh_added = False
        self.mesh.vertices = value.vertices
        self.mesh.triangles = value.triangles
        self.mesh.vertex_colors = value.vertex_colors
        self.mesh.vertex_normals = value.vertex_normals
        if (
            self.visualizer is not None
            and self.mode in ("mesh", "both")
            and len(self.mesh.triangles)
        ):
            self.visualizer.add_geometry(
                self.mesh, reset_bounding_box=False
            )
            self.mesh_added = True

    def poll(self) -> bool:
        if self.visualizer is None:
            return not self.stop
        if not self.visualizer.poll_events():
            self.stop = True
            return False
        self.visualizer.update_renderer()
        return not self.stop

    def close(self) -> None:
        if self.visualizer is not None:
            self.visualizer.destroy_window()


def load_transform(path: Path) -> np.ndarray:
    if path.suffix.lower() == ".json":
        data = json.loads(path.read_text())
        for key in (
            "global_j3_depth_to_ey_depth",
            "j3_depth_to_ey_depth",
        ):
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


def prepare_pair(
    pair: DecodedPair,
    ey: CameraProjector,
    j3: CameraProjector,
    ey_color_bgr: np.ndarray,
    j3_color_bgr: np.ndarray,
) -> PreparedPair:
    ey_depth, ey_color = ey.prepare_from_bgr(pair.ey_depth, ey_color_bgr)
    j3_depth, j3_color = j3.prepare_from_bgr(pair.j3_depth, j3_color_bgr)
    return PreparedPair(
        number=pair.number,
        sync_error_us=pair.sync_error_us,
        ey_depth=ey_depth,
        ey_color=ey_color,
        j3_depth=j3_depth,
        j3_color=j3_color,
    )


def parse_args() -> argparse.Namespace:
    # Capture and calibration data live outside the repository. Point
    # FOURD_CAPTURE_ROOT at the directory holding your capture set, and
    # FOURD_CALIBRATION_DIR at the calibration directory inside it, or pass
    # --ey-factory/--j3-factory/--j3-to-ey explicitly.
    root = Path(os.environ.get("FOURD_CAPTURE_ROOT", Path.cwd()))
    calibration = Path(
        os.environ.get("FOURD_CALIBRATION_DIR", root / "calibration_2026-07-29")
    )
    parser = argparse.ArgumentParser()
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=17000)
    parser.add_argument("--allow-nonloopback", action="store_true")
    parser.add_argument("--socket-timeout", type=float, default=10.0)
    parser.add_argument("--delay-usec", type=int, default=160)
    parser.add_argument("--sync-tolerance-usec", type=int, default=3000)
    parser.add_argument("--max-pairs", type=int, default=0)
    parser.add_argument("--headless", action="store_true")
    parser.add_argument("--log-every", type=int, default=10)
    parser.add_argument("--point-stride", type=int, choices=(1, 2, 3, 4), default=2)
    parser.add_argument("--point-voxel", type=float, default=0.004)
    parser.add_argument("--point-size", type=float, default=2.0)
    parser.add_argument("--mesh-window", type=int, default=7)
    parser.add_argument("--mesh-interval", type=float, default=4.0)
    parser.add_argument("--mesh-voxel", type=float, default=0.006)
    parser.add_argument("--mesh-truncation", type=float, default=0.03)
    parser.add_argument(
        "--mesh-device",
        default=os.environ.get("FOURD_MESH_DEVICE", "auto"),
        help="TSDF mesh device: auto, CPU:0, CUDA:0, CUDA:1, ...",
    )
    parser.add_argument("--mesh-block-count", type=int, default=20000)
    parser.add_argument(
        "--mesh-fusion-mode",
        choices=("shared-tsdf", "independent-merge"),
        default="independent-merge",
        help="shared TSDF or one TSDF per camera followed by explicit merge",
    )
    parser.add_argument(
        "--mesh-merge-mode",
        choices=("concatenate", "weld"),
        default="concatenate",
        help="independent mesh merge; concatenate preserves all geometry",
    )
    parser.add_argument("--mesh-weld-radius", type=float, default=0.003)
    parser.add_argument(
        "--ey-factory",
        type=Path,
        default=(
            calibration
            / "source"
            / "work"
            / "calibration_stepwise"
            / "factory"
            / "ey_factory_calibration.json"
        ),
    )
    parser.add_argument(
        "--j3-factory",
        type=Path,
        default=(
            calibration
            / "source"
            / "work"
            / "calibration_stepwise"
            / "factory"
            / "j3_factory_calibration.json"
        ),
    )
    parser.add_argument(
        "--j3-to-ey",
        type=Path,
        default=(
            calibration
            / "final_validated_fusion"
            / "j3_depth_to_ey_depth_refined.txt"
        ),
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=root / "live_fusion_output",
    )
    args = parser.parse_args()
    if not 1 <= args.mesh_window <= 31:
        parser.error("--mesh-window must be between 1 and 31")
    if args.mesh_block_count < 1000:
        parser.error("--mesh-block-count must be at least 1000")
    if args.mesh_weld_radius <= 0:
        parser.error("--mesh-weld-radius must be positive")
    return args


def main() -> None:
    args = parse_args()
    for path in (args.ey_factory, args.j3_factory, args.j3_to_ey):
        if not path.is_file():
            raise SystemExit(f"required calibration file not found: {path}")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    transform = load_transform(args.j3_to_ey)
    ey_projector = CameraProjector(args.ey_factory)
    j3_projector = CameraProjector(args.j3_factory)
    mesh_path = args.output_dir / "latest_live_full_scene_mesh.ply"
    mesh_worker = MeshWorker(
        ey_projector,
        j3_projector,
        transform,
        args.mesh_voxel,
        args.mesh_truncation,
        mesh_path,
        args.mesh_device,
        args.mesh_block_count,
        args.mesh_fusion_mode,
        args.mesh_merge_mode,
        args.mesh_weld_radius,
    )
    server = FrameServer(args)
    viewer = LiveViewer(args)
    signal.signal(signal.SIGINT, viewer.request_stop)
    signal.signal(signal.SIGTERM, viewer.request_stop)
    history: deque[PreparedPair] = deque(maxlen=args.mesh_window)
    processed = 0
    latest_pair = 0
    last_mesh_request = 0.0
    last_history_sample = 0.0
    last_mesh_pair = 0
    mesh_updates = 0
    processing_times: list[float] = []
    try:
        viewer.create()
        server.start()
        while viewer.poll():
            if server.fatal_error:
                raise RuntimeError(server.fatal_error)
            if mesh_worker.error:
                raise RuntimeError(f"mesh worker failed: {mesh_worker.error}")
            try:
                decoded = server.frames.get(timeout=0.01)
            except queue.Empty:
                decoded = None
            if decoded is not None:
                started = time.perf_counter()
                ey_color_bgr = ey_projector.decode_color(
                    decoded.ey_color_jpeg
                )
                j3_color_bgr = j3_projector.decode_color(
                    decoded.j3_color_jpeg
                )
                cloud = make_fused_cloud(
                    decoded,
                    ey_color_bgr,
                    j3_color_bgr,
                    ey_projector,
                    j3_projector,
                    transform,
                    args.point_stride,
                    args.point_voxel,
                )
                viewer.update_cloud(cloud)
                processed += 1
                latest_pair = decoded.number
                now = time.monotonic()
                history_sample_period = max(
                    0.2, args.mesh_interval / args.mesh_window
                )
                if (
                    len(history) < args.mesh_window
                    or now - last_history_sample >= history_sample_period
                ):
                    prepared = prepare_pair(
                        decoded,
                        ey_projector,
                        j3_projector,
                        ey_color_bgr,
                        j3_color_bgr,
                    )
                    history.append(prepared)
                    last_history_sample = now
                processing_times.append(time.perf_counter() - started)
                if (
                    len(history) >= args.mesh_window
                    and now - last_mesh_request >= args.mesh_interval
                    and mesh_worker.request(list(history))
                ):
                    last_mesh_request = now
                    last_mesh_pair = latest_pair
                if processed % args.log_every == 0:
                    print(
                        json.dumps(
                            {
                                "processed_pairs": processed,
                                "latest_pair": latest_pair,
                                "live_points": len(cloud.points),
                                "sync_error_us": decoded.sync_error_us,
                                "mean_prepare_and_cloud_ms": (
                                    1000.0
                                    * statistics.mean(
                                        processing_times[-args.log_every :]
                                    )
                                ),
                                "last_mesh_source_pair": last_mesh_pair,
                                "mesh_updates": mesh_updates,
                            }
                        ),
                        flush=True,
                    )
            try:
                result = mesh_worker.results.get_nowait()
            except queue.Empty:
                result = None
            if result is not None:
                viewer.update_mesh(result.mesh)
                mesh_updates += 1
                print(
                    json.dumps(
                        {
                            "mesh_update": mesh_updates,
                            "source_pair": result.source_pair,
                            "temporal_frames": result.input_frames,
                            "vertices": len(result.mesh.vertices),
                            "triangles": len(result.mesh.triangles),
                            "build_seconds": result.build_seconds,
                            "backend": result.backend,
                            "fusion_mode": result.fusion_mode,
                            "merge_mode": result.merge_mode,
                            "merge_seconds": result.merge_seconds,
                            "partial_vertices": result.partial_vertices,
                            "partial_triangles": result.partial_triangles,
                            "saved": str(mesh_path),
                        }
                    ),
                    flush=True,
                )
            if (
                args.max_pairs
                and server.received >= args.max_pairs
                and server.frames.empty()
            ):
                break
            if args.headless and server.stop.is_set() and server.frames.empty():
                break
        viewer.save_cloud()
    finally:
        server.close()
        mesh_worker.close()
        viewer.close()
        report = {
            "status": "completed",
            "full_scene": True,
            "spatial_crop": False,
            "component_filtering": False,
            "geometry_decimation": False,
            "processed_pairs": processed,
            "latest_pair": latest_pair,
            "mesh_updates": mesh_updates,
            "mesh_device": str(mesh_worker.device),
            "mesh_fusion_mode": mesh_worker.fusion_mode,
            "mesh_merge_mode": mesh_worker.merge_mode,
            "latest_pointcloud": str(
                args.output_dir / "latest_live_full_scene_pointcloud.ply"
            ),
            "latest_mesh": str(mesh_path),
            "receiver": server.report(),
        }
        (args.output_dir / "live_fusion_report.json").write_text(
            json.dumps(report, indent=2), encoding="utf-8"
        )
        print(json.dumps(report, indent=2), flush=True)


if __name__ == "__main__":
    main()
