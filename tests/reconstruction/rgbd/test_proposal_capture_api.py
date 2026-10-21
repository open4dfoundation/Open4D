"""Calibrated capture convenience APIs without optional reconstruction runtimes."""

import numpy as np
import pytest

import open4d
from open4d import _api
from open4d.core import Frame, Sequence, TriangleMesh
from open4d.demo import mesh_sequence

from open4d.reconstruction import MultiviewCapture
from open4d.reconstruction.rgbd import RGBDCapture
from open4d.reconstruction.rgbd import _capture


def test_rgbd_from_folder_forwards_explicit_calibration(monkeypatch):
    calls = []
    sentinel = object()
    def loader(*args, **kwargs):
        calls.append((args, kwargs))
        return sentinel
    monkeypatch.setattr(_capture, "load_capture", loader)
    assert RGBDCapture.from_folder("pairs", "rig", frames=[1, 2]) is sentinel
    assert calls == [(("pairs", "rig"), {"frames": [1, 2], "metadata": None, "serials": None,
                                         "camera_mode": None})]


def write_model(folder, *, camera="PINHOLE", name="frame with spaces.png"):
    folder.mkdir()
    params = "500 510 320 240" if camera == "PINHOLE" else "500 320 240 0.1"
    (folder / "cameras.txt").write_text(f"# camera\n1 {camera} 640 480 {params}\n")
    (folder / "images.txt").write_text(f"# poses\n2 1 0 0 0 1 2 3 1 {name}\n\n")


def test_colmap_pose_convention_explicit_scale_and_paths(tmp_path):
    root = tmp_path / "images"
    root.mkdir()
    (root / "frame with spaces.png").write_bytes(b"fixture")
    model = tmp_path / "model"
    write_model(model)
    capture = MultiviewCapture.from_colmap(model, root, scale_to_meters=2)
    assert len(capture) == 1
    np.testing.assert_allclose(capture.camera_poses[0, :3, 3], [-2, -4, -6])
    np.testing.assert_allclose(capture.intrinsics[0], [[500, 0, 320], [0, 510, 240], [0, 0, 1]])
    assert not capture.intrinsics.flags.writeable
    with pytest.raises(TypeError):
        MultiviewCapture.from_colmap(model, root)
    with pytest.raises(ValueError, match="scale_to_meters"):
        MultiviewCapture.from_colmap(model, root, scale_to_meters=0)


@pytest.mark.parametrize("camera,name,error", [
    ("SIMPLE_RADIAL", "frame.png", "undistort"),
    ("PINHOLE", "../outside.png", "leaves"),
])
def test_colmap_rejects_distortion_and_traversal(tmp_path, camera, name, error):
    model = tmp_path / "model"
    write_model(model, camera=camera, name=name)
    with pytest.raises(ValueError, match=error):
        MultiviewCapture.from_colmap(model, tmp_path, scale_to_meters=1)


def test_multiview_rejects_nonrigid_poses(tmp_path):
    image = tmp_path / "a.png"
    image.write_bytes(b"fixture")
    poses = np.eye(4)[None]
    poses[0, 0, 0] = 2
    with pytest.raises(ValueError, match="rigid"):
        MultiviewCapture((image,), np.eye(3)[None], poses)


def test_colmap_rotation_and_missing_observation_record(tmp_path):
    root = tmp_path / "images"
    root.mkdir()
    (root / "frame.png").write_bytes(b"fixture")
    model = tmp_path / "model"
    write_model(model, name="frame.png")
    quaternion = np.sqrt(0.5)
    pose = f"1 {quaternion} 0 0 {quaternion} 1 0 0 1 frame.png\n"
    (model / "images.txt").write_text(pose + "20 30 -1\n")
    capture = MultiviewCapture.from_colmap(model, root, scale_to_meters=1)
    np.testing.assert_allclose(capture.camera_poses[0, :3, 3], [0, 1, 0], atol=1e-12)
    np.testing.assert_allclose(capture.camera_poses[0, :3, :3],
                               [[0, 1, 0], [-1, 0, 0], [0, 0, 1]], atol=1e-12)
    (model / "images.txt").write_text(pose + pose)
    with pytest.raises(ValueError, match="observation record"):
        MultiviewCapture.from_colmap(model, root, scale_to_meters=1)


def tiny_capture():
    return RGBDCapture(
        np.ones((2, 2, 2, 2), dtype=np.uint16),
        np.zeros((2, 2, 2, 2, 3), dtype=np.uint8),
        np.array([[1, 1, 0, 0], [1, 1, 0, 0]]),
        np.tile(np.eye(4), (2, 1, 1)), (0.0, 0.2), (1, 2), None, ("a", "b"),
    )


def test_rgbd_capture_reconstructs_and_roundtrips_o4d_without_optional_runtime(tmp_path, monkeypatch):
    from open4d.reconstruction import rgbd
    capture = tiny_capture()
    owned = mesh_sequence(side=3, frames=2, fps=5)
    expected_positions = owned[1].geometry.positions.copy()
    seen = []
    def reconstruct(depth, **options):
        assert depth is capture.depth
        assert options["color"] is capture.color
        assert options["intrinsics"] is capture.intrinsics
        assert options["camera_poses"] is capture.camera_poses
        assert options["timestamps"] == capture.timestamps
        seen.append(True)
        return owned
    monkeypatch.setattr(rgbd, "reconstruct", reconstruct)
    output = tmp_path / "capture.o4d"
    assert open4d.reconstruct(capture, output, method="tsdf") == output
    assert seen == [True] and owned.closed
    with open4d.load(output) as decoded:
        assert len(decoded) == 2
        assert decoded.timestamps == (0.0, 0.2)
        np.testing.assert_array_equal(decoded[1].geometry.positions, expected_positions)


@pytest.mark.parametrize("suffix,kwargs,error", [
    ("obj", {}, ValueError), ("usdc", {"codec": "npz"}, TypeError),
    ("o4d", {"overwrite": 1}, TypeError),
])
def test_rgbd_destination_preflight_runs_before_reconstruction(tmp_path, monkeypatch, suffix, kwargs, error):
    from open4d.reconstruction import rgbd
    def expensive(*_args, **_kwargs):
        pytest.fail("reconstruction ran before invalid destination was rejected")
    monkeypatch.setattr(rgbd, "reconstruct", expensive)
    with pytest.raises(error):
        open4d.reconstruct(tiny_capture(), tmp_path / f"bad.{suffix}", **kwargs)
    existing = tmp_path / "existing.o4d"
    existing.write_bytes(b"keep")
    with pytest.raises(FileExistsError):
        open4d.reconstruct(tiny_capture(), existing)
    assert existing.read_bytes() == b"keep"


def test_rgbd_encoder_failure_closes_owned_sequence_and_keeps_existing_output(tmp_path, monkeypatch):
    from open4d.reconstruction import rgbd
    owned = mesh_sequence(side=3, frames=2)
    monkeypatch.setattr(rgbd, "reconstruct", lambda *_args, **_kwargs: owned)
    def fail(*_args, **_kwargs):
        raise RuntimeError("encoder failed")
    monkeypatch.setattr(_api, "save", fail)
    output = tmp_path / "capture.o4d"
    output.write_bytes(b"existing")
    with pytest.raises(RuntimeError, match="encoder failed"):
        open4d.reconstruct(tiny_capture(), output, overwrite=True)
    assert owned.closed and output.read_bytes() == b"existing"


@pytest.mark.parametrize("fails", [False, True])
def test_plugin_sequence_is_consumed_and_closed_before_temporary_output_cleanup(tmp_path, monkeypatch, fails):
    from open4d.reconstruction import _registry
    roots, closed = [], []
    class Provider:
        frame_count = 1
        def __init__(self, path):
            self.path = path
        def get_frame(self, index):
            assert self.path.read_bytes() == b"backend state"
            return Frame(index, 0, TriangleMesh(
                [[0.0, 0, 0], [1.0, 0, 0], [0.0, 1, 0]], [[0, 1, 2]]))
        def close(self):
            assert self.path.exists(), "temporary backend vanished before Sequence.close"
            closed.append(True)
    def plugin(source, output, **options):
        output.mkdir()
        roots.append(output)
        payload = output / "frame"
        payload.write_bytes(b"backend state")
        return Sequence(Provider(payload))
    monkeypatch.setattr(_registry, "get_method", lambda _method: plugin)
    original_encoder = _api.encode_sequence
    def encoder(sequence, destination, **options):
        assert roots[0].exists() and not sequence.closed
        sequence[0]
        if fails:
            raise RuntimeError("encoder failed")
        return original_encoder(sequence, destination, **options)
    monkeypatch.setattr(_api, "encode_sequence", encoder)
    destination = tmp_path / "plugin.o4d"
    if fails:
        with pytest.raises(RuntimeError, match="encoder failed"):
            open4d.reconstruct("input", destination, method="fake", codec="npz")
        assert not destination.exists()
    else:
        assert open4d.reconstruct("input", destination, method="fake", codec="npz") == destination
        with open4d.load(destination) as decoded:
            assert len(decoded) == 1
    assert closed == [True] and not roots[0].exists()


def test_plugin_run_metadata_survives_encoding_and_temporary_files_are_available(tmp_path, monkeypatch):
    from open4d.reconstruction import _registry
    from dataclasses import dataclass
    @dataclass
    class Run:
        path: object
        frame_numbers: tuple[int, ...]
    runs = []
    def plugin(source, output, **options):
        output.mkdir()
        (output / "weights").write_bytes(b"licensed backend state")
        run = Run(output, (0, 10, 20))
        runs.append(run)
        return run
    monkeypatch.setattr(_registry, "get_method", lambda _method: plugin)
    def encoder(source, destination, **options):
        assert source is runs[0], "run metadata must not be discarded by converting to .path"
        assert source.frame_numbers == (0, 10, 20)
        assert (source.path / "weights").read_bytes() == b"licensed backend state"
        destination.write_bytes(b"encoded")
        return destination
    monkeypatch.setattr(_api, "encode_sequence", encoder)
    destination = tmp_path / "plugin.o4d"
    assert open4d.reconstruct("input", destination, method="fake", codec="fake") == destination
    assert not runs[0].path.exists()


def test_unknown_rgbd_codec_is_rejected_before_reconstruction(tmp_path, monkeypatch):
    from open4d.reconstruction import rgbd
    monkeypatch.setattr(rgbd, "reconstruct", lambda *_a, **_k: pytest.fail("expensive reconstruction ran"))
    with pytest.raises(ValueError, match="unknown codec"):
        open4d.reconstruct(tiny_capture(), tmp_path / "unknown.o4d", codec="unknown")


def test_missing_usd_dependency_is_rejected_before_reconstruction(tmp_path, monkeypatch):
    from open4d.reconstruction import rgbd
    from open4d.io import _usd
    def missing():
        raise _usd.MissingDependencyError("install open4d[usd]")
    monkeypatch.setattr(_usd, "_pxr", missing)
    monkeypatch.setattr(rgbd, "reconstruct", lambda *_a, **_k: pytest.fail("expensive reconstruction ran"))
    with pytest.raises(_usd.MissingDependencyError, match="open4d"):
        open4d.reconstruct(tiny_capture(), tmp_path / "unavailable.usdc")
