#!/usr/bin/env python3
"""Exercise optional features from a wheel installed outside the source checkout."""

from __future__ import annotations

import argparse
import importlib
import json
import os
import platform
from pathlib import Path
import sys
import sysconfig
import tempfile

import numpy as np

import open4d
from open4d.demo import mesh_sequence


def tools(directory: Path) -> None:
    from open4d.io import open_sequence, write_sequence

    with mesh_sequence(side=4, frames=1) as source:
        for format in ("off", "glb", "stl"):
            path = write_sequence(source, directory / f"mesh.{format}", allow_lossy=True)
            with open_sequence(path) as restored:
                assert len(restored) == 1
                assert len(restored[0].geometry.triangles) == len(source[0].geometry.triangles)
                assert np.isfinite(restored[0].geometry.positions).all()


def usd(directory: Path) -> None:
    with mesh_sequence(side=4, frames=3) as source:
        path = open4d.save(source, directory / "mesh.usdc")
        with open4d.load(path) as restored:
            assert restored.timestamps == source.timestamps
            for expected, actual in zip(source, restored, strict=True):
                np.testing.assert_array_equal(actual.geometry.positions, expected.geometry.positions)
                np.testing.assert_array_equal(actual.geometry.triangles, expected.geometry.triangles)


def metrics(directory: Path) -> None:
    with mesh_sequence(side=4, frames=2) as source:
        assert open4d.compare_sequences(source, source).symmetric_rms == 0


def gaussians(directory: Path) -> None:
    from plyfile import PlyData, PlyElement

    names = ["x", "y", "z", "f_dc_0", "f_dc_1", "f_dc_2", "opacity",
             "scale_0", "scale_1", "scale_2", "rot_0", "rot_1", "rot_2", "rot_3"]
    data = np.zeros(5, dtype=[(name, "f4") for name in names])
    data["z"], data["rot_0"] = 1, 1
    path = directory / "gaussians.ply"
    PlyData([PlyElement.describe(data, "vertex")]).write(str(path))
    splats = open4d.load_gaussians(path)
    assert len(splats) == 5 and splats.spherical_harmonics.shape == (5, 1, 3)
    np.testing.assert_array_equal(splats.positions[:, 2], 1)
    np.testing.assert_allclose(splats.scales, 1)
    np.testing.assert_allclose(splats.opacities, 0.5)
    # ORBIT image loading shares the Gaussian extra's Pillow dependency.
    from PIL import Image

    image = directory / "camera.png"
    Image.fromarray(np.full((8, 12, 3), 128, np.uint8)).save(image)
    with Image.open(image) as restored:
        assert np.asarray(restored).shape == (8, 12, 3)


def open3d_feature(directory: Path) -> None:
    import open3d as o3d
    from open4d.integrations.open3d import frame_to_open3d

    with mesh_sequence(side=4, frames=1) as source:
        mesh = frame_to_open3d(source[0].geometry)
        assert isinstance(mesh, o3d.geometry.TriangleMesh)
        assert len(mesh.triangles) == len(source[0].geometry.triangles)
    depth = np.full((2, 48, 64), 1000, dtype=np.uint16)
    with open4d.reconstruct(depth, intrinsics=(60, 60, 31.5, 23.5), device="cpu") as source:
        assert len(source) == 2
        for frame in source:
            assert len(frame.geometry.triangles) > 0
            assert abs(np.median(frame.geometry.positions[:, 2]) - 1) < 0.05


def torch_feature(directory: Path) -> None:
    import torch
    from open4d.torch_ops import face_normals, load_obj, save_obj, vertex_normals

    vertices = torch.tensor([[0., 0, 0], [1, 0, 0], [0, 1, 0]])
    faces = torch.tensor([[0, 1, 2]])
    np.testing.assert_allclose(face_normals(vertices, faces).numpy(), [[0, 0, 1]])
    np.testing.assert_allclose(vertex_normals(vertices, faces).numpy(), [[0, 0, 1]] * 3)
    path = directory / "torch.obj"
    save_obj(path, vertices, faces)
    restored, indices, _ = load_obj(path)
    torch.testing.assert_close(restored, vertices)
    assert torch.equal(indices.verts_idx, faces)


def capture(directory: Path) -> None:
    import cv2
    import zstandard

    calibration = directory / "calibration"
    factory = calibration / "source/work/calibration_stepwise/factory"
    factory.mkdir(parents=True)
    cameras = []
    for purpose, width, height, fx, fy, cx, cy in (
        ("Depth", 1024, 1024, 0.5, 0.5, 0.5, 468 / 1024),
        ("PhotoVideo", 3840, 2160, 0.25, 320 / 720, 0.5, 0.5),
    ):
        cameras.append({
            "Purpose": f"CALIBRATION_CameraPurpose{purpose}",
            "SensorWidth": width, "SensorHeight": height,
            "Intrinsics": {"ModelParameters": [cx, cy, fx, fy] + [0.] * 11},
            "Rt": {"Rotation": np.eye(3).ravel().tolist(), "Translation": [0., 0., 0.]},
        })
    for camera in ("ey", "j3"):
        (factory / f"{camera}_factory_calibration.json").write_text(
            json.dumps({"CalibrationInformation": {"Cameras": cameras}}), encoding="utf-8")
    transform = calibration / "final_validated_fusion/j3_depth_to_ey_depth_refined.txt"
    transform.parent.mkdir()
    np.savetxt(transform, np.eye(4))
    replay = directory / "capture"
    serials = ("CAMERA1", "CAMERA2")
    for number in range(2):
        pair = replay / f"pair_{number:012d}"
        pair.mkdir(parents=True)
        payloads = []
        for serial in serials:
            depth = np.full((576, 640), 1000, dtype="<u2")
            image = np.full((720, 1280, 3), (30, 60, 90), dtype=np.uint8)
            ok, encoded = cv2.imencode(".jpg", image)
            assert ok
            for name, payload, codec, size, format_, raw_length in (
                ("depth.zst", zstandard.ZstdCompressor().compress(depth.tobytes()),
                 2, (640, 576), 4, depth.nbytes),
                ("color.jpg", encoded.tobytes(), 1, (1280, 720), 0, len(encoded)),
            ):
                file = f"{serial}_{name}"
                (pair / file).write_bytes(payload)
                payloads.append(dict(serial=serial, file=file, codec=codec, format=format_,
                                     stream_type=2 if codec == 2 else 1,
                                     width=size[0], height=size[1], raw_length=raw_length,
                                     compressed_length=len(payload)))
        (pair / "metadata.json").write_text(json.dumps(dict(
            pair_number=number, ey_timestamp_us=1_000_000 + number * 50_000,
            sync_error_us=0, payloads=payloads)), encoding="utf-8")
    source = open4d.load_rgbd_capture(replay, calibration, serials=serials)
    assert len(source) == 2 and source.depth.shape == (2, 2, 576, 640)
    assert source.timestamps == (0., 0.05)
    np.testing.assert_array_equal(source.depth[:, :, 300, 300], 1000)
    np.testing.assert_allclose(source.color[0, 0, 300, 300], (90, 60, 30), atol=3)


def player(directory: Path, render: bool) -> None:
    from open4d.visualization import render_gif
    from open4d.visualization._qt import check_available

    check_available(gif=True)
    if not render:
        return
    from PIL import Image

    with mesh_sequence(side=16, frames=4, fps=10) as source:
        output = render_gif(source, directory / "wave.gif", up="z", width=320, height=240,
                            distance=1.8, elevation=30, no_metrics=True)
    with Image.open(output) as image:
        assert image.size == (320, 240) and image.n_frames == 4
        previous = None
        for index in range(image.n_frames):
            image.seek(index)
            pixels = np.asarray(image.convert("RGB"))
            assert 0.01 < np.any(pixels < 240, axis=2).mean() < 0.8
            if previous is not None:
                assert np.any(np.abs(pixels.astype(int) - previous) > 8, axis=2).mean() > 0.005
            previous = pixels.astype(int)


def research(component: str, directory: Path) -> None:
    from open4d.codec import CodecError

    dependencies = {
        "klt": ("torch", "trimesh", "zstd", "zstandard", "tqdm", "point_cloud_utils"),
        "n4mc": ("torch", "trimesh", "skimage", "point_cloud_utils"),
        "qndf": ("torch", "open3d", "trimesh", "tqdm"),
    }
    for module in dependencies[component]:
        importlib.import_module(module)
    saved = os.environ.pop("OPEN4D_RESEARCH_ROOT", None)
    try:
        with mesh_sequence(side=4, frames=2) as source:
            try:
                open4d.encode(source, directory / f"{component}.o4d", codec=component)
            except CodecError as error:
                assert "OPEN4D_RESEARCH_ROOT" in str(error), error
            else:
                raise AssertionError("research implementations must remain outside the wheel")
    finally:
        if saved is not None:
            os.environ["OPEN4D_RESEARCH_ROOT"] = saved


FEATURES = {"tools": tools, "usd": usd, "metrics": metrics, "gaussians": gaussians,
            "open3d": open3d_feature, "torch": torch_feature, "capture": capture}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("extras", nargs="+", choices=[*FEATURES, "player", "klt", "n4mc", "qndf", "all"])
    parser.add_argument("--render", action="store_true", help="require actual GIF rendering with OpenGL")
    args = parser.parse_args()
    installed = Path(open4d.__file__).resolve()
    assert installed.is_relative_to(Path(sysconfig.get_path("purelib")).resolve()), installed
    extras = args.extras
    if "all" in extras:
        extras = [name for name in FEATURES if name != "open3d"] + ["player", "klt", "n4mc"]
        if sys.version_info < (3, 13) and platform.machine() != "aarch64":
            extras += ["open3d", "qndf"]
    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        for extra in dict.fromkeys(extras):
            if extra == "player":
                player(directory, args.render)
            elif extra in {"klt", "n4mc", "qndf"}:
                research(extra, directory)
            else:
                FEATURES[extra](directory)
            print(f"PASS installed {extra}", flush=True)


if __name__ == "__main__":
    main()
