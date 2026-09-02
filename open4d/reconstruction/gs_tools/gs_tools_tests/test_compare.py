"""The camera and comparison layer: rigs, captured views, and rig renders.

Same discipline as `test_view_pipeline.py` -- no GPU, no torch, no corpus. What
is pinned here is the geometry and the plan, because those are what decide
whether two panes showing different things are showing them from the same place.
"""

from __future__ import annotations

import json
import math
from pathlib import Path

import numpy as np
import pytest

from gs_tools import cameras, outputs
from streamer import bundle
from gs_tools.methods import capture, rerf


def _ring_transforms(count: int = 8, radius: float = 3.0, height: float = 1.0) -> dict:
    """A synthetic ORBIT `transforms.json`: a coplanar ring looking inward.

    Built the way the real corpus is -- OpenCV camera-to-world, x right, y down,
    z forward -- so a convention slip in `read_orbit_rig` shows up as a pose that
    is not orthonormal or not right-handed, which the tests below check for.
    """
    centre = np.array([0.0, height, 0.0])
    frames = []
    for index in range(count):
        angle = 2 * math.pi * index / count
        eye = centre + np.array([radius * math.sin(angle), 0.0, radius * math.cos(angle)])
        forward = centre - eye
        forward /= np.linalg.norm(forward)
        right = np.cross(forward, np.array([0.0, 1.0, 0.0]))
        right /= np.linalg.norm(right)
        down = np.cross(forward, right)
        c2w = np.eye(4)
        c2w[:3, 0], c2w[:3, 1], c2w[:3, 2], c2w[:3, 3] = right, down, forward, eye
        frames.append({
            "file_path": f"./images/view_{index:02d}.png",
            # Deliberately wrong on purpose: the OpenGL matrix is what a reader
            # must NOT pick up, so it is present and different.
            "transform_matrix": np.eye(4).tolist(),
            "camera_to_world_opencv": c2w.tolist(),
            "view_id": index,
        })
    return {
        "camera_model": "OPENCV", "fl_x": 1000.0, "fl_y": 1000.0,
        "cx": 512.0, "cy": 384.0, "w": 1024, "h": 768,
        "bounds_min": [-0.5, 0.0, -0.5], "bounds_max": [0.5, 2.0, 0.5],
        "frames": frames,
    }


def _orbit_scene(root: Path, *, frames: int = 3, views: int = 8) -> Path:
    root.mkdir(parents=True, exist_ok=True)
    transforms = _ring_transforms(views)
    for step in range(1, frames + 1):
        frame = root / f"frame_{step:06d}"
        (frame / "images").mkdir(parents=True, exist_ok=True)
        (frame / "transforms.json").write_text(json.dumps(transforms))
    return root


# ----------------------------------------------------------------- cameras ---
def test_rig_poses_are_orthonormal_and_right_handed(tmp_path):
    rig = cameras.read_orbit_rig(_orbit_scene(tmp_path / "subject"))
    assert len(rig.poses) == 8
    for pose in rig.poses:
        basis = np.array([pose.right, pose.down, pose.forward])
        np.testing.assert_allclose(basis @ basis.T, np.eye(3), atol=1e-9)
        # (right, down, forward) in that order, which is what the viewer's
        # projection assumes; a flipped pair renders the scene mirrored.
        np.testing.assert_allclose(np.cross(pose.right, pose.down), pose.forward, atol=1e-9)


def test_rig_reads_the_opencv_matrix_not_the_opengl_one(tmp_path):
    rig = cameras.read_orbit_rig(_orbit_scene(tmp_path / "subject"))
    # The synthetic corpus sets transform_matrix to the identity; picking it up
    # would put every camera at the origin.
    assert not np.allclose(rig.poses[0].position, [0, 0, 0])


def test_rig_geometry_matches_the_ring_it_was_built_from(tmp_path):
    rig = cameras.read_orbit_rig(_orbit_scene(tmp_path / "subject"))
    np.testing.assert_allclose(rig.centre, [0, 1, 0], atol=1e-9)
    assert rig.radius == pytest.approx(3.0, abs=1e-6)
    assert math.degrees(rig.fov_y()) == pytest.approx(
        2 * math.degrees(math.atan(0.5 * 768 / 1000.0)), abs=1e-9)


def test_ring_path_lands_on_the_real_cameras(tmp_path):
    rig = cameras.read_orbit_rig(_orbit_scene(tmp_path / "subject"))
    exact = cameras.ring_path(rig, 8)
    assert [pose.view_id for pose in exact] == list(range(8))
    for a, b in zip(exact, rig.poses):
        np.testing.assert_allclose(a.position, b.position, atol=1e-9)

    finer = cameras.ring_path(rig, 32)
    assert len(finer) == 32
    # Every fourth sample is a station; the rest are synthesised and say so, so
    # a caller can tell where ground truth exists.
    assert [i for i, pose in enumerate(finer) if pose.view_id is not None] == list(range(0, 32, 4))
    for pose in finer:
        assert np.linalg.norm(np.asarray(pose.position) - rig.centre) == pytest.approx(3.0, abs=1e-6)


def test_ring_path_rejects_a_nonsense_count(tmp_path):
    rig = cameras.read_orbit_rig(_orbit_scene(tmp_path / "subject"))
    with pytest.raises(ValueError):
        cameras.ring_path(rig, 0)


def test_pose_round_trips_through_its_matrix(tmp_path):
    rig = cameras.read_orbit_rig(_orbit_scene(tmp_path / "subject"))
    for pose in rig.poses:
        again = cameras.Pose.from_c2w(pose.c2w(), pose.view_id)
        np.testing.assert_allclose(again.position, pose.position, atol=1e-12)
        np.testing.assert_allclose(again.forward, pose.forward, atol=1e-12)


# ----------------------------------------------------------------- corpus ----
def test_detect_orbit_scene_and_corpus(tmp_path):
    scene = _orbit_scene(tmp_path / "corpus" / "subject")
    assert outputs.detect(scene).kind is outputs.Kind.ORBIT_SCENE
    (tmp_path / "corpus" / "dataset.json").write_text(json.dumps(
        {"format": "orbit-rgb-gaussian-training", "frame_limit": 3,
         "views_per_frame": 8, "objects": [{"name": "subject"}]}))
    found = outputs.detect(tmp_path / "corpus")
    assert found.kind is outputs.Kind.ORBIT_CORPUS
    assert found.detail["objects"] == ["subject"]


def test_rigs_for_skips_scenes_the_corpus_does_not_have(tmp_path):
    _orbit_scene(tmp_path / "corpus" / "subject")
    rigs = capture.rigs_for(tmp_path / "corpus", ["subject", "absent"])
    assert list(rigs) == ["subject"]
    assert len(rigs["subject"]["poses"]) == 8


def test_frame_dirs_are_time_ordered_past_nine(tmp_path):
    scene = _orbit_scene(tmp_path / "subject", frames=12)
    names = [p.name for p in capture.frame_dirs(scene)]
    # Lexical order would put frame_000010 before frame_000002.
    assert names[:3] == ["frame_000001", "frame_000002", "frame_000003"]
    assert names[-1] == "frame_000012"


# ------------------------------------------------------------------ bundle ---
def test_frame_dir_never_lets_two_clips_share_a_directory(tmp_path):
    first = bundle.frame_dir(tmp_path, "subject")
    (first / "frame_0000.ply").write_bytes(b"x")
    second = bundle.frame_dir(tmp_path, "subject")
    assert first.name == "subject" and second.name == "subject-2"


def test_clip_carries_scene_method_and_camera(tmp_path):
    clip = bundle.Clip(name="c", representation="pixels", scene="subject", method="rerf", camera=3,
                       frames=["c/frame_0000.jpg"])
    bundle.write(tmp_path, title="t", source="s", clips=[clip],
                 scenes={"subject": {"poses": []}})
    index = bundle.read(tmp_path)
    stored = index["clips"][0]
    assert (stored["scene"], stored["method"], stored["camera"]) == ("subject", "rerf", 3)
    assert "subject" in index["scenes"]


# -------------------------------------------------------------- rig render ---
def _rerf_bitstream(root: Path, frames: int = 4) -> Path:
    root.mkdir(parents=True, exist_ok=True)
    (root / "model_kwargs.json").write_text(json.dumps({"xyz_min": [0] * 3, "xyz_max": [1] * 3}))
    (root / "rgb_net.tar").write_bytes(b"")
    for index in range(frames):
        entries = ([{"origin_size": [13, 8, 16, 8], "quality": 99}] if index == 0
                   else [{"origin_size": [7, 8, 16, 8], "quality": 99},
                         {"origin_size": [6, 8, 16, 8], "quality": 98}])
        (root / f"header_{index}.json").write_text(json.dumps({"headers": entries}))
    return root


def test_rig_render_dir_names_the_views_and_frames(tmp_path):
    assert rerf.rig_render_dir(tmp_path, (0, 2), 30).name == "render_rig_v0-2_f30"
    # Upstream's own name carries only the frame count, which is why two
    # bitstreams overwrite each other; this one cannot.
    assert rerf.rig_render_dir(tmp_path, (0,), 30).name != rerf.rig_render_dir(tmp_path, (2,), 30).name


def test_rig_render_writes_a_plan_and_refuses_without_permission(tmp_path):
    root = _rerf_bitstream(tmp_path / "rerf")
    (tmp_path / "config.py").write_text("expname = 'g_x'\n")
    options = rerf.RerfRenderOptions(rig_views=(0, 2), frames=3)
    with pytest.raises(RuntimeError, match="--render was not given"):
        rerf.render_at_rig(tmp_path, root, options)
    plan = json.loads((rerf.rig_render_dir(tmp_path, (0, 2), 3) / "plan.json").read_text())
    assert plan["views"] == [0, 2]
    assert plan["times"] == [0, 1, 2]
    assert Path(plan["nevo_tree"]).name == "nevo"


def test_rig_render_refuses_more_frames_than_exist(tmp_path):
    root = _rerf_bitstream(tmp_path / "rerf", frames=4)
    (tmp_path / "config.py").write_text("expname = 'g_x'\n")
    with pytest.raises(ValueError, match="4 compressed frames"):
        rerf.render_at_rig(tmp_path, root,
                           rerf.RerfRenderOptions(rig_views=(0,), frames=9, render=True))


def test_collect_rig_unpacks_timestep_major_view_minor_order(tmp_path):
    images = tmp_path / "render_rig_v0-2_f3"
    images.mkdir()
    views, times = [0, 2], [0, 1, 2]
    # The plan renders every view of one instant before moving on, so index
    # step*len(views)+position is the only correct way back.
    for step in times:
        for position, view in enumerate(views):
            index = step * len(views) + position
            (images / f"{index:03d}.jpg").write_bytes(f"v{view}t{step}".encode())
            (images / f"{index:03d}_depth.jpg").write_bytes(b"d")
    clips = rerf.collect_rig(images, tmp_path / "out", "run-rerf", views, times,
                             rerf.RerfRenderOptions(depth=False), scene="subject", method="rerf")
    assert [(c.method, c.camera) for c in clips] == [("rerf", 0), ("rerf", 2)]
    for clip, view in zip(clips, views):
        for step in times:
            got = (tmp_path / "out" / clip.frames[step]).read_bytes()
            assert got == f"v{view}t{step}".encode()


def test_collect_rig_reports_a_short_render(tmp_path):
    images = tmp_path / "render_rig_v0_f3"
    images.mkdir()
    (images / "000.jpg").write_bytes(b"x")
    with pytest.raises(FileNotFoundError, match="did not produce"):
        rerf.collect_rig(images, tmp_path / "out", "run", [0], [0, 1, 2],
                         rerf.RerfRenderOptions(depth=False))


def test_scene_name_strips_the_corpus_prefix():
    assert rerf.scene_name("g_basketball") == "basketball"
    assert rerf.scene_name("basketball") == "basketball"


def test_rig_runner_anchors_still_match_the_vendored_script():
    """The injected patch anchors on two exact lines of upstream's renderer.

    If a pin bump moves or rewords either, the runner must fail loudly rather
    than render something subtly different -- so the anchors are checked here
    too, where it costs nothing to notice.
    """
    from gs_tools import paths
    from gs_tools.methods import _rerf_rig_render as runner

    script = paths.upstream("nevo") / "rerf" / "rerf_render.py"
    if not script.is_file():
        pytest.skip("vendored ReRF is not checked out")
    source = script.read_text()
    assert runner.CALLBACK_ANCHOR in source
    assert runner.OUTPUT_ANCHOR in source
