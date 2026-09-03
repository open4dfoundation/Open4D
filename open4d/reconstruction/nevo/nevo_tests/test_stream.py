"""Streaming the bitstream: the structure it reads, and the knobs around it.

The decode itself needs a GPU, ReRF's CPython 3.8 entropy coder and an encoded
sequence on disk, so the test that proves it correct is the one at the bottom
that scores a decoded frame against the captured image -- 44.5 dB on
``g_basketball`` -- and it skips when any of those is missing.

Everything above it is what can be checked without a GPU and is worth checking
anyway, because each one is a way a live stream goes wrong quietly rather than
loudly: a bitstream whose frames are read in the wrong order, a downscale that
changes the view instead of the resolution, a crop that finds nothing.
"""
from __future__ import annotations

import json
import shutil
from pathlib import Path

import numpy as np
import pytest

from nevo.cameras import Camera

# The CLI pulls in Vega's MJPEG server; the decoder does not. Import them
# separately so a missing sibling baseline cannot hide the decoder's tests.
from nevo import stream as stream_module

live_stream = pytest.importorskip(
    "orbitnevo.live_stream", reason="needs vega.streaming beside this baseline"
)


# --------------------------------------------------------- the bitstream shape ---


def write_bitstream(root: Path, frames) -> Path:
    """A directory shaped like one ``compress.py`` writes, with no real payload."""
    root.mkdir(parents=True, exist_ok=True)
    for index in frames:
        (root / ("header_%d.json" % index)).write_text(
            json.dumps({"headers": [{"mask_size": 8, "quality": 99}]})
        )
    (root / "model_kwargs.json").write_text("{}")
    (root / "rgb_net.tar").write_bytes(b"")
    return root


def test_frames_are_ordered_numerically_not_lexically(tmp_path):
    """``header_10.json`` sorts before ``header_2.json`` as text.

    Which would decode frame 10's residual onto frame 1's grid: the picture
    stays plausible and the motion is wrong, with nothing raised anywhere.
    """
    root = write_bitstream(tmp_path / "rerf", [0, 1, 2, 9, 10, 11, 20])
    found = stream_module.BitstreamPlayer._available_frames.__get__(
        _Stub(root)
    )()
    assert found == [0, 1, 2, 9, 10, 11, 20]


def test_a_stray_file_is_not_a_frame(tmp_path):
    root = write_bitstream(tmp_path / "rerf", [0, 1])
    (root / "header_notanumber.json").write_text("{}")
    (root / "header_.json").write_text("{}")
    assert stream_module.BitstreamPlayer._available_frames.__get__(_Stub(root))() == [0, 1]


class _Stub:
    """Just enough of a player for the path-reading helpers."""

    def __init__(self, path):
        self.path = Path(path)


def test_bitstream_bytes_counts_the_whole_directory(tmp_path):
    root = write_bitstream(tmp_path / "rerf", [0, 1])
    (root / "feature_0_99.rerf").write_bytes(b"x" * 1000)
    total = stream_module.BitstreamPlayer.bitstream_bytes.fget(_Stub(root))
    assert total >= 1000


# ------------------------------------------------------------------ downscaling ---


def camera(width=1280, height=960, fx=1279.0):
    return Camera(
        camera_id=3,
        width=width,
        height=height,
        fx=fx,
        fy=fx,
        cx=(width - 1) * 0.5,
        cy=(height - 1) * 0.5,
        c2w=np.eye(4),
    )


def field_of_view(cam):
    return (
        2.0 * np.arctan(cam.width * 0.5 / cam.fx),
        2.0 * np.arctan(cam.height * 0.5 / cam.fy),
    )


def test_scaling_a_camera_keeps_its_field_of_view():
    """The whole point of --scale: fewer rays, same picture.

    Scaling the resolution without the intrinsics would zoom in instead, which
    looks like a working speed-up until it is compared against anything.
    """
    original = camera()
    smaller = live_stream.scaled(original, 0.5)
    assert (smaller.width, smaller.height) == (640, 480)
    for before, after in zip(field_of_view(original), field_of_view(smaller)):
        assert abs(before - after) < 1e-9


def test_scaling_by_one_returns_the_same_camera():
    original = camera()
    assert live_stream.scaled(original, 1.0) is original


def test_scaling_never_reaches_zero_pixels():
    """A pathological --scale should render something small, not divide by zero."""
    smaller = live_stream.scaled(camera(), 0.0001)
    assert smaller.width >= 16 and smaller.height >= 16


# ----------------------------------------------------------------- the crop box ---


def test_the_crop_finds_the_subject():
    image = np.zeros((100, 200, 3), dtype=np.float32)
    image[40:60, 80:120] = 1.0
    left, top, right, bottom = live_stream.subject_box(image, pad=0.0)
    assert (left, top, right, bottom) == (80, 40, 120, 60)


def test_the_crop_pads_outward_and_stays_inside_the_frame():
    image = np.zeros((100, 200, 3), dtype=np.float32)
    image[0:100, 0:200] = 1.0
    left, top, right, bottom = live_stream.subject_box(image, pad=0.5)
    assert (left, top) == (0, 0)
    assert (right, bottom) == (200, 100)


def test_an_empty_frame_has_no_crop():
    """Frame 0 of a stream can be empty; a crop of nothing must not be (0,0,0,0),
    which would send a zero-pixel JPEG and blank the pane for good."""
    assert live_stream.subject_box(np.zeros((10, 10, 3), dtype=np.float32)) is None


# -------------------------------------------------------------------- the label ---


def test_the_label_adds_a_caption_band_and_keeps_the_pixels():
    image = np.zeros((40, 30, 3), dtype=np.float32)
    image[:, :] = 0.5
    sheet = live_stream.label(image, "frame 3")
    assert sheet.size == (30, 40 + live_stream.LABEL_HEIGHT)
    # The image lands below the band, not over it.
    assert np.asarray(sheet)[live_stream.LABEL_HEIGHT + 5, 15].max() > 100


def test_the_label_crops_when_given_a_box():
    image = np.zeros((40, 30, 3), dtype=np.float32)
    sheet = live_stream.label(image, "x", (5, 5, 15, 25))
    assert sheet.size == (10, 20 + live_stream.LABEL_HEIGHT)


# ------------------------------------------------------------------ the CLI ---


def test_the_defaults_describe_a_working_stream():
    args = live_stream.parse_args(
        ["--config", "c.py", "--compression-path", "b"]
    )
    assert args.scale == 1.0
    assert args.pca_chs == "7,13"          # must match the encode
    assert args.no_pca is False            # this repository encodes with pca
    assert args.max_fps == 0.0             # as fast as it renders


def test_orbit_is_off_by_default():
    """A moving camera is a demo choice, not the default a comparison wants."""
    args = live_stream.parse_args(["--config", "c.py", "--compression-path", "b"])
    assert args.orbit is False


# ------------------------------------------------- the decode, when it can run ---

RUN = Path("/media/frozzzen/LocalDisk/nevo_runs/g_basketball")
CONFIG, BITSTREAM = RUN / "config.py", RUN / "rerf"

requires_bitstream = pytest.mark.skipif(
    not (CONFIG.is_file() and (BITSTREAM / "header_0.json").is_file()),
    reason="needs an encoded ReRF bitstream on this machine",
)


@requires_bitstream
def test_a_decoded_frame_matches_the_captured_image():
    """The one test that proves the transcription is right.

    ``mmap_decode`` was copied out of a script into `nevo.stream`; a copy that
    drops a step still decodes, still renders, and renders the wrong scene. So
    score it against the photograph, the same argument `nevo.render`'s module
    docstring makes about reassembling a checkpoint.

    Composited onto **black**, because this bitstream's config has
    ``white_bkgd=False``. Scoring against the white composite gives 0.3 dB and
    looks like a broken decoder rather than a broken test -- which is exactly
    what it did the first time.
    """
    from PIL import Image

    from nevo.render import psnr, render_view

    player = stream_module.BitstreamPlayer(CONFIG, BITSTREAM, group_size=30)
    assert player.render_kwargs()["bg"] == 0, "this fixture is the black-bg encode"

    cameras = player.cameras()
    entries = sorted(
        json.load(open(player.corpus_dir / "cams_0.json"))["frames"],
        key=lambda item: item["file"],
    )
    scores = []
    for count, frame in enumerate(player.play(loop=False)):
        if count >= 2:
            break
        image = render_view(player, frame, cameras[0])
        entry = entries[0]
        rgb = np.asarray(Image.open(entry["file"]).convert("RGB"), np.float32) / 255.0
        alpha = np.asarray(Image.open(entry["mask"]).convert("L"), np.float32)[..., None] / 255.0
        scores.append(psnr(image, rgb * alpha))
    # 44-45 dB measured. A mis-wired decode lands under 20.
    assert min(scores) > 35.0, scores


@requires_bitstream
def test_the_first_frame_of_a_group_decodes_without_a_predecessor():
    """And the ones after it do not: a P-frame is a residual, so the player has
    to thread the accumulated grid through the group."""
    player = stream_module.BitstreamPlayer(CONFIG, BITSTREAM, group_size=30)
    kinds = []
    for count, frame in enumerate(player.play(loop=False)):
        if count >= 3:
            break
        kinds.append(frame.is_key_frame)
    assert kinds == [True, False, False]


@requires_bitstream
def test_playing_without_loop_stops_at_the_end_of_the_group():
    player = stream_module.BitstreamPlayer(CONFIG, BITSTREAM, group_size=30)
    assert len(list(player.play(loop=False))) == len(player.frames)


def test_an_empty_directory_says_how_to_fill_it(tmp_path):
    """The likeliest mistake is pointing this at a run dir rather than at the
    bitstream inside it, so the error names the command that makes one."""
    with pytest.raises(FileNotFoundError, match="compress.py"):
        stream_module.BitstreamPlayer(CONFIG if CONFIG.is_file() else __file__, tmp_path)
