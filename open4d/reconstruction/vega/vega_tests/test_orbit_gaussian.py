"""Tests for the multi-view RGB (Gaussian-training) ORBIT loader.

The frame-indexing/camera-convention tests are pure logic and always run. The
carving test needs the real corpus and the CUDA rasterizer, so it skips when
either is unavailable.
"""
from __future__ import annotations

import json
import math
from pathlib import Path

import numpy as np
import pytest

from vega.cameras import look_at_RT
from vega.datasets import orbit_gaussian as og

DATASET_ROOT = Path("/media/frozzzen/DataDrive/ORBIT_datasets_gaussian")

torch = pytest.importorskip("torch")


def _fake_transforms(n_frames: int = 3, n_views: int = 4, start: int = 900) -> dict:
    """A transforms.json shaped like the real one: a ring of cameras actually
    aimed at the subject, with views listed out of order so grouping has
    something to sort.

    The OpenCV camera-to-world matrix is built from `look_at_RT`, which is this
    repo's own statement of the convention the real corpus's
    `camera_to_world_opencv` follows (+Z forward, +Y down).
    """
    target = np.array([0.0, 1.0, 0.0], dtype=np.float32)
    frames = []
    for t in range(n_frames):
        for v in reversed(range(n_views)):
            theta = 2 * math.pi * v / n_views
            eye = target + np.array([3.0 * math.cos(theta), 0.0, 3.0 * math.sin(theta)],
                                    dtype=np.float32)
            R, _T = look_at_RT(eye, target)
            c2w = np.eye(4)
            c2w[:3, :3] = R
            c2w[:3, 3] = eye
            frames.append({
                "file_path": f"./frame_{start + t:06d}/images/view_{v:02d}.png",
                "camera_to_world_opencv": c2w.tolist(),
                "transform_matrix": c2w.tolist(),
                "view_id": v,
                "source_frame": start + t,
                "frame_index": t,
                "time": t / 30.0,
            })
    return {"fl_x": 800.0, "fl_y": 800.0, "cx": 511.5, "cy": 383.5, "w": 1024, "h": 768,
            "view_count": n_views, "bounds_min": [-1, 0, -1], "bounds_max": [1, 2, 1],
            "frames": frames}


def test_group_frames_orders_by_frame_then_view():
    grouped = og.group_frames(_fake_transforms(n_frames=3, n_views=4))
    assert len(grouped) == 3
    for t, entries in enumerate(grouped):
        assert [e["view_id"] for e in entries] == [0, 1, 2, 3]
        assert {e["frame_index"] for e in entries} == {t}


def test_even_frame_indices_subsamples_and_clamps():
    assert og.even_frame_indices(30, 30) == list(range(30))
    assert og.even_frame_indices(30, 100) == list(range(30))
    picked = og.even_frame_indices(30, 4)
    assert picked[0] == 0 and picked[-1] == 29
    assert len(picked) == 4 and picked == sorted(picked)


def test_build_camera_matches_the_intrinsics_it_was_given():
    tr = _fake_transforms()
    cam = og.build_camera(tr, og.group_frames(tr)[0][0], image_scale=0.25, device="cpu")
    assert (cam.width, cam.height) == (256, 192)
    # fov must come from the *native* intrinsics, independent of image_scale
    assert cam.fovx == pytest.approx(2 * math.atan(1024 / (2 * 800.0)))
    assert cam.fovy == pytest.approx(2 * math.atan(768 / (2 * 800.0)))
    cam_half = og.build_camera(tr, og.group_frames(tr)[0][0], image_scale=0.5, device="cpu")
    assert cam_half.fovx == pytest.approx(cam.fovx)


def test_build_camera_recovers_the_camera_centre():
    """R/T must follow the 3DGS convention: T = -R^T C, so the camera centre
    the rasterizer derives from the view matrix comes back out."""
    tr = _fake_transforms(n_frames=1, n_views=4)
    for entry in og.group_frames(tr)[0]:
        cam = og.build_camera(tr, entry, image_scale=0.25, device="cpu")
        expected = np.asarray(entry["camera_to_world_opencv"])[:3, 3]
        assert cam.camera_center.numpy() == pytest.approx(expected, abs=1e-5)


def test_projection_agrees_with_the_camera_matrices():
    """`_project` (used for carving/colouring) and `Camera.full_proj_transform`
    (used for rendering) must place a point at the same pixel, or carved
    geometry and rendered geometry would disagree."""
    tr = _fake_transforms(n_frames=1, n_views=4)
    entry = og.group_frames(tr)[0][1]
    scale = 0.25
    cam = og.build_camera(tr, entry, image_scale=scale, device="cpu")
    w2c = og._world_to_cam([entry], "cpu")[0]
    fx, fy = tr["fl_x"] * scale, tr["fl_y"] * scale
    cx, cy = tr["cx"] * scale, tr["cy"] * scale

    pts = torch.tensor([[0.2, 1.0, 0.1], [-0.3, 1.4, 0.25]])
    u, v, z = og._project(pts, w2c, fx, fy, cx, cy)
    assert bool((z > 0).all()), "fixture cameras must have the points in front of them"

    hom = torch.cat([pts, torch.ones_like(pts[:, :1])], dim=1)
    clip = hom @ cam.full_proj_transform
    ndc = clip[:, :3] / clip[:, 3:4]
    u_ndc = ((ndc[:, 0] + 1.0) * cam.width - 1.0) * 0.5
    v_ndc = ((ndc[:, 1] + 1.0) * cam.height - 1.0) * 0.5

    assert z.numpy() == pytest.approx(clip[:, 3].numpy(), abs=1e-4)
    assert u.numpy() == pytest.approx(u_ndc.numpy(), abs=0.75)
    assert v.numpy() == pytest.approx(v_ndc.numpy(), abs=0.75)


def test_load_scene_rejects_out_of_range_frames():
    if not DATASET_ROOT.is_dir():
        pytest.skip(f"{DATASET_ROOT} not present")
    with pytest.raises(IndexError):
        og.load_scene(DATASET_ROOT, "basketball", [10_000], device="cpu")


def test_unknown_scene_lists_the_available_ones():
    if not DATASET_ROOT.is_dir():
        pytest.skip(f"{DATASET_ROOT} not present")
    with pytest.raises(KeyError, match="basketball"):
        og.load_object_transforms(DATASET_ROOT, "no_such_object")


@pytest.mark.skipif(not DATASET_ROOT.is_dir(), reason=f"{DATASET_ROOT} not present")
def test_carved_hull_projects_inside_the_real_silhouettes():
    """The carved hull is only useful if it lands where the subject actually is:
    every hull point must project inside the foreground of every view (that is
    the defining property of the visual hull), and cover most of it."""
    if not torch.cuda.is_available():
        pytest.skip("carving needs CUDA")
    scene = "basketball"
    tr = og.load_object_transforms(DATASET_ROOT, scene)
    entries = og.group_frames(tr)[0]
    base = og.object_transforms_path(DATASET_ROOT, scene).parent

    # view_slack=0 (also the default) is the strict visual hull the property
    # below asserts: with any slack a voxel needs only 7 of the 8 views to
    # agree, so part of the hull may legitimately miss a single silhouette.
    pts, cols = og.carve_frame(
        base, tr, entries,
        bounds_min=np.asarray(tr["bounds_min"], dtype=np.float32),
        bounds_max=np.asarray(tr["bounds_max"], dtype=np.float32),
        device="cuda", view_slack=0)
    assert pts.shape[0] > 10_000
    assert cols.shape == pts.shape
    assert cols.min() >= 0.0 and cols.max() <= 1.0

    xyz = torch.as_tensor(pts, device="cuda")
    imgs, w, h = og._load_views(base, entries, 0.25, "cuda")
    masks = imgs.max(dim=3).values > og.DEFAULT_SILHOUETTE_THRESHOLD
    w2c = og._world_to_cam(entries, "cuda")
    sx = w / float(tr["w"])
    fx, fy = tr["fl_x"] * sx, tr["fl_y"] * sx
    cx, cy = tr["cx"] * sx, tr["cy"] * sx

    for v in range(len(entries)):
        u, vv, z = og._project(xyz, w2c[v], fx, fy, cx, cy)
        ui, vi = u.round().long(), vv.round().long()
        assert bool((z > 0).all()), f"view {v}: hull points behind the camera"
        assert bool(((ui >= 0) & (ui < w) & (vi >= 0) & (vi < h)).all()), \
            f"view {v}: hull points project outside the image"
        inside = masks[v][vi, ui].float().mean().item()
        assert inside > 0.99, f"view {v}: only {inside:.3f} of hull points hit the silhouette"

        # ...and the hull must explain most of the silhouette, not just a corner
        painted = torch.zeros(h * w, dtype=torch.bool, device="cuda")
        painted[vi * w + ui] = True
        fg = masks[v].reshape(-1)
        covered = (painted & fg).sum().item() / max(int(fg.sum()), 1)
        assert covered > 0.5, f"view {v}: hull covers only {covered:.3f} of the silhouette"


def test_uniform_scale_leaves_the_projection_unchanged():
    """`_uniform_scale` only exists to move cameras out of the rasterizer's
    dead zone; if it changed where anything lands on screen, refinement would
    be fitting to the wrong image."""
    from vega.datasets.orbit import points_to_gaussians

    tr = _fake_transforms(n_frames=1, n_views=4)
    entries = og.group_frames(tr)[0]
    cams = [og.build_camera(tr, e, image_scale=0.25, device="cpu") for e in entries]
    rng = np.random.default_rng(0)
    pts = rng.uniform([-0.5, 0.5, -0.5], [0.5, 1.5, 0.5], size=(64, 3)).astype(np.float32)
    cols = rng.uniform(0.1, 0.9, size=(64, 3)).astype(np.float32)
    gs = points_to_gaussians(pts, cols, np.array([-1, 0, -1], np.float32),
                             np.array([1, 2, 1], np.float32), device="cpu")

    centre = gs.xyz.mean(dim=0)
    k = 2.5
    scaled_gs, scaled_cams = og._uniform_scale(gs, cams, centre, k)

    # cameras really did move out, by exactly k
    for cam, scam in zip(cams, scaled_cams):
        before = float((cam.camera_center - centre).norm())
        after = float((scam.camera_center - centre).norm())
        assert after == pytest.approx(before * k, rel=1e-4)

    # ...and every point still projects to the same pixel
    for cam, scam in zip(cams, scaled_cams):
        hom = torch.cat([gs.xyz, torch.ones_like(gs.xyz[:, :1])], dim=1)
        ndc = hom @ cam.full_proj_transform
        ndc = ndc[:, :2] / ndc[:, 3:4]
        hom_s = torch.cat([scaled_gs.xyz, torch.ones_like(scaled_gs.xyz[:, :1])], dim=1)
        ndc_s = hom_s @ scam.full_proj_transform
        ndc_s = ndc_s[:, :2] / ndc_s[:, 3:4]
        assert ndc_s.numpy() == pytest.approx(ndc.numpy(), abs=1e-4)

    # Gaussian extents scale with the world, so the footprint matches too
    assert scaled_gs.get_scaling.numpy() == pytest.approx(
        (gs.get_scaling * k).numpy(), rel=1e-5)


def test_refine_iters_zero_is_a_no_op():
    from vega.datasets.orbit import points_to_gaussians
    pts = np.random.default_rng(0).uniform(-0.5, 0.5, size=(32, 3)).astype(np.float32)
    cols = np.full((32, 3), 0.5, dtype=np.float32)
    gs = points_to_gaussians(pts, cols, np.array([-1, -1, -1], np.float32),
                             np.array([1, 1, 1], np.float32), device="cpu")
    out = og.refine_gaussians(gs, [], None, n_iters=0, extent=2.0)
    assert out is gs


@pytest.mark.skipif(not DATASET_ROOT.is_dir(), reason=f"{DATASET_ROOT} not present")
def test_refinement_improves_agreement_with_the_real_views():
    """Guards the failure mode this feature actually had: the rig cameras sit
    inside the rasterizer's dead zone, so a refinement that renders from them
    directly gets black images, zero gradients, and silently changes nothing."""
    if not torch.cuda.is_available():
        pytest.skip("refinement needs CUDA")
    from vega.metrics import psnr
    from vega.rasterize import render

    def psnr_against_views(gs, cams, gt):
        centre = gs.xyz.mean(dim=0)
        nearest = min(float((c.camera_center - centre).norm()) for c in cams)
        k = og.MIN_RENDER_DISTANCE / nearest if nearest < og.MIN_RENDER_DISTANCE else 1.0
        sgs, scams = og._uniform_scale(gs, cams, centre, k)
        vals = []
        for v in range(len(scams)):
            with torch.no_grad():
                img = render(scams[v], sgs, torch.zeros(3, device="cuda"))["render"].clamp(0, 1)
            assert img.max().item() > 0.0, "scaled render is black — dead zone not escaped"
            vals.append(psnr(img, gt[v]).item())
        return float(np.mean(vals))

    plain = og.load_scene(DATASET_ROOT, "basketball", [0], device="cuda",
                          image_scale=0.125, load_gt_images=True)
    refined = og.load_scene(DATASET_ROOT, "basketball", [0], device="cuda",
                            image_scale=0.125, load_gt_images=True, refine_iters=150)

    before = psnr_against_views(plain[0][0], plain[1], plain[2][0])
    after = psnr_against_views(refined[0][0], refined[1], refined[2][0])
    assert after > before + 1.0, f"refinement changed nothing: {before:.2f} -> {after:.2f} dB"
    # refinement must not change the Gaussian count the rate model prices
    assert len(refined[0][0]) == len(plain[0][0])


def _two_frames(n=40, shift=0.4):
    """A key frame and a residual frame of the same single object, moved."""
    from vega.datasets.orbit import points_to_gaussians
    rng = np.random.default_rng(0)
    pts = rng.uniform(-0.3, 0.3, size=(n, 3)).astype(np.float32)
    cols = np.full((n, 3), 0.5, dtype=np.float32)
    lo = np.array([-1, -1, -1], np.float32); hi = np.array([1, 1, 1], np.float32)
    key = points_to_gaussians(pts, cols, lo, hi, device="cpu")
    res = points_to_gaussians(pts + np.float32(shift), cols, lo, hi, device="cpu")
    return key, res


def test_single_object_reconstruction_is_exact_when_dynamic():
    """The ghosting guard. With one object (the ORBIT corpora's actual case)
    a moving subject must reconstruct to exactly the current frame — never a
    mix of current and key-frame geometry, which is what draws extra limbs."""
    from vega.filtering import apply_filtering, plan_filtering
    key, res = _two_frames()
    plan = plan_filtering(res.object_ids().tolist(), dynamic_objects={0})
    out = apply_filtering(res, key, plan)
    assert len(out) == len(res), "reconstruction changed the Gaussian count"
    assert out.get_xyz.numpy() == pytest.approx(res.get_xyz.numpy()), \
        "a dynamic single object must come entirely from the current frame"


def test_single_object_falls_back_to_key_when_static():
    from vega.filtering import apply_filtering, plan_filtering
    key, res = _two_frames()
    plan = plan_filtering(res.object_ids().tolist(), dynamic_objects=set())
    out = apply_filtering(res, key, plan)
    assert out.get_xyz.numpy() == pytest.approx(key.get_xyz.numpy()), \
        "a static single object must be reused wholesale from the key frame"


def test_multi_cluster_filtering_can_mix_poses():
    """Documents *why* the default is one object: splitting one moving subject
    into position clusters lets a residual frame carry geometry from two
    different instants at once."""
    from vega.filtering import apply_filtering, plan_filtering
    key, res = _two_frames(n=40, shift=0.4)
    half = len(key) // 2
    ids = torch.zeros(len(key), dtype=torch.long)
    ids[half:] = 1
    key.object_id = ids.clone()
    res.object_id = ids.clone()

    plan = plan_filtering([0, 1], dynamic_objects={1})   # cluster 0 judged static
    out = apply_filtering(res, key, plan)
    xyz = out.get_xyz
    from_key = (xyz[:, 0] < 0.35).sum().item()      # unmoved cluster
    from_res = (xyz[:, 0] >= 0.35).sum().item()     # moved cluster
    assert from_key > 0 and from_res > 0, \
        "expected this configuration to mix key-frame and current-frame geometry"
    assert len(out) == len(res)
