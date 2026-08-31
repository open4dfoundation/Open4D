"""End-to-end smoke test on the synthetic scene: segmentation -> GOV planning
-> hierarchical color encoding -> dynamicity filtering -> bitstream, plus the
view-adaptive rendering pipeline simulation. Uses tiny iteration counts so it
runs in seconds; it is meant to catch integration regressions, not to
validate quality (see scripts/ for that).
"""
import torch

from vega.culling import early_cull_from_boxes
from vega.encoder import VegaEncoderConfig, default_eval_camera, encode_sequence
from vega.pipeline import ViewAdaptiveRenderer
from vega.profiling import profile_latency_model
from vega.rasterize import render
from vega.segmentation import segment_sequence
from vega.synthetic import playback_camera_path, synthetic_sequence


def test_synthetic_scene_renders_non_degenerate_image():
    frames, cams, _ = synthetic_sequence(n_frames=2, n_objects=4, device="cuda")
    bg = torch.zeros(3, device="cuda")
    out = render(cams[0], frames[0], bg)
    assert (out["radii"] > 0).any(), "no Gaussians visible — camera likely facing away from the scene"
    assert out["render"].max().item() > 0.0


def test_encode_sequence_end_to_end_on_synthetic_scene():
    frames, cams, object_dynamic_gt = synthetic_sequence(n_frames=6, n_objects=4, device="cuda")
    bounds_min = torch.stack([f.get_xyz.min(dim=0).values for f in frames]).min(dim=0).values
    bounds_max = torch.stack([f.get_xyz.max(dim=0).values for f in frames]).max(dim=0).values

    frames_seg = segment_sequence(frames, k=4, n_iters=10)
    cfg = VegaEncoderConfig(key_iters=30, residual_iters=15, dyn_iters=5)
    result = encode_sequence(frames_seg, bounds_min, bounds_max, config=cfg)

    assert result.frame_costs[0].frame_type == "key"
    assert result.frame_costs[1].frame_type == "residual"
    assert len(result.chunks) == 6
    assert all(c.total_bytes > 0 for c in result.chunks)
    # a key frame's chunk must be far larger than a residual chunk (big hash
    # + full non-color for every object vs. tiny hash + a few objects)
    key_bytes = result.chunks[0].total_bytes
    residual_bytes = result.chunks[1].total_bytes
    assert key_bytes > residual_bytes


def test_rendering_pipeline_simulation_runs_and_hits_positive_fps():
    frames, cams, _ = synthetic_sequence(n_frames=4, n_objects=4, device="cuda")
    bounds_min = torch.stack([f.get_xyz.min(dim=0).values for f in frames]).min(dim=0).values
    bounds_max = torch.stack([f.get_xyz.max(dim=0).values for f in frames]).max(dim=0).values
    frames_seg = segment_sequence(frames, k=4, n_iters=10)

    cfg = VegaEncoderConfig(key_iters=30, residual_iters=15, dyn_iters=5)
    result = encode_sequence(frames_seg, bounds_min, bounds_max, config=cfg)

    eval_cam = default_eval_camera(bounds_min, bounds_max, "cuda")
    lat_model = profile_latency_model(eval_cam, sizes=[500, 2000])
    renderer = ViewAdaptiveRenderer(result.color_model, lat_model, t_deadline_ms=1000 / 30)
    play_cams = playback_camera_path(n_steps=4, device="cuda", width=128, height=128)
    bg = torch.zeros(3, device="cuda")

    dyn_by_frame = {d["frame_idx"]: d.get("per_object_dyn", {}) for d in result.dynamicity_log}
    for i, (gs, cam) in enumerate(zip(result.reconstructed, play_cams)):
        is_key = result.frame_costs[i].frame_type == "key"
        fr = renderer.render_frame(gs, cam, i, dyn_by_frame.get(i, {}), is_key, bg)
        assert fr.fps > 0
        assert fr.image.shape[0] == 3


def test_early_culling_matches_visible_object_count():
    frames, cams, _ = synthetic_sequence(n_frames=1, n_objects=4, device="cuda")
    gs = frames[0]
    boxes = gs.object_bounding_boxes()
    visible, culled = early_cull_from_boxes(boxes, cams[0])
    assert visible | culled == set(boxes.keys())
    assert len(visible) + len(culled) == gs.num_objects
