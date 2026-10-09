import json
import re
import sys

import numpy as np
import pytest

import open4d
from open4d.reconstruction.rgbd import RGBDCapture, load_capture

pytest.importorskip("cv2")
pytest.importorskip("open3d")
pytest.importorskip("zstandard")

import cv2  # noqa: E402
import zstandard  # noqa: E402

pytestmark = pytest.mark.open3d

WIDTH, HEIGHT = 640, 576
SERIALS = ("CAMERA1", "CAMERA2")
# Rectified depth intrinsics produced by the factory files below.
INTRINSICS = (512.0, 512.0, 319.5, 287.5)
RED, BLUE = (220, 30, 30), (30, 30, 220)


def factory_entry(purpose, width, height, fx, fy, cx, cy):
    # Brown-Conrady parameters in the order the live receiver reads them,
    # normalized by the sensor size; distortion is zero.
    return {
        "Purpose": purpose,
        "SensorWidth": width,
        "SensorHeight": height,
        "Intrinsics": {"ModelParameters": [cx, cy, fx, fy] + [0.0] * 11},
        "Rt": {"Rotation": np.eye(3).ravel().tolist(), "Translation": [0.0, 0.0, 0.0]},
    }


def rotation_y(degrees):
    angle = np.radians(degrees)
    matrix = np.eye(4)
    matrix[[0, 0, 2, 2], [0, 2, 0, 2]] = (np.cos(angle), np.sin(angle),
                                          -np.sin(angle), np.cos(angle))
    return matrix


def write_calibration(root, transform):
    factory = root / "source/work/calibration_stepwise/factory"
    factory.mkdir(parents=True)
    data = {"CalibrationInformation": {"Cameras": [
        # NFOV unbinned: a 1024x1024 sensor cropped by (192, 180) to 640x576.
        factory_entry("CALIBRATION_CameraPurposeDepth", 1024, 1024,
                      0.5, 0.5, 0.5, 468 / 1024),
        # 3840x2160 colour scaled to 1280x720; a wider view than depth.
        factory_entry("CALIBRATION_CameraPurposePhotoVideo", 3840, 2160,
                      0.25, 320 / 720, 0.5, 0.5),
    ]}}
    for name in ("ey", "j3"):
        (factory / f"{name}_factory_calibration.json").write_text(json.dumps(data))
    (root / "final_validated_fusion").mkdir()
    np.savetxt(root / "final_validated_fusion/j3_depth_to_ey_depth_refined.txt", transform)
    return root


def depth_image(millimetres):
    depth = np.full((HEIGHT, WIDTH), millimetres, dtype=np.uint16)
    depth[:10, :10] = 0
    return depth


def jpeg(rgb):
    image = np.empty((720, 1280, 3), dtype=np.uint8)
    image[:] = rgb[::-1]
    return cv2.imencode(".jpg", image)[1].tobytes()


def stamp(number):
    return 1_000_000 + number * 50_000


def pair_metadata(number, payloads=()):
    return {"pair_number": number, "sender_wallclock_ns": 0, "ey_timestamp_us": stamp(number),
            "j3_timestamp_us": stamp(number) + 160 + number, "sync_error_us": number,
            "payloads": list(payloads)}


def write_replay_pair(root, number, depths=(1000, 1500)):
    pair = root / f"pair_{number:012d}"
    pair.mkdir(parents=True)
    payloads = []
    for serial, millimetres, rgb in zip(SERIALS, depths, (RED, BLUE)):
        depth = zstandard.ZstdCompressor().compress(depth_image(millimetres).astype("<u2").tobytes())
        color = jpeg(rgb)
        for kind, data, codec, size, format_ in (
                ("depth.zst", depth, 2, (WIDTH, HEIGHT), 4),
                ("color.jpg", color, 1, (1280, 720), 0)):
            name = f"{serial}_{kind}"
            (pair / name).write_bytes(data)
            payloads.append({
                "serial": serial, "stream_type": 2 if codec == 2 else 1, "codec": codec,
                "width": size[0], "height": size[1], "format": format_,
                "raw_length": WIDTH * HEIGHT * 2 if codec == 2 else len(data),
                "compressed_length": len(data), "device_timestamp_us": stamp(number),
                "file": name,
            })
    (pair / "metadata.json").write_text(json.dumps(pair_metadata(number, payloads)))
    return pair


def write_raw_pair(root, number, depths=(1000, 1500)):
    pair = root / f"pair_{number:012d}"
    pair.mkdir(parents=True)
    for prefix, millimetres, rgb in zip(("ey", "j3"), depths, (RED, BLUE)):
        (pair / f"{prefix}_depth_u16le.raw").write_bytes(
            depth_image(millimetres).astype("<u2").tobytes())
        (pair / f"{prefix}_color.jpg").write_bytes(jpeg(rgb))
    return pair


@pytest.fixture
def transform():
    matrix = rotation_y(30)
    matrix[:3, 3] = (0.5, -0.02, 0.1)
    return matrix


@pytest.fixture
def calibration(tmp_path, transform):
    return write_calibration(tmp_path / "calibration", transform)


@pytest.fixture
def replay(tmp_path):
    root = tmp_path / "replay"
    for number in (5, 6, 7):
        write_replay_pair(root, number)
    return root


def assert_images(capture, frames):
    assert isinstance(capture, RGBDCapture) and len(capture) == frames
    assert capture.depth.shape == (frames, 2, HEIGHT, WIDTH) and capture.depth.dtype == np.uint16
    assert capture.color.shape == (frames, 2, HEIGHT, WIDTH, 3) and capture.color.dtype == np.uint8
    # Zero distortion makes rectification the identity.
    assert np.array_equal(capture.depth[:, 0], np.broadcast_to(depth_image(1000), (frames, HEIGHT, WIDTH)))
    assert np.array_equal(capture.depth[:, 1], np.broadcast_to(depth_image(1500), (frames, HEIGHT, WIDTH)))
    # Colour is RGB, aligned to depth, and black where depth is missing.
    np.testing.assert_allclose(capture.color[:, 0, 300, 300], np.broadcast_to(RED, (frames, 3)), atol=4)
    np.testing.assert_allclose(capture.color[:, 1, 300, 300], np.broadcast_to(BLUE, (frames, 3)), atol=4)
    assert not capture.color[:, :, :10, :10].any()


def test_load_replay_layout(replay, calibration, transform):
    capture = load_capture(replay, calibration, serials=SERIALS)
    assert_images(capture, 3)
    np.testing.assert_allclose(capture.intrinsics, [INTRINSICS, INTRINSICS])
    assert capture.camera_poses.shape == (2, 4, 4)
    np.testing.assert_allclose(capture.camera_poses[0], np.eye(4))
    np.testing.assert_allclose(capture.camera_poses[1], transform, atol=1e-12)
    assert capture.timestamps == pytest.approx((0, 0.05, 0.1))
    assert capture.pair_numbers == (5, 6, 7)
    assert capture.sync_error_us == (5, 6, 7)
    assert capture.serials == SERIALS


def test_load_capture_default_serials_follow_live_receiver(tmp_path, calibration, monkeypatch):
    from open4d.reconstruction.rgbd._receiver import default_serials

    pairs = write_replay_pair(tmp_path / "pairs", 1).parent
    with pytest.raises(ValueError, match=rf"{re.escape(default_serials()[0])}.*serials="):
        load_capture(pairs, calibration)
    monkeypatch.setenv("FOURD_CAMERA1_SERIAL", SERIALS[0])
    monkeypatch.setenv("FOURD_CAMERA2_SERIAL", SERIALS[1])
    assert load_capture(pairs, calibration).serials == SERIALS


def live_scripts():
    """Import the source checkout's receiver scripts by path, as the receiver runs them."""
    import importlib.util
    from pathlib import Path

    root = Path(open4d.__file__).resolve().parent / "reconstruction/rgbd/python"
    if not (root / "live_two_camera_fusion.py").is_file():
        pytest.skip("needs a source checkout with reconstruction/rgbd/python")
    names = ("protocol", "live_two_camera_fusion")
    previous = {name: sys.modules.get(name) for name in names}
    modules = []
    try:
        for name in names:
            spec = importlib.util.spec_from_file_location(f"_open4d_test_{name}", root / f"{name}.py")
            module = importlib.util.module_from_spec(spec)
            # Dataclasses look their module up while the script runs, and the
            # receiver does "import protocol".
            sys.modules[spec.name] = sys.modules[name] = module
            spec.loader.exec_module(module)
            modules.append(module)
    finally:
        for name, module in previous.items():
            sys.modules.pop(f"_open4d_test_{name}", None)
            if module is None:
                sys.modules.pop(name, None)
            else:
                sys.modules[name] = module
    return modules


def test_packaged_receiver_matches_live_scripts(tmp_path, monkeypatch):
    # load_capture uses a packaged copy of the receiver's rectification so it
    # works from a wheel; it must stay identical to the scripts.
    from open4d.reconstruction.rgbd import _receiver as rx

    monkeypatch.delenv("FOURD_CAMERA1_SERIAL", raising=False)
    monkeypatch.delenv("FOURD_CAMERA2_SERIAL", raising=False)
    protocol, live = live_scripts()
    for name in ("STREAM_COLOR", "STREAM_DEPTH", "CODEC_MJPEG", "CODEC_ZSTD",
                 "FORMAT_DEPTH16_LE", "MAX_SINGLE_PAYLOAD"):
        assert getattr(rx, name) == getattr(protocol, name), name
    for name in ("WIDTH", "HEIGHT", "DEPTH_BYTES", "NFOV_UNBINNED_CROP_X", "NFOV_UNBINNED_CROP_Y"):
        assert getattr(rx, name) == getattr(live, name), name
    assert rx.default_serials() == (live.EY_SERIAL, live.J3_SERIAL)

    # Nonzero distortion, an off-centre colour camera and a depth-to-colour
    # offset exercise every term of the rectification and alignment.
    factory = factory_entry("CALIBRATION_CameraPurposeDepth", 1024, 1024,
                            0.49, 0.51, 0.502, 470 / 1024)
    factory["Intrinsics"]["ModelParameters"][4:14] = [0.08, -0.03, 0.002, 0.01, -0.004,
                                                      0.001, 0, 0, 0.0007, -0.0004]
    colour = factory_entry("CALIBRATION_CameraPurposePhotoVideo", 3840, 2160,
                           0.26, 0.45, 0.49, 0.52)
    colour["Intrinsics"]["ModelParameters"][4:6] = [0.05, -0.02]
    colour["Rt"]["Rotation"] = rotation_y(1.5)[:3, :3].ravel().tolist()
    colour["Rt"]["Translation"] = [-0.032, 0.002, 0.004]
    path = tmp_path / "factory.json"
    path.write_text(json.dumps({"CalibrationInformation": {"Cameras": [factory, colour]}}))
    rng = np.random.default_rng(7)
    depth = rng.integers(400, 3000, size=(HEIGHT, WIDTH)).astype(np.uint16)
    depth[rng.random(depth.shape) < 0.05] = 0
    bgr = rng.integers(0, 256, size=(720, 1280, 3)).astype(np.uint8)
    packaged, script = rx.CameraProjector(path), live.CameraProjector(path)
    for name in ("depth_k", "depth_d", "color_k", "color_d", "depth_to_color", "map_x", "map_y"):
        assert np.array_equal(getattr(packaged, name), getattr(script, name)), name
    for mine, theirs in zip(packaged.prepare_from_bgr(depth, bgr), script.prepare_from_bgr(depth, bgr)):
        assert mine.dtype == theirs.dtype and np.array_equal(mine, theirs)
    image = jpeg(RED)
    assert np.array_equal(rx.CameraProjector.decode_color(image), live.CameraProjector.decode_color(image))
    for bad in (b"not a jpeg", cv2.imencode(".jpg", np.zeros((10, 10, 3), np.uint8))[1].tobytes()):
        for module in (rx, live):
            with pytest.raises(RuntimeError, match="1280x720"):
                module.CameraProjector.decode_color(bad)

    transform = rotation_y(10)
    np.savetxt(tmp_path / "t.txt", transform)
    (tmp_path / "a.json").write_text(json.dumps({"global_j3_depth_to_ey_depth": transform.tolist()}))
    (tmp_path / "b.json").write_text(json.dumps({"j3_depth_to_ey_depth": transform.tolist()}))
    for name in ("t.txt", "a.json", "b.json"):
        assert np.array_equal(rx.load_transform(tmp_path / name), live.load_transform(tmp_path / name))
    for name, content in (("none.json", "{}"), ("small.txt", "1 0\n0 1\n")):
        (tmp_path / name).write_text(content)
        for module in (rx, live):
            with pytest.raises(RuntimeError):
                module.load_transform(tmp_path / name)


def test_load_capture_works_from_wheel_contents(tmp_path, replay, calibration):
    # Install exactly the files the wheel ships (no python/ scripts) and load a
    # capture from them in a fresh interpreter.
    import importlib.util
    import shutil
    import subprocess
    from pathlib import Path

    repository = Path(open4d.__file__).resolve().parents[1]
    checker = repository / "scripts/check_wheel_contents.py"
    if not checker.is_file():
        pytest.skip("needs a source checkout with scripts/check_wheel_contents.py")
    spec = importlib.util.spec_from_file_location("_open4d_test_wheel_contents", checker)
    contents = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(contents)
    site = tmp_path / "site"
    files = contents.expected_python_files()
    assert "open4d/reconstruction/rgbd/_receiver.py" in files
    assert not any("/rgbd/python/" in name for name in files)
    for name in files:
        (site / name).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(repository / name, site / name)
    script = (
        "import sys; sys.path.insert(0, sys.argv[1]); import open4d\n"
        "assert open4d.__file__.startswith(sys.argv[1]), open4d.__file__\n"
        "capture = open4d.load_rgbd_capture(sys.argv[2], sys.argv[3], serials=('CAMERA1', 'CAMERA2'))\n"
        "print(capture.pair_numbers, capture.depth.shape, int(capture.depth[0, 1, 300, 300]))\n"
    )
    result = subprocess.run([sys.executable, "-I", "-c", script, str(site), str(replay), str(calibration)],
                            cwd=tmp_path, capture_output=True, text=True, timeout=300)
    assert result.returncode == 0, result.stderr
    assert result.stdout.strip() == "(5, 6, 7) (3, 2, 576, 640) 1500"


@pytest.mark.parametrize("frames,numbers,first", [
    (range(6, 8), (6, 7), 6),
    ([5, 7], (5, 7), 5),
    ((7,), (7,), 7),
])
def test_load_capture_selects_frames(replay, calibration, frames, numbers, first):
    capture = load_capture(replay, calibration, frames=frames, serials=SERIALS)
    assert capture.pair_numbers == numbers
    assert capture.depth.shape[0] == len(numbers)
    assert capture.timestamps == pytest.approx(tuple((stamp(n) - stamp(first)) / 1e6 for n in numbers))


@pytest.mark.parametrize("frames,error,match", [
    ([8], FileNotFoundError, r"pair number\(s\) 8; available: 5\.\.7"),
    ([7, 5], ValueError, "increasing"),
    ([], ValueError, "no pairs"),
    (["5"], TypeError, "integer"),
    (5, TypeError, "range or a sequence"),
])
def test_load_capture_rejects_bad_frames(replay, calibration, frames, error, match):
    with pytest.raises(error, match=match):
        load_capture(replay, calibration, frames=frames, serials=SERIALS)


def test_load_raw_layout_with_metadata_folder(tmp_path, calibration):
    raw, metadata = tmp_path / "raw", tmp_path / "metadata"
    for number in (3, 4):
        write_raw_pair(raw, number)
        (metadata / f"pair_{number:012d}").mkdir(parents=True)
        (metadata / f"pair_{number:012d}/metadata.json").write_text(json.dumps(pair_metadata(number)))
    capture = load_capture(raw, calibration, metadata=metadata)
    assert_images(capture, 2)
    assert capture.timestamps == pytest.approx((0, 0.05))
    assert capture.sync_error_us == (3, 4)
    (metadata / "pair_000000000004/metadata.json").unlink()
    with pytest.raises(FileNotFoundError, match="no metadata for pair_000000000004"):
        load_capture(raw, calibration, metadata=metadata)


def test_load_raw_layout_without_metadata_uses_index_timing(tmp_path, calibration):
    raw = tmp_path / "raw"
    for number in (3, 4):
        write_raw_pair(raw, number)
    capture = load_capture(raw, calibration)
    assert capture.timestamps is None and capture.sync_error_us is None
    assert capture.pair_numbers == (3, 4)
    with open4d.reconstruct(capture, fps=10, voxel_size=0.05, truncation=0.15) as sequence:
        assert sequence.timestamps == (0, 0.1)
        assert sequence.metadata["fps"] == 10


@pytest.mark.parametrize("filename", ["../outside.zst", "/tmp/outside.zst"])
def test_load_capture_rejects_payload_paths_outside_pair(replay, calibration, filename):
    path = replay / "pair_000000000006/metadata.json"
    metadata = json.loads(path.read_text())
    metadata["payloads"][0]["file"] = filename
    path.write_text(json.dumps(metadata))
    with pytest.raises(ValueError, match="payload path leaves the capture pair"):
        load_capture(replay, calibration, serials=SERIALS)


def edit_payload(replay, index, **changes):
    path = replay / "pair_000000000006/metadata.json"
    metadata = json.loads(path.read_text())
    metadata["payloads"][index].update(changes)
    path.write_text(json.dumps(metadata))


@pytest.mark.parametrize("index,changes,error,match", [
    (0, {"file": "missing.zst"}, FileNotFoundError, "missing.zst is missing"),
    (0, {"compressed_length": 1}, ValueError, "metadata says 1"),
    (0, {"width": 320}, ValueError, "not zstd 16-bit 640x576 depth"),
    (1, {"codec": 2}, ValueError, "not 1280x720 MJPEG"),
    (1, {"stream_type": 2}, ValueError, "duplicate payload for CAMERA1"),
    (2, {"serial": "OTHER"}, ValueError, r"no CAMERA2 depth payload.*serials="),
])
def test_load_capture_validates_replay_payloads(replay, calibration, index, changes, error, match):
    edit_payload(replay, index, **changes)
    with pytest.raises(error, match=match):
        load_capture(replay, calibration, serials=SERIALS)


def test_load_capture_validates_payload_contents(replay, calibration):
    pair = replay / "pair_000000000006"
    (pair / "CAMERA1_depth.zst").write_bytes(zstandard.ZstdCompressor().compress(b"\0" * 100))
    edit_payload(replay, 0, compressed_length=(pair / "CAMERA1_depth.zst").stat().st_size)
    with pytest.raises(ValueError, match="decompresses to 100 bytes"):
        load_capture(replay, calibration, serials=SERIALS)


def test_load_capture_reports_missing_inputs(tmp_path, calibration, transform):
    raw = tmp_path / "raw"
    pair = write_raw_pair(raw, 1)
    (pair / "j3_color.jpg").unlink()
    with pytest.raises(FileNotFoundError, match="missing j3_color.jpg"):
        load_capture(raw, calibration)
    (pair / "j3_color.jpg").write_bytes(b"not a jpeg")
    with pytest.raises(ValueError, match="not a 1280x720 JPEG"):
        load_capture(raw, calibration)
    (pair / "j3_color.jpg").write_bytes(jpeg(BLUE))
    (pair / "ey_depth_u16le.raw").write_bytes(b"\0" * 10)
    with pytest.raises(ValueError, match="holds 10 depth bytes"):
        load_capture(raw, calibration)
    with pytest.raises(FileNotFoundError, match=r"(?s)camera 2 to camera 1 transform.*README"):
        load_capture(raw, write_calibration(tmp_path / "other", transform).parent)
    with pytest.raises(FileNotFoundError, match="no pair_<012d> directories"):
        load_capture(calibration, calibration)
    with pytest.raises(FileNotFoundError, match="capture folder not found"):
        load_capture(tmp_path / "nowhere", calibration)


def test_load_capture_rejects_nonrigid_transform(tmp_path, replay):
    calibration = write_calibration(tmp_path / "skewed", np.diag([1.0, 2.0, 1.0, 1.0]))
    with pytest.raises(ValueError, match="not a rigid transform"):
        load_capture(replay, calibration, serials=SERIALS)


def test_load_capture_snaps_rounded_transform_to_a_rotation(tmp_path, replay, transform):
    calibration = write_calibration(tmp_path / "rounded", np.round(transform, 5))
    pose = load_capture(replay, calibration, serials=SERIALS).camera_poses[1]
    np.testing.assert_allclose(pose[:3, :3] @ pose[:3, :3].T, np.eye(3), atol=1e-12)
    np.testing.assert_allclose(pose, transform, atol=1e-4)


def test_load_capture_rejects_nonmonotonic_timestamps(replay, calibration):
    path = replay / "pair_000000000007/metadata.json"
    metadata = json.loads(path.read_text())
    metadata["ey_timestamp_us"] = stamp(5)
    path.write_text(json.dumps(metadata))
    with pytest.raises(ValueError, match="do not increase at pair 7"):
        load_capture(replay, calibration, serials=SERIALS)


@pytest.fixture
def plane_capture(tmp_path):
    # Camera 2 sits 0.3 m to the right of camera 1; both see the plane z = 1 m.
    transform = np.eye(4)
    transform[0, 3] = 0.3
    calibration = write_calibration(tmp_path / "plane-calibration", transform)
    root = tmp_path / "plane"
    for number in (10, 11):
        write_replay_pair(root, number, depths=(1000, 1000))
    return load_capture(root, calibration, serials=SERIALS)


def test_reconstruct_capture_end_to_end(plane_capture):
    with open4d.reconstruct(plane_capture, voxel_size=0.02, truncation=0.06) as sequence:
        assert sequence.timestamps == pytest.approx((0, 0.05))
        assert "fps" not in sequence.metadata
        assert sequence.fps == pytest.approx(20)
        mesh = sequence[1].geometry
        assert len(mesh.triangles) > 1000
        assert mesh.positions[:, 2] == pytest.approx(1, abs=0.02)
        # Camera 2's view extends the plane to the right of camera 1's.
        assert mesh.positions[:, 0].max() > 0.3 + 0.6
        assert mesh.positions[:, 0].min() < -0.55
        right = mesh.colors[mesh.positions[:, 0] > 0.75]
        left = mesh.colors[mesh.positions[:, 0] < -0.4]
        assert right[:, 2].mean() > 0.8 and right[:, 0].mean() < 0.2
        assert left[:, 0].mean() > 0.8 and left[:, 2].mean() < 0.2


@pytest.mark.parametrize("kwargs,match", [
    ({"intrinsics": INTRINSICS}, "intrinsics come from the RGBDCapture"),
    ({"camera_poses": np.eye(4)}, "camera_poses come from"),
    ({"color": None}, "color come from"),
    ({"timestamps": (0, 1)}, "timestamps come from"),
    ({"fps": 30}, "has timestamps; omit fps"),
])
def test_reconstruct_capture_rejects_overrides(plane_capture, kwargs, match):
    with pytest.raises(TypeError, match=match):
        open4d.reconstruct(plane_capture, **kwargs)


def test_reconstruct_capture_rejects_positional_rgb(plane_capture):
    with pytest.raises(TypeError, match="rgb come from"):
        open4d.reconstruct(plane_capture, plane_capture.color)


def test_capture_is_exported_at_top_level():
    assert open4d.load_rgbd_capture is load_capture
    assert open4d.RGBDCapture is RGBDCapture
