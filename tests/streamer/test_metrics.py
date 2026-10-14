"""Scoring what the viewer received.

The SSIM here is a reimplementation, kept so `streamer` needs nothing beyond
`open4d`. A reimplementation is a liability, so it is held to scikit-image's
wherever that is installed -- which is the only test in this file that could
not be written by reading the code it tests.
"""
from __future__ import annotations

import json
import math

import numpy as np
import pytest

from streamer import bundle, metrics

pytestmark = pytest.mark.cpu


# ---------------------------------------------------------------- the metrics ---


def test_psnr_of_identical_images_is_infinite():
    image = np.random.default_rng(0).random((16, 16, 3))
    assert metrics.psnr(image, image) == float("inf")


def test_psnr_matches_the_definition():
    a = np.zeros((8, 8))
    b = np.full((8, 8), 0.1)
    # mse = 0.01, so 10*log10(1/0.01) = 20 dB exactly.
    assert abs(metrics.psnr(a, b) - 20.0) < 1e-9


def test_psnr_falls_as_error_grows():
    rng = np.random.default_rng(1)
    truth = rng.random((32, 32, 3))
    close = np.clip(truth + rng.normal(0, 0.01, truth.shape), 0, 1)
    far = np.clip(truth + rng.normal(0, 0.10, truth.shape), 0, 1)
    assert metrics.psnr(close, truth) > metrics.psnr(far, truth)


def test_ssim_of_identical_images_is_one():
    image = np.random.default_rng(2).random((32, 32))
    assert abs(metrics.ssim(image, image) - 1.0) < 1e-9


def test_ssim_rejects_a_shape_mismatch():
    with pytest.raises(ValueError, match="shapes differ"):
        metrics.ssim(np.zeros((8, 8)), np.zeros((9, 9)))


def test_ssim_is_lower_for_a_structural_change_than_for_a_brightness_shift():
    """The property the metric exists for: PSNR cannot tell these apart as
    well, because a uniform shift and scrambled detail can carry the same MSE.
    """
    rng = np.random.default_rng(3)
    truth = rng.random((64, 64))
    shifted = np.clip(truth + 0.05, 0, 1)
    scrambled = truth.copy()
    scrambled[::2] = rng.random(scrambled[::2].shape)
    assert metrics.ssim(scrambled, truth) < metrics.ssim(shifted, truth)


@pytest.mark.parametrize("shape", [(64, 64), (48, 72)])
def test_ssim_agrees_with_scikit_image(shape):
    """The check that makes the reimplementation defensible.

    Same formulation: Gaussian window, sigma 1.5, population covariance. If
    this drifts, the number this package reports is not SSIM as anyone else
    computes it, and cross-paper comparison silently breaks.
    """
    skimage = pytest.importorskip("skimage.metrics")
    rng = np.random.default_rng(4)
    truth = rng.random(shape)
    prediction = np.clip(truth + rng.normal(0, 0.05, shape), 0, 1)

    theirs = skimage.structural_similarity(
        prediction, truth, data_range=1.0, gaussian_weights=True,
        sigma=metrics.SIGMA, use_sample_covariance=False,
    )
    assert abs(metrics.ssim(prediction, truth) - theirs) < 1e-4


def test_ssim_agrees_with_scikit_image_on_colour():
    skimage = pytest.importorskip("skimage.metrics")
    rng = np.random.default_rng(5)
    truth = rng.random((40, 40, 3))
    prediction = np.clip(truth + rng.normal(0, 0.04, truth.shape), 0, 1)
    theirs = skimage.structural_similarity(
        prediction, truth, data_range=1.0, gaussian_weights=True,
        sigma=metrics.SIGMA, use_sample_covariance=False, channel_axis=-1,
    )
    assert abs(metrics.ssim(prediction, truth) - theirs) < 1e-4


# ------------------------------------------------------------------ pairing ---


def write_frames(root, name, count, *, size=(64, 48), shift=0.0, seed=0):
    from PIL import Image

    rng = np.random.default_rng(seed)
    paths = []
    for index in range(count):
        array = np.clip(rng.random((size[1], size[0], 3)) * 0.5 + shift, 0, 1)
        relative = f"{name}/frame_{index:04d}.png"
        target = root / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        Image.fromarray((array * 255).astype(np.uint8)).save(target)
        paths.append(relative)
    return paths


def a_bundle(tmp_path, *, clips):
    bundle.write(tmp_path, title="t", source="s", clips=clips)
    return tmp_path


def a_scene(tmp_path, *, sizes=None, seed=0):
    """One scene, one station: a reference and a reconstruction of it."""
    sizes = sizes or {"captured": (64, 48), "method": (64, 48)}
    reference = write_frames(tmp_path, "ref-cam00", 6,
                             size=sizes["captured"], seed=seed)
    rendered = write_frames(tmp_path, "m-cam00", 6, size=sizes["method"],
                            shift=0.02, seed=seed)
    return a_bundle(tmp_path, clips=[
        bundle.Clip(name="ref-cam00", representation="pixels", scene="s1",
                    method="captured", camera=0, frames=reference),
        bundle.Clip(name="m-cam00", representation="pixels", scene="s1",
                    method="mine", camera=0, frames=rendered),
    ])


def test_a_reconstruction_is_paired_with_the_reference_at_its_station(tmp_path):
    report = metrics.measure(a_scene(tmp_path), every=2)
    assert len(report.scores) == 1
    score = report.scores[0]
    assert (score.clip, score.reference) == ("m-cam00", "ref-cam00")
    assert score.method == "mine"
    assert score.frames == 3          # 6 frames, every 2
    assert math.isfinite(score.psnr)
    assert 0.0 <= score.ssim <= 1.0


def test_the_reference_is_not_scored_against_itself(tmp_path):
    report = metrics.measure(a_scene(tmp_path))
    assert all(score.method != "captured" for score in report.scores)


def test_a_clip_at_a_station_with_no_reference_is_reported_not_skipped(tmp_path):
    """Silently omitting it would make a bundle look fully measured when a
    method's numbers are simply absent."""
    frames = write_frames(tmp_path, "m-cam03", 4)
    root = a_bundle(tmp_path, clips=[
        bundle.Clip(name="m-cam03", representation="pixels", scene="s1",
                    method="mine", camera=3, frames=frames),
    ])
    report = metrics.measure(root)
    assert report.scores == []
    assert len(report.unmeasured) == 1
    assert "no captured clip" in report.unmeasured[0]["why"]


def test_geometry_clips_are_reported_as_needing_a_renderer(tmp_path):
    """A Gaussian clip has no pixels to score until something rasterises it."""
    frames = write_frames(tmp_path, "ref-cam00", 2)
    root = a_bundle(tmp_path, clips=[
        bundle.Clip(name="ref-cam00", representation="pixels", scene="s1",
                    method="captured", camera=0, frames=frames),
        bundle.Clip(name="splats", representation="gaussians", scene="s1",
                    method="vega", camera=0, frames=["splats/frame_0000.ply"]),
    ])
    report = metrics.measure(root)
    assert report.scores == []
    why = report.unmeasured[0]["why"]
    assert "gaussians" in why and "renders it" in why


def test_a_live_clip_is_reported_as_having_nothing_to_score(tmp_path):
    from streamer import live

    frames = write_frames(tmp_path, "ref-cam00", 2)
    root = a_bundle(tmp_path, clips=[
        bundle.Clip(name="ref-cam00", representation="pixels", scene="s1",
                    method="captured", camera=0, frames=frames),
        live.mjpeg("http://127.0.0.1:9/stream", name="feed", origin="rendered",
                   scene="s1"),
    ])
    report = metrics.measure(root)
    assert any("live stream" in entry["why"] for entry in report.unmeasured)


def test_a_size_mismatch_is_resampled_and_flagged(tmp_path):
    """It has to be measurable, but a resized comparison is not level with the
    rest, so the report says so rather than burying it."""
    root = a_scene(tmp_path, sizes={"captured": (64, 48), "method": (32, 24)})
    report = metrics.measure(root, every=3)
    assert report.scores[0].resized == "32x24 -> 64x48"
    assert "resampled" in metrics.render_table(report)


def test_matching_sizes_are_not_flagged(tmp_path):
    report = metrics.measure(a_scene(tmp_path), every=3)
    assert report.scores[0].resized is None
    assert "resampled" not in metrics.render_table(report)


# ------------------------------------------------------------------ sampling ---


def test_sampling_is_deterministic(tmp_path):
    """A research number that moves between runs is not a number."""
    root = a_scene(tmp_path)
    first = metrics.measure(root, every=2).scores[0]
    second = metrics.measure(root, every=2).scores[0]
    assert first.psnr == second.psnr
    assert first.ssim == second.ssim


def test_every_must_be_at_least_one(tmp_path):
    with pytest.raises(ValueError, match="at least 1"):
        metrics.measure(a_scene(tmp_path), every=0)


def test_limit_caps_the_frames_scored(tmp_path):
    assert metrics.measure(a_scene(tmp_path), every=1, limit=2).scores[0].frames == 2


def test_scene_filter_selects_one_scene(tmp_path):
    root = a_scene(tmp_path)
    assert metrics.measure(root, scene="s1").scores
    assert metrics.measure(root, scene="nope").scores == []


def test_a_bundle_is_required(tmp_path):
    with pytest.raises(FileNotFoundError, match="view.json"):
        metrics.measure(tmp_path / "nothing")


# -------------------------------------------------------------------- output ---


def test_the_json_report_round_trips(tmp_path):
    report = metrics.measure(a_scene(tmp_path), every=3)
    payload = json.loads(json.dumps(report.as_dict()))
    assert payload["scores"][0]["clip"] == "m-cam00"
    assert "psnr" in payload["scores"][0] and "ssim" in payload["scores"][0]


def test_the_table_rolls_up_by_scene_and_by_method(tmp_path):
    table = metrics.render_table(metrics.measure(a_scene(tmp_path), every=3))
    assert "s1" in table and "mine" in table
    assert "all" in table            # the cross-scene rollup


def test_the_per_clip_table_names_each_clip(tmp_path):
    table = metrics.render_table(
        metrics.measure(a_scene(tmp_path), every=3), per_clip=True
    )
    assert "m-cam00" in table


# --------------------------------------------------- what a clip is a picture of ---


def test_a_clip_that_depicts_something_else_is_not_scored(tmp_path):
    """The bug this guards: a depth map paired with a colour photograph scores
    0.4 dB, which reads as a method that catastrophically failed rather than as
    a comparison that was never meaningful."""
    reference = write_frames(tmp_path, "ref-cam00", 4)
    depth = write_frames(tmp_path, "d-cam00", 4, shift=0.4)
    root = a_bundle(tmp_path, clips=[
        bundle.Clip(name="ref-cam00", representation="pixels", scene="s1",
                    method="captured", camera=0, frames=reference),
        bundle.Clip(name="d-cam00", representation="pixels", scene="s1",
                    method="mine-depth", camera=0, frames=depth,
                    detail={"depicts": "depth"}),
    ])
    report = metrics.measure(root, every=2)
    assert report.scores == []
    assert "depicts depth" in report.unmeasured[0]["why"]


def test_a_clip_that_says_nothing_is_treated_as_appearance(tmp_path):
    """Backwards compatible: older bundles and methods that never considered
    this are measured exactly as before."""
    assert metrics.depicts({"detail": {}}) == metrics.APPEARANCE
    assert metrics.depicts({}) == metrics.APPEARANCE
    assert metrics.measure(a_scene(tmp_path), every=3).scores


def test_depicts_reads_the_declared_value():
    assert metrics.depicts({"detail": {"depicts": "depth"}}) == "depth"


def test_both_blur_backends_agree():
    """The fallback exists so `streamer` needs no scipy; this is what makes it
    safe to prefer scipy when it is there. If they diverge, the number depends
    on which machine ran it."""
    pytest.importorskip("scipy.ndimage")
    rng = np.random.default_rng(6)
    kernel = metrics._gaussian_kernel(metrics.SIGMA, metrics.TRUNCATE)
    for shape in ((40, 56), (40, 56, 3)):
        image = rng.random(shape)
        fast = metrics._blur_scipy(image)
        slow = metrics._blur_numpy(image, kernel)
        assert fast.shape == image.shape
        assert np.abs(fast - slow).max() < 1e-10, shape


def test_ssim_is_unchanged_without_scipy(monkeypatch):
    """Force the fallback and check it lands in the same place."""
    pytest.importorskip("skimage.metrics")
    import builtins

    rng = np.random.default_rng(7)
    truth = rng.random((48, 48, 3))
    prediction = np.clip(truth + rng.normal(0, 0.03, truth.shape), 0, 1)
    with_scipy = metrics.ssim(prediction, truth)

    real_import = builtins.__import__

    def no_scipy(name, *args, **kwargs):
        if name.startswith("scipy"):
            raise ImportError("pretend scipy is absent")
        return real_import(name, *args, **kwargs)

    monkeypatch.setattr(builtins, "__import__", no_scipy)
    assert abs(metrics.ssim(prediction, truth) - with_scipy) < 1e-10
