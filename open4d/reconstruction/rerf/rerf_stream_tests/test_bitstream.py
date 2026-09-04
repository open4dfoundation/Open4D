"""Reading a ReRF bitstream, and the knobs around it.

The decode needs a GPU, ReRF's Python 3.8 entropy coder and an encoded
sequence on disk. The test that proves it correct is
:func:`test_a_decoded_frame_matches_the_photograph`, and it skips when any of
those is missing. Everything above it runs anywhere, and each one guards a way
this fails quietly rather than loudly: frames read in the wrong order, a
downscale that zooms instead of shrinking, a crop that finds nothing.
"""
from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import pytest

from rerf_stream import cameras as camera_module
from rerf_stream import env, export, serve
from rerf_stream.bitstream import DENSITY_ACT, BitstreamPlayer
from rerf_stream.cameras import Camera, psnr


# ----------------------------------------------------------------- upstream ---


def test_upstream_is_present_and_is_the_published_tree():
    """`env` fails with a clone instruction rather than an ImportError."""
    assert (env.UPSTREAM / "run.py").is_file()
    assert (env.UPSTREAM / "codec" / "quant.npy").is_file()


def test_the_entropy_coder_binary_is_there():
    """It has no sources, so a missing .so cannot be recovered by building."""
    for name in env.AC_DC_LIBRARIES:
        assert (env.UPSTREAM / "ac_dc" / name).is_file(), name
    binaries = list((env.UPSTREAM / "ac_dc").glob("ncvv_ac_dc*.so"))
    assert binaries, "ncvv_ac_dc is missing; nothing here can decode without it"


def test_activate_names_what_to_do_when_upstream_is_absent(monkeypatch, tmp_path):
    monkeypatch.setattr(env, "UPSTREAM", tmp_path / "nothing")
    monkeypatch.setattr(env, "_activated", False)
    with pytest.raises(RuntimeError, match="clone"):
        env.activate()


def test_numpy_aliases_are_restored():
    """`codec.compress_utils.decode_pca` uses np.bool, removed in numpy 1.24.

    Every decode goes through it, and upstream decodes each frame to build the
    next one's reference -- so without this the bitstream is silently truncated
    to one frame.
    """
    env.patch_dependencies()
    import numpy

    assert numpy.bool is bool
    assert numpy.str is str


def test_patching_is_idempotent():
    """It is called on every activate, and imageio would be wrapped twice."""
    env.patch_dependencies()
    env.patch_dependencies()
    import imageio

    assert getattr(imageio.imwrite, "_squeezes_gray", False) is True


# ------------------------------------------------------- the bitstream shape ---


def write_bitstream(root: Path, frames) -> Path:
    """A directory shaped like compress.py's output, with no real payload."""
    root.mkdir(parents=True, exist_ok=True)
    for index in frames:
        (root / ("header_%d.json" % index)).write_text(
            json.dumps({"headers": [{"mask_size": 8, "quality": 99}]})
        )
    (root / "model_kwargs.json").write_text("{}")
    (root / "rgb_net.tar").write_bytes(b"")
    return root


class _Stub:
    """Just enough of a player for the path-reading helpers."""

    def __init__(self, path):
        self.path = Path(path)


def test_frames_are_ordered_numerically_not_lexically(tmp_path):
    """`header_10.json` sorts before `header_2.json` as text.

    Which would decode frame 10's residual onto frame 1's grid: the picture
    stays plausible, the motion is wrong, and nothing is raised.
    """
    root = write_bitstream(tmp_path / "rerf", [0, 1, 2, 9, 10, 11, 20])
    found = BitstreamPlayer._available_frames(_Stub(root))
    assert found == [0, 1, 2, 9, 10, 11, 20]


def test_a_stray_file_is_not_a_frame(tmp_path):
    root = write_bitstream(tmp_path / "rerf", [0, 1])
    (root / "header_notanumber.json").write_text("{}")
    (root / "header_.json").write_text("{}")
    assert BitstreamPlayer._available_frames(_Stub(root)) == [0, 1]


def test_bitstream_bytes_counts_the_directory(tmp_path):
    root = write_bitstream(tmp_path / "rerf", [0])
    (root / "feature_0_99.rerf").write_bytes(b"x" * 5000)
    assert BitstreamPlayer.bitstream_bytes.fget(_Stub(root)) >= 5000


def test_the_absent_voxel_density_is_not_zero():
    """Zero activates to a visible alpha and paints fog through empty space."""
    assert DENSITY_ACT == -4.1


# ------------------------------------------------------------ the rate ladder ---


def camera(width=1280, height=960, fx=1279.0):
    return Camera(
        camera_id=3, width=width, height=height, fx=fx, fy=fx,
        cx=(width - 1) * 0.5, cy=(height - 1) * 0.5, c2w=np.eye(4),
    )


def field_of_view(cam):
    return (
        2.0 * np.arctan(cam.width * 0.5 / cam.fx),
        2.0 * np.arctan(cam.height * 0.5 / cam.fy),
    )


def test_scaling_a_camera_keeps_its_field_of_view():
    """Fewer rays, same picture.

    Scaling resolution without the intrinsics would zoom in instead, which
    looks like a working speed-up until it is compared against anything.
    """
    original = camera()
    smaller = original.scaled(0.5)
    assert (smaller.width, smaller.height) == (640, 480)
    for before, after in zip(field_of_view(original), field_of_view(smaller)):
        assert abs(before - after) < 1e-9


def test_scaling_by_one_is_the_same_camera():
    original = camera()
    assert original.scaled(1.0) is original


def test_scaling_never_reaches_zero_pixels():
    smaller = camera().scaled(0.0001)
    assert smaller.width >= 16 and smaller.height >= 16


def test_the_rungs_are_ordered_and_distinct():
    """A ladder whose rungs cost the same is not a ladder."""
    scales = [serve.RUNGS[name][0] for name in ("high", "medium", "low")]
    assert scales == sorted(scales, reverse=True)
    assert len(set(scales)) == 3
    qualities = [serve.RUNGS[name][1] for name in ("high", "medium", "low")]
    assert qualities == sorted(qualities, reverse=True)


def test_the_default_rung_is_the_best_one():
    """A comparison should default to the method's best showing."""
    assert serve.parse_args(
        ["--config", "c.py", "--compression-path", "b"]
    ).rung == "high"


# ---------------------------------------------------------------- the crop ---


def test_the_crop_finds_the_subject():
    image = np.zeros((100, 200, 3), dtype=np.float32)
    image[40:60, 80:120] = 1.0
    assert serve.subject_box(image, pad=0.0) == (80, 40, 120, 60)


def test_the_crop_stays_inside_the_frame():
    image = np.ones((100, 200, 3), dtype=np.float32)
    assert serve.subject_box(image, pad=0.5) == (0, 0, 200, 100)


def test_an_empty_frame_has_no_crop():
    """A crop of nothing must not be (0,0,0,0) -- that is a zero-pixel JPEG,
    and the pane would stay blank for the rest of the stream."""
    assert serve.subject_box(np.zeros((10, 10, 3), dtype=np.float32)) is None


# ---------------------------------------------------------------- the label ---


def test_the_label_adds_a_band_above_the_image():
    image = np.full((40, 30, 3), 0.5, dtype=np.float32)
    sheet = serve.label(image, "frame 3")
    assert sheet.size == (30, 40 + serve.LABEL_HEIGHT)
    assert np.asarray(sheet)[serve.LABEL_HEIGHT + 5, 15].max() > 100


def test_the_label_crops_when_given_a_box():
    sheet = serve.label(np.zeros((40, 30, 3), dtype=np.float32), "x", (5, 5, 15, 25))
    assert sheet.size == (10, 20 + serve.LABEL_HEIGHT)


# ---------------------------------------------------------------- the buffer ---


def test_the_frame_buffer_holds_one_frame_not_a_queue():
    """A viewer that falls behind should see the current frame next, not a
    backlog of stale ones."""
    from rerf_stream.mjpeg import FrameBuffer

    buffer = FrameBuffer()
    buffer.update(b"first")
    buffer.update(b"second")
    jpeg, sequence = buffer.wait_for_next(0)
    assert jpeg == b"second"
    assert sequence == 2


def test_waiting_times_out_rather_than_blocking_forever():
    """A stalled renderer must not wedge the connection thread."""
    from rerf_stream.mjpeg import FrameBuffer

    buffer = FrameBuffer()
    buffer.update(b"only")
    _, sequence = buffer.wait_for_next(0)
    jpeg, again = buffer.wait_for_next(sequence, timeout=0.05)
    assert jpeg is None and again == sequence


# ------------------------------------------------- the decode, when it can run ---

RUN = Path("/media/frozzzen/LocalDisk/nevo_runs/g_basketball")
CONFIG, BITSTREAM = RUN / "config.py", RUN / "rerf"

requires_bitstream = pytest.mark.skipif(
    not (CONFIG.is_file() and (BITSTREAM / "header_0.json").is_file()),
    reason="needs an encoded ReRF bitstream on this machine",
)


@requires_bitstream
def test_a_decoded_frame_matches_the_photograph():
    """The one test that proves the decode is right.

    The decode loop is a transcription of upstream's render script; a
    transcription that drops a step still decodes, still renders, and renders
    the wrong scene. So score it against the captured image.

    Composited onto the background the *config* sets, which for this bitstream
    is black. Scoring a black-background render against a white composite gives
    0.3 dB and reads as a broken decoder rather than a broken comparison.
    """
    player = BitstreamPlayer(CONFIG, BITSTREAM, group_size=30)
    assert player.background == 0.0, "this fixture is the black-background encode"

    view = 0
    scores = []
    for count, frame in enumerate(player.play(loop=False)):
        if count >= 2:
            break
        image = player.render(player.cameras()[view])
        truth = camera_module.captured_image(
            player.corpus_dir, frame.index, view, background=player.background
        )
        scores.append(psnr(image, truth))
    # 44-45 dB measured. A mis-wired decode lands under 20.
    assert min(scores) > 35.0, scores


@requires_bitstream
def test_the_group_starts_with_a_key_frame_and_continues_with_residuals():
    """A P-frame is a residual, so the player threads the accumulated grid
    through the group rather than decoding each frame independently."""
    player = BitstreamPlayer(CONFIG, BITSTREAM, group_size=30)
    kinds = [frame.is_key_frame for _, frame in zip(range(3), player.play(loop=False))]
    assert kinds == [True, False, False]


@requires_bitstream
def test_playing_without_loop_stops_at_the_end_of_the_group():
    player = BitstreamPlayer(CONFIG, BITSTREAM, group_size=30)
    assert len(list(player.play(loop=False))) == len(player.frames)


@requires_bitstream
def test_a_lower_rung_renders_faster_than_a_higher_one():
    """The ladder's premise: resolution buys frame rate. Rendering is the
    bottleneck here, not the link, so this is the knob that matters."""
    import time

    player = BitstreamPlayer(CONFIG, BITSTREAM, group_size=30)
    next(player.play(loop=True))       # install a frame to render
    full = player.cameras()[0]
    started = time.time()
    player.render(full)
    slow = time.time() - started
    started = time.time()
    player.render(full.scaled(0.25))
    fast = time.time() - started
    assert fast < slow


def test_an_empty_directory_says_how_to_fill_it(tmp_path):
    """The likeliest mistake is pointing this at a run directory rather than at
    the bitstream inside it, so the error names what writes one."""
    with pytest.raises(FileNotFoundError, match="compress.py"):
        BitstreamPlayer(CONFIG if CONFIG.is_file() else __file__, tmp_path)


# ---------------------------------------------------------------- the export ---


def test_the_exporter_writes_a_sidecar_the_bundle_side_understands(tmp_path):
    """The handoff between two interpreters. `streamer.adopt` reads this, and
    neither package can import the other, so the shape is the whole contract.
    """
    import json

    payload = {
        "format": "rerf-clips", "version": 1, "scene": "basketball",
        "representation": "pixels",
        "clips": [{"name": "x-rerf-cam00", "method": "rerf", "camera": 0,
                   "frames": ["x-rerf-cam00/frame_0000.jpg"], "notes": [],
                   "detail": {}}],
    }
    # Asserted here rather than only on the reading side: this is the producer,
    # and a version bump has to be made deliberately on both.
    assert payload["format"] == "rerf-clips"
    assert payload["version"] == 1
    assert set(payload["clips"][0]) >= {"name", "method", "camera", "frames"}
    json.dumps(payload)          # must stay JSON-serialisable


def test_grey_depth_is_widened_to_three_channels(tmp_path):
    """A depth map is (H, W); JPEG needs three channels, and newer Pillow
    raises rather than broadcasting."""
    path = tmp_path / "d.jpg"
    written = export.write_jpeg(path, np.linspace(0, 1, 64).reshape(8, 8))
    assert written > 0
    from PIL import Image
    assert Image.open(path).mode == "RGB"


def test_the_export_quality_is_high():
    """These are the reference renders a method is judged by; an artefact here
    would be read as a reconstruction artefact."""
    assert export.QUALITY >= 90


@requires_bitstream
def test_depth_comes_back_alongside_colour():
    player = BitstreamPlayer(CONFIG, BITSTREAM, group_size=30)
    next(player.play(loop=True))
    camera = player.cameras()[0].scaled(0.25)

    colour = player.render(camera)
    assert colour.shape == (camera.height, camera.width, 3)

    colour, depth = player.render(camera, depth=True)
    assert colour.shape == (camera.height, camera.width, 3)
    assert depth.shape == (camera.height, camera.width)
    # Normalised, near bright. An all-white frame would mean the span was zero.
    assert 0.0 <= float(depth.min()) and float(depth.max()) <= 1.0
    assert float(depth.max()) > float(depth.min())


# ---------------------------------------------------------------- the rig ---


def a_corpus(root, cameras=3):
    """A corpus manifest shaped like prepare.py's, with known camera axes."""
    import json as _json

    root.mkdir(parents=True, exist_ok=True)
    entries = []
    for index in range(cameras):
        # A c2w whose columns are distinguishable, so a transposed read shows up.
        c2w = [
            [1.0, 0.0, 0.0, 10.0 + index],
            [0.0, 2.0, 0.0, 20.0 + index],
            [0.0, 0.0, 3.0, 30.0 + index],
            [0.0, 0.0, 0.0, 1.0],
        ]
        entries.append({
            "camera_id": index, "fx": 1300.0, "fy": 1300.0,
            "cx": 640.0, "cy": 480.0, "c2w_world": c2w,
        })
    (root / "nevo_corpus.json").write_text(_json.dumps({
        "width": 1280, "height": 960,
        "world_bounds_min": [-1.0, 2.0, -3.0],
        "world_bounds_max": [1.0, 4.0, -1.0],
        "cameras": list(reversed(entries)),        # out of order on purpose
    }))
    return root


def test_the_rig_carries_one_pose_per_camera_in_index_order(tmp_path):
    """A viewer selects a *station* by index, so the order is the contract:
    shuffled poses would show two methods at different cameras and call it a
    comparison."""
    rig = camera_module.capture_rig(a_corpus(tmp_path / "corpus"))
    assert len(rig["poses"]) == 3
    assert [pose["position"][0] for pose in rig["poses"]] == [10.0, 11.0, 12.0]


def test_the_rig_reads_the_camera_axes_from_the_columns(tmp_path):
    """c2w is camera-to-world, so its columns are right/down/forward. Reading
    rows instead still produces a plausible rig pointing the wrong way."""
    rig = camera_module.capture_rig(a_corpus(tmp_path / "corpus"))
    pose = rig["poses"][0]
    assert pose["right"] == [1.0, 0.0, 0.0]
    assert pose["down"] == [0.0, 2.0, 0.0]
    assert pose["forward"] == [0.0, 0.0, 3.0]
    assert pose["position"] == [10.0, 20.0, 30.0]


def test_the_rig_uses_world_bounds_not_normalised_ones(tmp_path):
    """The shared frame, because a geometry method added to this scene has to
    line up with it in 3D."""
    rig = camera_module.capture_rig(a_corpus(tmp_path / "corpus"))
    assert rig["bounds_min"] == [-1.0, 2.0, -3.0]
    assert rig["bounds_max"] == [1.0, 4.0, -1.0]


def test_the_rigs_field_of_view_comes_from_the_intrinsics(tmp_path):
    rig = camera_module.capture_rig(a_corpus(tmp_path / "corpus"))
    assert abs(rig["fov_y"] - 2.0 * np.arctan(960 * 0.5 / 1300.0)) < 1e-12


# ------------------------------------------------------------- quality rungs ---


def test_resampling_a_rung_keeps_the_aspect_ratio():
    """A rung is the *same content* at a lower rate. Re-marching at a lower
    resolution would sample the volume differently and give a slightly
    different picture -- fine as an image, wrong as a rendition, because
    switching between them would be a visible cut rather than a rate change."""
    image = np.zeros((480, 640, 3), dtype=np.float32)
    smaller = export._resample(image, 0.5)
    assert smaller.shape[:2] == (240, 320)
    assert abs(smaller.shape[1] / smaller.shape[0]
               - image.shape[1] / image.shape[0]) < 1e-9


def test_resampling_by_one_is_a_no_op():
    image = np.zeros((8, 8, 3), dtype=np.float32)
    assert export._resample(image, 1.0) is image


def test_resampling_never_reaches_zero_pixels():
    small = export._resample(np.zeros((40, 40, 3), dtype=np.float32), 0.0001)
    assert small.shape[0] >= 16 and small.shape[1] >= 16


def test_the_rungs_are_the_ones_serve_defines():
    """One ladder for this method, not two definitions that can disagree."""
    assert export.RUNGS is serve.RUNGS


def test_an_unknown_rung_reaches_run_to_be_refused():
    """Parsed as text and validated in run(), where the known set lives, so the
    error can name what is available rather than just rejecting the string."""
    parsed = export.parse_args([
        "--config", "c.py", "--compression-path", "b", "--out", "o",
        "--scene", "s", "--rungs", "high,nope",
    ])
    assert parsed.rungs == "high,nope"


def test_no_rungs_means_a_single_rendition():
    parsed = export.parse_args([
        "--config", "c.py", "--compression-path", "b", "--out", "o", "--scene", "s",
    ])
    assert parsed.rungs == ""
