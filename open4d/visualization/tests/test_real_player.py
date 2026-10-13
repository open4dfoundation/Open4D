from __future__ import annotations

import os
import warnings
import threading

import numpy as np
import pytest

from open4d import Frame, MemoryFrameProvider, Sequence, TriangleMesh
from open4d.demo import mesh_sequence
from open4d.visualization import Visualizer, render_gif, visualize

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


def test_live_viewer_renders_interpolated_texture_and_baseline():
    if os.environ.get("OPEN4D_TEST_RENDER") != "1":
        pytest.skip("set OPEN4D_TEST_RENDER=1 with a desktop or Xvfb display")
    pytest.importorskip("PyQt6.QtWidgets")
    mesh = TriangleMesh(
        [[-1., -1, 0], [1., -1, 0], [1., 1, 0], [-1., 1, 0]], [[0, 1, 2], [0, 2, 3]],
        texture_coordinates=[[[0., 0], [1., 0], [1., 1]], [[0., 0], [1., 1], [0., 1]]],
    )
    # Every corner is red, but the center is green: sampled vertex colors alone
    # would render no green at all, so this proves fragment texture sampling.
    texture = np.full((32, 32, 3), [240, 20, 20], dtype=np.uint8)
    texture[8:24, 8:24] = [20, 230, 20]
    with Visualizer(width=800, height=400, elevation=90, azimuth=0, distance=2.5) as viewer:
        viewer.add_geometry(mesh, texture=texture)
        viewer.add_geometry(mesh, baseline=True)
        assert viewer.poll_events()
        textured = viewer.capture_screen()
        reference = viewer.capture_screen(baseline=True)
        green = (textured[:, :, 1] > 180) & (textured[:, :, 0] < 80)
        red = (textured[:, :, 0] > 180) & (textured[:, :, 1] < 80)
        assert green.mean() > 0.02 and red.mean() > 0.02
        assert not np.any((reference[:, :, 1] > 180) & (reference[:, :, 0] < 80))
        # Stream updates preserve GPU texture state, and explicitly clearing it
        # recreates a normal mesh item without leaving the old texture visible.
        viewer.update_geometry(mesh, texture=None)
        untextured = viewer.capture_screen()
        assert not np.any((untextured[:, :, 1] > 180) & (untextured[:, :, 0] < 80))


def test_live_viewer_displays_a_real_tcp_stream():
    if os.environ.get("OPEN4D_TEST_RENDER") != "1":
        pytest.skip("set OPEN4D_TEST_RENDER=1 with a desktop or Xvfb display")
    from open4d.transport import receive, send

    mesh = TriangleMesh([[-1., -1, 0], [1., -1, 0], [0., 1, 0]], [[0, 1, 2]],
                        colors=[[0., 0, 1], [0., 0, 1], [0., 0, 1]])
    source = Sequence(MemoryFrameProvider([Frame(index, index / 30, mesh) for index in range(3)]))
    sent, errors = [], []
    with receive(port=0, timeout=3) as receiver:
        def publish():
            try:
                sent.append(send(source, *receiver.address, realtime=False))
            except Exception as error:
                errors.append(error)

        sender = threading.Thread(target=publish)
        sender.start()
        with Visualizer(width=320, height=240, elevation=90, distance=2.5) as viewer:
            assert viewer.stream(receiver) == 3
            pixels = viewer.capture_screen()
            blue = (pixels[:, :, 2] > 180) & (pixels[:, :, 0] < 80)
            assert blue.mean() > 0.01
        sender.join(timeout=3)
    assert not sender.is_alive() and not errors and sent == [3]
    assert not source.closed
