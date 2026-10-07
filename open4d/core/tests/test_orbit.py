from __future__ import annotations

import json
import warnings
import sys
from pathlib import Path

import numpy as np
import pytest

from open4d import gaussians, orbit

CENTER = np.array([1.0, 0.5, 3.0])
RADIUS = 0.3
K = (60.0, 60.0, 31.5, 23.5)
SIZE = (64, 48)


def look_at(eye):
    z = CENTER - eye
    z /= np.linalg.norm(z)
    x = np.cross([0.0, -1.0, 0.0], z)
    x /= np.linalg.norm(x)
    pose = np.eye(4)
    pose[:3, :3] = np.column_stack([x, np.cross(z, x), z])
    pose[:3, 3] = eye
    return pose


def render(pose, center):
    u, v = np.meshgrid(np.arange(SIZE[0]), np.arange(SIZE[1]))
    rays = np.stack([(u - K[2]) / K[0], (v - K[3]) / K[1], np.ones(u.shape)], -1) @ pose[:3, :3].T
    rays /= np.linalg.norm(rays, axis=-1, keepdims=True)
    offset = pose[:3, 3] - center
    b = rays @ offset
    hit = b ** 2 - (offset @ offset - RADIUS ** 2) > 0
    image = np.zeros((*u.shape, 3), np.uint8)
    image[hit] = (200, 50, 50)
    return image


def make_corpus(root, *, frames=3, views=4, background="black", corpus=True, edit=None):
    Image = pytest.importorskip("PIL.Image")
    folder = root / "ball"
    poses = [look_at(CENTER + [2 * np.sin(a), 0.4, 2 * np.cos(a)])
             for a in np.linspace(0, 2 * np.pi, views, endpoint=False)]
    entries = []
    for t in range(frames):
        for view, pose in enumerate(poses):
            relative = f"frame_{100 + t:06d}/images/view_{view:02d}.png"
            (folder / relative).parent.mkdir(parents=True, exist_ok=True)
            Image.fromarray(render(pose, CENTER + [0.02 * t, 0, 0])).save(folder / relative)
            gl = pose @ np.diag([1.0, -1.0, -1.0, 1.0])
            entries.append({"file_path": "./" + relative, "frame_index": t, "view_id": view,
                            "source_frame": 100 + t, "time": t / max(frames - 1, 1),
                            "transform_matrix": gl.tolist(), "camera_to_world_opencv": pose.tolist()})
    meta = {"camera_model": "OPENCV", "fl_x": K[0], "fl_y": K[1], "cx": K[2], "cy": K[3],
            "w": SIZE[0], "h": SIZE[1], "k1": 0.0, "k2": 0.0, "p1": 0.0, "p2": 0.0,
            "view_count": views, "bounds_min": (CENTER - 0.4).tolist(),
            "bounds_max": (CENTER + 0.4).tolist(), "frames": entries}
    if edit:
        edit(meta)
    (folder / "transforms.json").write_text(json.dumps(meta))
    if corpus:
        (root / "dataset.json").write_text(json.dumps({
            "format": "orbit-rgb-gaussian-training", "version": 1, "background": background,
            "objects": [{"name": "ball", "path": "ball/transforms.json"}]}))
    return folder


def test_load_orbit_reads_corpus_and_object_folders(tmp_path):
    folder = make_corpus(tmp_path)
    assert orbit.is_orbit(tmp_path) and orbit.is_orbit(folder)
    assert orbit.objects(tmp_path) == ("ball",)
    with pytest.raises(ValueError, match="pass one of: ball"):
        orbit.load_orbit(tmp_path)
    with pytest.raises(KeyError, match="ball"):
        orbit.load_orbit(tmp_path, "cube")
    scene = orbit.load_orbit(tmp_path, "ball")
    direct = orbit.load_orbit(folder)
    assert scene.path == direct.path and direct.background == "black"
    assert len(scene) == 3 and scene.source_frames == (100, 101, 102)
    assert [camera.view_id for camera in scene.cameras] == [0, 1, 2, 3]
    assert scene.image(1, 2).shape == (48, 64, 3)
    np.testing.assert_allclose(scene.cameras[0].center, CENTER + [0, 0.4, 2])
    assert len(scene.select(2)) == 2 and scene.select(range(1, 3)).source_frames == (101, 102)
    with pytest.raises(ValueError, match="contiguous"):
        scene.select(slice(0, 3, 2))


@pytest.mark.parametrize("edit,match", [
    (lambda m: m["frames"][4]["camera_to_world_opencv"][0].__setitem__(3, 9.0), "fixed"),
    (lambda m: m["frames"].pop(), "same views"),
    (lambda m: m.__setitem__("k1", 0.1), "undistorted"),
    (lambda m: m["frames"][0].__setitem__("file_path", "../../outside.png"), "leaves"),
    (lambda m: m["frames"][0]["camera_to_world_opencv"][0].__setitem__(0, 2.0), "rigid"),
])
def test_load_orbit_rejects_captures_it_cannot_represent(tmp_path, edit, match):
    make_corpus(tmp_path, edit=edit)
    with pytest.raises(ValueError, match=match):
        orbit.load_orbit(tmp_path, "ball")


def test_prepare_queen_matches_queen_camera_convention(tmp_path):
    scene = orbit.load_orbit(make_corpus(tmp_path / "data"))
    out = orbit.prepare(scene, tmp_path / "queen", method="queen", test_views=(2,), max_width=32)
    rows = np.load(out / "poses_bounds.npy")
    order = json.loads((out / "orbit.json").read_text())["views"]
    assert order[0] == 2 and sorted(order) == [0, 1, 2, 3]
    assert len(list((out / "cam00" / "images").glob("*.png"))) == 3
    # QUEEN's readCamerasFromPoseBounds.
    poses = rows[:, :-2].reshape(-1, 3, 5)
    height, width, focal = poses[0, :, -1]
    assert (width, height, focal) == (32, 24, 30)
    poses = np.concatenate([poses[..., 1:2], -poses[..., :1], poses[..., 2:4]], -1)
    for slot, view in enumerate(order):
        R = -poses[slot][:3, :3]
        R[:, 0] = -R[:, 0]
        T = -poses[slot][:3, 3].dot(R)
        pose = scene.cameras[view].camera_to_world
        np.testing.assert_allclose(R, pose[:3, :3], atol=1e-9)
        np.testing.assert_allclose(T, -pose[:3, :3].T @ pose[:3, 3], atol=1e-9)
        near, far = rows[slot, -2:]
        assert near < np.linalg.norm(pose[:3, 3] - CENTER) - RADIUS < far
    assert (out / "colmap/dense/workspace/fused.ply").is_file() and (out / "points3D_downsample2.ply").is_file()
    # QUEEN's getVideoCameras reads render_path.npy with the same conversion:
    # every path camera must look at the object from the rig's distance.
    path = np.load(out / "render_path.npy")
    assert path.shape == (3, 3, 4)
    for pose in path:
        R = -pose[:3, :3]
        R[:, 0] = -R[:, 0]
        eye = pose[:3, 3]
        forward = R[:, 2]
        towards = (CENTER - eye) / np.linalg.norm(CENTER - eye)
        assert forward @ towards == pytest.approx(1, abs=1e-6)
        assert np.linalg.det(R) == pytest.approx(1)
        assert R[:, 1] @ [0, -1, 0] > 0.9  # image down is world down
        assert np.linalg.norm((eye - CENTER)[[0, 2]]) == pytest.approx(2, abs=1e-6)
    np.testing.assert_allclose(path[0][:3, 3], CENTER + [0, 0.4, 2], atol=1e-9)


def test_prepare_3dgstream_matches_blender_camera_convention(tmp_path):
    scene = orbit.load_orbit(make_corpus(tmp_path / "data"))
    out = orbit.prepare(scene, tmp_path / "gstream", method="3dgstream", test_views=(1,))
    assert sorted(p.name for p in out.glob("frame*")) == ["frame000000", "frame000001", "frame000002"]
    test = json.loads((out / "frame000001" / "transforms_test.json").read_text())
    train = json.loads((out / "frame000001" / "transforms_train.json").read_text())
    assert [e["file_path"] for e in test["frames"]] == ["images/view_01"] and len(train["frames"]) == 3
    assert test["camera_angle_x"] == pytest.approx(2 * np.arctan(32 / 60))
    # 3DGStream's patched readCamerasFromTransforms.
    c2w = np.array(test["frames"][0]["transform_matrix"])
    c2w[:3, 1:3] *= -1
    w2c = np.linalg.inv(c2w)
    pose = scene.cameras[1].camera_to_world
    np.testing.assert_allclose(w2c[:3, :3].T, pose[:3, :3], atol=1e-9)
    np.testing.assert_allclose(w2c[:3, 3], -pose[:3, :3].T @ pose[:3, 3], atol=1e-9)
    assert (out / "frame000002" / "images" / "view_03.png").is_file()


def read_points(path):
    data = path.read_bytes()
    body = data[data.index(b"end_header\n") + len(b"end_header\n"):]
    dtype = [(n, "<f4") for n in ("x", "y", "z", "nx", "ny", "nz")] + [(n, "u1") for n in ("red", "green", "blue")]
    values = np.frombuffer(body, dtype)
    return np.column_stack([values["x"], values["y"], values["z"]]), \
        np.column_stack([values["red"], values["green"], values["blue"]])


@pytest.mark.parametrize("held", [(), (1, 3)])
def test_prepare_holds_out_any_set_of_views(tmp_path, held):
    scene = orbit.load_orbit(make_corpus(tmp_path / "data"))
    queen = orbit.prepare(scene, tmp_path / "queen", method="queen", test_views=held)
    recorded = json.loads((queen / "orbit.json").read_text())
    assert recorded["test_views"] == list(held) and recorded["views"][:len(held)] == list(held)
    assert len(np.load(queen / "poses_bounds.npy")) == 4
    gstream = orbit.prepare(scene, tmp_path / "gstream", method="3dgstream", test_views=held)
    train = json.loads((gstream / "frame000000/transforms_train.json").read_text())["frames"]
    test = json.loads((gstream / "frame000000/transforms_test.json").read_text())["frames"]
    assert [e["file_path"] for e in train] == [f"images/view_{v:02d}" for v in recorded["views"][len(held):]]
    if held:
        assert [e["file_path"] for e in test] == ["images/view_01", "images/view_03"]
    else:  # upstream needs a test split; it repeats a training view
        assert len(train) == 4 and test == train[:1]
    with pytest.raises(ValueError, match="two views"):
        orbit.prepare(scene, tmp_path / "bad", method="queen", test_views=(0, 1, 2))
    with pytest.raises(ValueError, match="distinct"):
        orbit.prepare(scene, tmp_path / "bad", method="queen", test_views=(1, 1))


def test_initial_points_carve_black_background_silhouettes(tmp_path):
    scene = orbit.load_orbit(make_corpus(tmp_path / "data", views=6))
    out = orbit.prepare(scene, tmp_path / "carved", method="3dgstream", initial_points="carve")
    points, colors = read_points(out / "frame000000" / "points3d.ply")
    # Six ring cameras carve a hexagonal hull around the sphere: points lie on
    # or outside it, within RADIUS * (1 / cos(30 degrees) - 1) at the waist.
    signed = np.linalg.norm(points - CENTER, axis=1) - RADIUS
    assert np.percentile(signed, 5) > -0.015
    assert np.median(signed) < RADIUS * (1 / np.cos(np.pi / 6) - 1) + 0.01
    assert np.median(colors, axis=0) == pytest.approx([200, 50, 50], abs=20)


def test_held_out_view_does_not_shape_initial_points(tmp_path):
    from PIL import Image

    folder = make_corpus(tmp_path / "data", views=6)
    scene = orbit.load_orbit(folder)
    before = orbit.prepare(scene, tmp_path / "before", method="3dgstream", test_views=(2,), initial_points="carve")
    Image.fromarray(np.zeros((48, 64, 3), np.uint8)).save(folder / "frame_000100/images/view_02.png")
    after = orbit.prepare(scene, tmp_path / "after", method="3dgstream", test_views=(2,), initial_points="carve")
    assert (before / "frame000000/points3d.ply").read_bytes() == (after / "frame000000/points3d.ply").read_bytes()


def test_initial_points_fill_bounds_by_default(tmp_path):
    scene = orbit.load_orbit(make_corpus(tmp_path / "data", corpus=False))
    assert scene.background is None
    with pytest.raises(ValueError, match="black background"):
        orbit.prepare(scene, tmp_path / "carve", method="queen", initial_points="carve")
    points, _ = read_points(orbit.prepare(scene, tmp_path / "out", method="queen") / "points3D_downsample2.ply")
    assert np.all(points >= scene.bounds_min - 0.05) and np.all(points <= scene.bounds_max + 0.05)
    assert np.linalg.norm(points.std(axis=0) - 0.8 * 1.1 / np.sqrt(12)) < 0.02


def test_prepare_removes_a_partial_layout(tmp_path):
    folder = make_corpus(tmp_path / "data")
    scene = orbit.load_orbit(folder)
    (folder / "frame_000102/images/view_01.png").write_bytes(b"not a png")
    with pytest.raises(Exception):
        orbit.prepare(scene, tmp_path / "out", method="3dgstream")
    assert not (tmp_path / "out").exists()


def test_prepare_rejects_unrepresentable_cameras(tmp_path):
    make_corpus(tmp_path, edit=lambda m: m.__setitem__("cx", 20.0))
    scene = orbit.load_orbit(tmp_path, "ball")
    with pytest.raises(ValueError, match="principal point"):
        orbit.prepare(scene, tmp_path / "out", method="queen")
    assert not (tmp_path / "out").exists()


@pytest.fixture
def fake_runtime(tmp_path):
    runtime = tmp_path / "runtime" / "gs_tools"
    (runtime / "gs_tools").mkdir(parents=True)
    (runtime / "gs_tools" / "__init__.py").write_text("")
    (runtime / "gs_tools" / "cli.py").write_text(
        "import json, sys\nfrom pathlib import Path\n"
        "args = sys.argv[1:]\n"
        "if args[0] == 'depth-prior':\n"
        "    scene = Path(args[args.index('-s') + 1])\n"
        "    (scene / 'depth_priors').mkdir()\n"
        "    (scene / 'depth_priors' / 'call.json').write_text(json.dumps(args))\n"
        "    raise SystemExit(0)\n"
        "run, scene = Path(args[args.index('-m') + 1]), Path(args[args.index('-s') + 1])\n"
        "(run / 'arguments.json').write_text(json.dumps(args))\n"
        "frames = sorted(p.name for p in (scene / 'cam00' / 'images').glob('*.png'))\n"
        "for i, _ in enumerate(frames, 1):\n"
        "    frame = run / 'frames' / f'{i:04d}' / 'point_cloud.ply'\n"
        "    frame.parent.mkdir(parents=True, exist_ok=True)\n"
        "    frame.write_text('fixture')\n"
    )
    return runtime


def test_reconstruct_converts_orbit_input_inside_the_run(tmp_path, fake_runtime):
    import open4d

    make_corpus(tmp_path / "data", frames=4)
    run = open4d.reconstruct(tmp_path / "data" / "ball", tmp_path / "run", method="queen",
                             runtime=fake_runtime, frames=2, max_width=32, test_views=(3,))
    arguments = json.loads((tmp_path / "run" / "arguments.json").read_text())
    assert arguments[arguments.index("-s") + 1] == str(tmp_path / "run" / "input")
    assert run.source == tmp_path / "run" / "input" and len(run.frame_paths) == 2
    recorded = json.loads((run.source / "orbit.json").read_text())
    assert recorded["source_frames"] == [100, 101] and recorded["test_views"] == [3]
    assert arguments[arguments.index("--test-indices") + 1:arguments.index("--")] == ["0"]
    scene = open4d.load_orbit(tmp_path / "data", "ball")
    open4d.reconstruct(scene.select(3), tmp_path / "run2", method="queen", runtime=fake_runtime,
                       test_views=())
    arguments = json.loads((tmp_path / "run2" / "arguments.json").read_text())
    assert arguments[arguments.index("--test-indices") + 1:arguments.index("--")] == []
    open4d.reconstruct(scene.select(2), tmp_path / "run3", method="queen", runtime=fake_runtime,
                       test_views=(2, 0))
    arguments = json.loads((tmp_path / "run3" / "arguments.json").read_text())
    assert arguments[arguments.index("--test-indices") + 1:arguments.index("--")] == ["0", "1"]
    assert len(list((tmp_path / "run2" / "input" / "cam00" / "images").glob("*.png"))) == 3


def test_reconstruct_validates_orbit_options_before_creating_output(tmp_path, fake_runtime):
    make_corpus(tmp_path / "data")
    with pytest.raises(ValueError, match="test_views"):
        gaussians.reconstruct_gaussians(tmp_path / "data" / "ball", tmp_path / "run",
                                        runtime=fake_runtime, test_views=(9,))
    with pytest.raises(ValueError, match="pass one of"):
        gaussians.reconstruct_gaussians(tmp_path / "data", tmp_path / "run", runtime=fake_runtime)
    plain = tmp_path / "plain"
    plain.mkdir()
    with pytest.raises(TypeError, match="ORBIT"):
        gaussians.reconstruct_gaussians(plain, tmp_path / "run", runtime=fake_runtime, frames=2)
    assert not (tmp_path / "run").exists()
    assert "gs_tools" not in sys.modules


def test_reconstruct_caches_depth_priors_with_their_own_interpreter(tmp_path, fake_runtime):
    import open4d

    make_corpus(tmp_path / "data")
    with pytest.warns(RuntimeWarning, match="black-background"):
        run = open4d.reconstruct(tmp_path / "data" / "ball", tmp_path / "run", method="queen",
                                 runtime=fake_runtime, depth_priors=True, depth_python="/opt/midas/python")
    call = json.loads((run.source / "depth_priors" / "call.json").read_text())
    assert call[:3] == ["depth-prior", "-s", str(run.source)] and call[-2:] == ["--python", "/opt/midas/python"]
    arguments = json.loads((tmp_path / "run" / "arguments.json").read_text())
    assert "--depth-priors" in arguments[:arguments.index("--")]
    with pytest.raises(ValueError, match="only to QUEEN"):
        open4d.reconstruct(tmp_path / "data" / "ball", tmp_path / "run2", method="3dgstream",
                           runtime=fake_runtime, depth_priors=True)
    with pytest.raises(TypeError, match="depth_priors=True"):
        open4d.reconstruct(tmp_path / "data" / "ball", tmp_path / "run2", method="queen",
                           runtime=fake_runtime, depth_python="/opt/midas/python")
    make_corpus(tmp_path / "grey", background=None)
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        open4d.reconstruct(tmp_path / "grey" / "ball", tmp_path / "run3", method="queen",
                           runtime=fake_runtime, depth_priors=True)
