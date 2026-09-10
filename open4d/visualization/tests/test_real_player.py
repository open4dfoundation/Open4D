from __future__ import annotations

import os
import warnings

import numpy as np
import pytest

from open4d import Frame, MemoryFrameProvider, Sequence, TriangleMesh
from open4d.demo import mesh_sequence
from open4d.visualization import render_gif, visualize

pytestmark = [pytest.mark.player, pytest.mark.slow]


def test_two_sequential_viewers_use_valid_opengl_programs():
    if os.environ.get("OPEN4D_TEST_GUI") != "1":
        pytest.skip("set OPEN4D_TEST_GUI=1 in a desktop session")
    QtCore = pytest.importorskip("PyQt6.QtCore")
    QtWidgets = pytest.importorskip("PyQt6.QtWidgets")
    mesh = TriangleMesh(
        [[0.0, 0, 0], [1.0, 0, 0], [0, 1.0, 0]], [[0, 1, 2]]
    )
    sequence = Sequence(MemoryFrameProvider([Frame(0, 0.0, mesh)]))
    application = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])

    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        for index in range(2):
            QtCore.QTimer.singleShot(100, application.closeAllWindows)
            visualize(sequence, title=f"viewer {index + 1}", width=320, height=320)

    failures = [item for item in caught if issubclass(item.category, RuntimeWarning)]
    assert not failures


def test_synthetic_sequence_renders_a_gif(tmp_path):
    if os.environ.get("OPEN4D_TEST_RENDER") != "1":
        pytest.skip("set OPEN4D_TEST_RENDER=1 with a desktop or Xvfb display")
    from PIL import Image

    with mesh_sequence(side=16, frames=6, fps=10) as decoded:
        output = render_gif(
            decoded, tmp_path / "wave.gif", up="z", width=320, height=240,
            distance=1.8, elevation=30, no_metrics=True,
        )

    with Image.open(output) as image:
        assert image.size == (320, 240)
        assert image.n_frames == 6
        assert image.info["loop"] == 0
        previous = None
        for index in range(image.n_frames):
            image.seek(index)
            assert image.info["duration"] == 100
            pixels = np.asarray(image.convert("RGB"))
            foreground = np.any(pixels < 240, axis=2)
            assert 0.01 < foreground.mean() < 0.8, "geometry is missing or fills the frame"
            if previous is not None:
                changed = np.any(np.abs(pixels.astype(int) - previous) > 8, axis=2)
                assert changed.mean() > 0.005, "the rendered geometry did not move"
            previous = pixels.astype(int)


def test_sequence_with_empty_first_frame_renders(tmp_path):
    if os.environ.get("OPEN4D_TEST_RENDER") != "1":
        pytest.skip("set OPEN4D_TEST_RENDER=1 with a desktop or Xvfb display")
    from PIL import Image

    empty = TriangleMesh(np.empty((0, 3), dtype=np.float32), np.empty((0, 3), dtype=np.uint32))
    visible = mesh_sequence(side=8, frames=1)[0].geometry
    frames = Sequence(MemoryFrameProvider([Frame(0, 0, empty), Frame(1, 0.1, visible)]))
    output = render_gif(frames, tmp_path / "empty-first.gif", width=321, height=241,
                        no_metrics=True)

    with Image.open(output) as image:
        assert image.size == (321, 241)
        assert image.n_frames == 2
        assert not np.any(np.asarray(image.convert("RGB")) < 240)
        image.seek(1)
        assert np.any(np.asarray(image.convert("RGB")) < 240)
