from __future__ import annotations

import os
import warnings
import threading
import gc
import weakref
import subprocess
import sys

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


def test_owned_texture_is_rendered_and_updated_without_a_separate_image_argument(tmp_path):
    if os.environ.get("OPEN4D_TEST_RENDER") != "1":
        pytest.skip("set OPEN4D_TEST_RENDER=1 with a desktop or Xvfb display")
    from dataclasses import replace
    from open4d.core import MeshTexture, TextureImage
    from PIL import Image
    mesh = TriangleMesh([[-1., -1, 0], [1., -1, 0], [1., 1, 0], [-1., 1, 0]],
                        [[0, 1, 2], [0, 2, 3]],
                        texture_coordinates=[[0., 0], [1., 0], [1., 1], [0., 1]])
    green = np.full((32, 32, 3), [20, 230, 20], np.uint8)
    red = np.full((32, 32, 3), [230, 20, 20], np.uint8)
    first = replace(mesh, texture=MeshTexture((TextureImage(green),)))
    second = replace(mesh, texture=MeshTexture((TextureImage(red),)))
    with Visualizer(width=320, height=240, elevation=90, distance=2.5) as viewer:
        viewer.add_geometry(first)
        pixels = viewer.capture_screen()
        assert ((pixels[:, :, 1] > 180) & (pixels[:, :, 0] < 80)).mean() > .02
        viewer.update_geometry(second)
        pixels = viewer.capture_screen()
        assert ((pixels[:, :, 0] > 180) & (pixels[:, :, 1] < 80)).mean() > .02
    frames = Sequence(MemoryFrameProvider([Frame(0, 0, first), Frame(1, .1, second)]))
    path = render_gif(frames, tmp_path / "owned.gif", width=320, height=240,
                      elevation=90, distance=2.5, no_metrics=True)
    with Image.open(path) as image:
        assert image.n_frames == 2
        pixels = np.asarray(image.convert("RGB"))
        assert ((pixels[:, :, 1] > 180) & (pixels[:, :, 0] < 80)).mean() > .02


def test_live_viewer_keeps_one_application_after_repeated_gifs(tmp_path):
    if os.environ.get("OPEN4D_TEST_RENDER") != "1":
        pytest.skip("set OPEN4D_TEST_RENDER=1 with a desktop or Xvfb display")
    QtWidgets = pytest.importorskip("PyQt6.QtWidgets")
    sequence = mesh_sequence(side=8, frames=2)
    application = None
    for index in range(2):
        render_gif(sequence, tmp_path / f"repeat-{index}.gif", width=320, height=240)
        current = QtWidgets.QApplication.instance()
        assert current is not None, "rendering must retain the process-wide Qt application"
        if application is not None:
            assert current is application
        application = current
    with Visualizer(width=640, height=240, elevation=90, distance=2.5) as viewer:
        viewer.add_geometry(sequence[0])
        viewer.add_geometry(sequence[0], baseline=True)
        assert viewer._backend.application is application
        assert viewer.poll_events()
        assert np.any(viewer.capture_screen() < 240)


def test_live_viewer_initializes_comparison_contexts_before_geometry():
    if os.environ.get("OPEN4D_TEST_RENDER") != "1":
        pytest.skip("set OPEN4D_TEST_RENDER=1 with a desktop or Xvfb display")
    pytest.importorskip("PyQt6.QtWidgets")
    frame = mesh_sequence(side=8, frames=1)[0]
    with Visualizer(width=640, height=320, elevation=30, distance=2) as viewer:
        views = viewer._backend.views
        contexts = {key: view.context() for key, view in views.items()}
        assert all(view.isValid() for view in views.values()), "both panes must initialize before uploads"
        assert all(context is not None for context in contexts.values())
        assert contexts[False].shareGroup() is contexts[True].shareGroup()
        viewer.add_geometry(frame)
        for _ in range(3):
            viewer.add_geometry(frame, baseline=True)
            assert viewer.poll_events()
            assert np.any(viewer.capture_screen(baseline=True) < 240)
            viewer.remove_geometry(baseline=True)
            assert viewer.poll_events()
            assert views[True].context() is contexts[True], "revealing a pane must reuse its existing context"


def test_live_viewer_releases_previous_gif_views_without_cyclic_gc(tmp_path, monkeypatch):
    if os.environ.get("OPEN4D_TEST_RENDER") != "1":
        pytest.skip("set OPEN4D_TEST_RENDER=1 with a desktop or Xvfb display")
    from open4d.visualization import _qt

    views = []
    original = _qt.Scene

    class TrackedScene(original):
        def __init__(self, *args, **options):
            super().__init__(*args, **options)
            views.append(weakref.ref(self.view))

    monkeypatch.setattr(_qt, "Scene", TrackedScene)
    enabled = gc.isenabled()
    gc.disable()
    try:
        sequence = mesh_sequence(side=8, frames=2)
        for index in range(3):
            render_gif(sequence, tmp_path / f"released-{index}.gif", width=320, height=240)
            assert all(view() is None for view in views), "closed GPU views must not wait for later cyclic GC"
        with Visualizer(width=640, height=320) as viewer:
            viewer.add_geometry(sequence[0])
            viewer.add_geometry(sequence[0], baseline=True)
            assert viewer.poll_events()
            assert np.any(viewer.capture_screen() < 240)
    finally:
        if enabled:
            gc.enable()


def test_live_viewer_reports_platform_mismatch_without_aborting_qt(tmp_path):
    if os.environ.get("OPEN4D_TEST_RENDER") != "1":
        pytest.skip("set OPEN4D_TEST_RENDER=1 with a desktop or Xvfb display")
    # Simulate the same missing public context getter that a GLX PyOpenGL
    # platform produces for a real Qt EGL context. Use real widgets in a fresh
    # process: raising directly through initializeGL would abort that process.
    program = """
import sys
from OpenGL import platform
from open4d.demo import mesh_sequence
from open4d.visualization import Visualizer, render_gif, visualize
from open4d.visualization._qt import _application, _qt
platform.GetCurrentContext = lambda: 0
QtWidgets, QtCore, _ = _qt()
application = _application(QtWidgets)
sequence = mesh_sequence(side=8, frames=1)
actions = [lambda: render_gif(sequence, sys.argv[1]),
           lambda: visualize(sequence), lambda: Visualizer().create_window()]
for action in actions:
    QtCore.QTimer.singleShot(200, application.closeAllWindows)
    try:
        action()
    except RuntimeError as error:
        assert 'PYOPENGL_PLATFORM=egl' in str(error), str(error)
        assert 'before importing' in str(error), str(error)
    else:
        raise AssertionError('a mismatched context getter must fail before uploads')
assert not sequence.closed
print('GIF, player and live viewer reported actionable context errors')
"""
    result = subprocess.run([sys.executable, "-c", program, str(tmp_path / "mismatch.gif")],
                            text=True, capture_output=True, timeout=15)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "actionable context errors" in result.stdout
    assert not (tmp_path / "mismatch.gif").exists()


def _require_render():
    if os.environ.get("OPEN4D_TEST_RENDER") != "1":
        pytest.skip("set OPEN4D_TEST_RENDER=1 with a desktop or Xvfb display")
    pytest.importorskip("PyQt6.QtWidgets")


def _cloud(count, *, seed=0, z=0.0):
    from open4d import GaussianCloud

    rng = np.random.default_rng(seed)
    positions = np.c_[rng.uniform(-0.8, 0.8, (count, 2)), np.full(count, z)]
    return GaussianCloud(positions, np.full((count, 3), 0.05), np.tile([1., 0, 0, 0], (count, 1)),
                         np.full(count, 0.9), colors=np.tile([[0.1, 0.2, 0.9]], (count, 1)))


def _foreground(pixels):
    return np.any(pixels < 235, axis=2)


def test_splats_render_oriented_anisotropic_ellipses():
    _require_render()
    from open4d import GaussianCloud

    half = np.radians(30) / 2
    # Long along x, then turned 30 degrees about z: seen from above, the
    # ellipse's major axis must sit 30 degrees counterclockwise of screen x.
    cloud = GaussianCloud([[0., 0, 0]], [[0.4, 0.06, 0.06]], [[np.cos(half), 0, 0, np.sin(half)]],
                          [0.95], colors=[[0.9, 0.1, 0.1]])
    with Visualizer(width=400, height=400, elevation=90, azimuth=-90, distance=3.0) as viewer:
        viewer.add_geometry(cloud)
        assert viewer.poll_events()
        pixels = viewer.capture_screen()
        assert "Gaussian splats" in viewer._backend.labels[False].text()
    red = (pixels[:, :, 0] > 150) & (pixels[:, :, 1] < 150)
    rows, columns = np.nonzero(red)
    assert len(rows) > 100, "the splat is missing"
    covariance = np.cov([columns - columns.mean(), rows.mean() - rows])
    minor, major = np.linalg.eigvalsh(covariance)
    # A uniform ellipse's variance ratio is its squared axis ratio, (0.4/0.06)^2.
    assert major / minor > 20
    angle = 0.5 * np.degrees(np.arctan2(2 * covariance[0, 1], covariance[0, 0] - covariance[1, 1]))
    assert abs(angle - 30) < 3


def test_splats_blend_back_to_front_for_every_camera():
    _require_render()
    from open4d import GaussianCloud

    # Red sits above blue. Splats do not write depth, so only the per-camera
    # sort decides which one covers the other: from above red must win, from
    # below blue, with the same upload order both times.
    cloud = GaussianCloud([[0., 0, 0.3], [0., 0, -0.3]], np.full((2, 3), 0.25),
                          [[1., 0, 0, 0]] * 2, [0.99, 0.99],
                          colors=[[0.95, 0.05, 0.05], [0.05, 0.05, 0.95]])
    with Visualizer(width=320, height=320, elevation=90, azimuth=-90, distance=3.0) as viewer:
        viewer.add_geometry(cloud)
        assert viewer.poll_events()
        above = viewer.capture_screen()
        view = viewer._backend.views[False]
        view.setCameraPosition(elevation=-90)
        assert viewer.poll_events()
        below = viewer.capture_screen()
    for pixels, front, back in ((above, 0, 2), (below, 2, 0)):
        centre = pixels[pixels.shape[0] // 2, pixels.shape[1] // 2].astype(int)
        assert centre[front] > 200 and centre[back] < 60, centre


def test_splat_sh_color_is_reevaluated_when_the_camera_moves():
    _require_render()
    from open4d import GaussianSplats

    sh = np.zeros((1, 4, 3), np.float32)
    sh[0, 2, 0] = 1.5                       # red rises with the view direction's z
    splats = GaussianSplats(positions=[[0., 0, 0]], scales=[[0.25, 0.25, 0.25]],
                            rotations=[[1., 0, 0, 0]], opacities=[0.99], spherical_harmonics=sh)
    with Visualizer(width=320, height=320, elevation=90, azimuth=-90, distance=3.0) as viewer:
        viewer.add_geometry(splats)
        assert viewer.poll_events()
        above = viewer.capture_screen()
        viewer._backend.views[False].setCameraPosition(elevation=-90)
        assert viewer.poll_events()
        below = viewer.capture_screen()
    middle = (above.shape[0] // 2, above.shape[1] // 2)
    assert above[middle][0] < 60 and below[middle][0] > 200


def test_splat_comparison_panes_stay_in_sync_and_follow_counts():
    _require_render()
    with Visualizer(width=800, height=400, elevation=30, azimuth=20, distance=2.5) as viewer:
        backend = viewer._backend
        viewer.add_geometry(_cloud(30))
        viewer.add_geometry(_cloud(30), baseline=True)
        assert viewer.poll_events()
        current, baseline = viewer.capture_screen(), viewer.capture_screen(baseline=True)
        assert _foreground(current).mean() > 0.01
        assert np.abs(current.astype(int) - baseline).max() <= 2
        backend.views[False].setCameraPosition(azimuth=110, elevation=60)
        backend._sync_camera(backend.views[False])
        assert viewer.poll_events()
        moved, moved_baseline = viewer.capture_screen(), viewer.capture_screen(baseline=True)
        assert np.abs(moved.astype(int) - moved_baseline).max() <= 2
        assert np.abs(moved.astype(int) - current).max() > 50
        areas = []
        for count in (60, 0, 4, 60):
            viewer.update_geometry(_cloud(count, seed=count))
            assert viewer.poll_events()
            areas.append(_foreground(viewer.capture_screen()).mean())
        assert areas[1] == 0, "an empty frame must draw nothing"
        assert areas[0] > areas[2] > 0 and areas[3] == pytest.approx(areas[0])
        viewer.remove_geometry(baseline=True)
        assert viewer.poll_events()


def test_splat_gif_playback_handles_empty_and_changing_frames(tmp_path, monkeypatch):
    _require_render()
    from PIL import Image
    from open4d.visualization import _qt

    views = []
    original = _qt.Scene

    class TrackedScene(original):
        def __init__(self, *args, **options):
            super().__init__(*args, **options)
            views.append(weakref.ref(self.view))

    monkeypatch.setattr(_qt, "Scene", TrackedScene)
    frames = [Frame(index, index / 10, _cloud(count, seed=index))
              for index, count in enumerate((0, 5, 40))]
    sequence = Sequence(MemoryFrameProvider(frames))
    enabled = gc.isenabled()
    gc.disable()
    try:
        output = render_gif(sequence, tmp_path / "splats.gif", width=320, height=240,
                            elevation=90, azimuth=-90, distance=2.5, no_metrics=True)
        assert all(view() is None for view in views), "splat buffers must not wait for cyclic GC"
    finally:
        if enabled:
            gc.enable()
    areas = []
    with Image.open(output) as image:
        for index in range(image.n_frames):
            image.seek(index)
            areas.append(_foreground(np.asarray(image.convert("RGB"))).mean())
    assert areas[0] == 0 and 0 < areas[1] < areas[2]


def test_splats_fall_back_to_points_without_instancing(tmp_path, monkeypatch):
    _require_render()
    from PIL import Image
    from open4d.visualization import _splats

    def unsupported(context):
        raise _splats.SplatsUnsupported("instancing disabled for this test")

    monkeypatch.setattr(_splats, "capabilities", unsupported)
    with Visualizer(width=320, height=320, elevation=90, azimuth=-90, distance=3.0,
                    point_size=12) as viewer:
        viewer.add_geometry(_cloud(3))
        assert viewer.poll_events()
        pixels = viewer.capture_screen()
        assert "point fallback: instancing disabled" in viewer._backend.labels[False].text()
        assert _foreground(pixels).mean() > 0
    sequence = Sequence(MemoryFrameProvider([Frame(0, 0, _cloud(3))]))
    output = render_gif(sequence, tmp_path / "fallback.gif", width=320, height=240,
                        elevation=90, azimuth=-90, distance=2.5, point_size=12)
    with Image.open(output) as image:
        # The first captured frame already shows the fallback, and says so.
        pixels = np.asarray(image.convert("RGB"))
        assert (np.all(pixels[:, :, :2] < 120, axis=2) & (pixels[:, :, 2] > 150)).any()
