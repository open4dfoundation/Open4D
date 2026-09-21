from __future__ import annotations

import sys
from types import SimpleNamespace

import numpy as np
import pytest

from open4d import Frame, MemoryFrameProvider, Sequence, TriangleMesh
from open4d.visualization import (
    VisualizationDependencyError,
    render_gif,
    visualize,
)
from open4d.visualization._frames import LazyRenderSequence, bounds, shade, to_render_frame

pytestmark = pytest.mark.cpu


def test_bounds_skip_empty_frames_and_reject_a_wholly_empty_sequence():
    empty = Frame(0, 0, TriangleMesh(np.empty((0, 3), dtype=np.float32),
                                    np.empty((0, 3), dtype=np.uint32)))
    blank = to_render_frame(empty, [0, 1, 2])
    visible = to_render_frame(sequence()[0], [0, 1, 2])

    lower, upper = bounds([blank, visible])

    np.testing.assert_array_equal(lower, [0, 0, 0])
    np.testing.assert_array_equal(upper, [1, 2, 0])
    with pytest.raises(ValueError, match="no vertices"):
        bounds([blank])


@pytest.mark.player
@pytest.mark.parametrize("width", [4, 5, 6, 7, 8])
def test_framebuffer_rgb_padding_preserves_pixels(width):
    QtGui = pytest.importorskip("PyQt6.QtGui")
    Image = pytest.importorskip("PIL.Image")
    from open4d.visualization._qt import Scene

    expected = np.arange(width * 4 * 3, dtype=np.uint8).reshape(4, width, 3)
    framebuffer = QtGui.QImage(width, 4, QtGui.QImage.Format.Format_RGB888)
    for row in range(4):
        for column in range(width):
            framebuffer.setPixelColor(column, row, QtGui.QColor(*map(int, expected[row, column])))
    scene = Scene.__new__(Scene)
    scene.view = SimpleNamespace(update=lambda: None, grabFramebuffer=lambda: framebuffer)
    scene.application = SimpleNamespace(processEvents=lambda: None)
    scene.args = SimpleNamespace(width=width, height=4, no_metrics=True)

    np.testing.assert_array_equal(np.asarray(scene.grab(Image)), expected)


def sequence() -> Sequence:
    mesh = TriangleMesh(
        positions=[[0.0, 0, 0], [1.0, 0, 0], [0, 2.0, 0]],
        triangles=[[0, 1, 2]],
    )
    return Sequence(MemoryFrameProvider([Frame(7, 0.0, mesh)]))


def test_visualize_prepares_real_frames_without_importing_qt(monkeypatch):
    captured = {}
    qt_before = sys.modules.get("PyQt6")
    from open4d.visualization import _qt

    monkeypatch.setattr(_qt, "check_available", lambda **options: None)
    monkeypatch.setattr(
        _qt, "play", lambda frames, options: captured.update(frames=frames, options=options)
    )
    visualize(sequence(), up="y", fps=24, width=640)

    assert sys.modules.get("PyQt6") is qt_before
    assert captured["options"].fps == 24
    assert captured["options"].width == 640
    np.testing.assert_array_equal(
        captured["frames"][0].positions[2], [0, 0, 2]
    )


def test_lazy_render_sequence_strides_and_evicts_frames():
    calls = []

    class Provider:
        frame_count = 6

        def get_frame(self, index):
            calls.append(index)
            mesh = TriangleMesh(
                np.array(
                    [[index, 0, 0], [index + 1, 0, 0], [index, 1, 0]],
                    dtype=np.float32,
                ),
                [[0, 1, 2]],
            )
            return Frame(index, index / 30, mesh)

    frames = LazyRenderSequence(Sequence(Provider()), stride=2, order=[0, 1, 2], cache_size=2)

    assert len(frames) == 3
    assert calls == []
    assert frames[0].frame_index == 0
    assert frames[0].frame_index == 0
    assert calls == [0]
    frames.prefetch(1)
    assert calls == [0, 2]
    assert frames[2].frame_index == 4
    assert len(frames.cached_indices) == 2
    assert frames[0].frame_index == 0
    assert calls == [0, 2, 4, 0]


def test_render_gif_delegates_to_shared_renderer(tmp_path, monkeypatch):
    captured = {}
    from open4d.visualization import _qt

    monkeypatch.setattr(_qt, "check_available", lambda **options: None)
    monkeypatch.setattr(
        _qt,
        "record",
        lambda frames, options, output: captured.update(output=output, count=len(frames)),
    )
    output = render_gif(sequence(), tmp_path / "preview.gif")

    assert output == tmp_path / "preview.gif"
    assert captured == {"output": output, "count": 1}


def test_visualize_path_owns_and_closes_loaded_sequence(monkeypatch):
    value = sequence()
    close_calls = []
    real_close = value.close
    from open4d import _api
    from open4d.visualization import _qt

    monkeypatch.setattr(
        _api, "load", lambda source: value
    )
    monkeypatch.setattr(_qt, "check_available", lambda **options: None)
    monkeypatch.setattr(_qt, "play", lambda frames, options: frames[0])
    monkeypatch.setattr(
        value, "close", lambda: (close_calls.append(True), real_close())[1]
    )

    visualize("capture.o4d")

    assert close_calls == [True]
    assert value.closed is True


def test_visualize_does_not_close_a_caller_owned_sequence(monkeypatch):
    value = sequence()
    from open4d.visualization import _qt

    monkeypatch.setattr(_qt, "check_available", lambda **options: None)
    monkeypatch.setattr(_qt, "play", lambda frames, options: frames[0])

    visualize(value)

    assert value.closed is False


def test_visualize_path_closes_after_renderer_failure(monkeypatch):
    value = sequence()
    from open4d import _api
    from open4d.visualization import _qt

    monkeypatch.setattr(_api, "load", lambda source: value)
    monkeypatch.setattr(_qt, "check_available", lambda **options: None)
    monkeypatch.setattr(
        _qt, "play", lambda frames, options: (_ for _ in ()).throw(RuntimeError("boom"))
    )

    with pytest.raises(RuntimeError, match="boom"):
        visualize("capture.o4d")

    assert value.closed is True


def test_visualize_uses_sequence_up_axis_by_default(monkeypatch):
    mesh = TriangleMesh(
        [[0.0, 0, 0], [1, 0, 0], [0, 2.0, 0]], [[0, 1, 2]]
    )
    value = Sequence(MemoryFrameProvider(
        [Frame(0, 0, mesh)], metadata={"up_axis": "y"}
    ))
    captured = {}
    from open4d.visualization import _qt

    monkeypatch.setattr(_qt, "check_available", lambda **options: None)
    monkeypatch.setattr(
        _qt, "play", lambda frames, options: captured.update(frame=frames[0])
    )

    visualize(value)

    np.testing.assert_array_equal(captured["frame"].positions[2], [0, 0, 2])


def test_visualization_arguments_and_shading_are_validated(monkeypatch):
    from open4d.visualization import _qt

    monkeypatch.setattr(_qt, "check_available", lambda **options: None)
    with pytest.raises(ValueError, match="stride"):
        visualize(sequence(), stride=0)
    with pytest.raises(ValueError, match="up"):
        visualize(sequence(), up="north")
    with pytest.raises(ValueError, match="finite"):
        visualize(sequence(), fps=float("nan"))
    with pytest.raises(ValueError, match="title"):
        visualize(sequence(), title="")
    with pytest.raises(ValueError, match=".gif"):
        render_gif(sequence(), "preview.png")
    colors = shade(
        sequence()[0].geometry.positions,
        sequence()[0].geometry.triangles,
    )
    assert colors.shape == (3, 4)
    assert np.isfinite(colors).all()


def test_missing_backend_fails_before_decoding(monkeypatch):
    value = sequence()
    calls = []
    real_get_frame = value._provider.get_frame
    monkeypatch.setattr(
        value._provider,
        "get_frame",
        lambda index: (calls.append(index), real_get_frame(index))[1],
    )
    from open4d.visualization import _qt

    def unavailable(**options):
        raise VisualizationDependencyError("player missing")

    monkeypatch.setattr(_qt, "check_available", unavailable)

    with pytest.raises(VisualizationDependencyError, match="player missing"):
        visualize(value)
    assert calls == []


def test_missing_backend_fails_before_loading_a_path(monkeypatch):
    from open4d import _api
    from open4d.visualization import _qt

    calls = []
    monkeypatch.setattr(_api, "load", lambda source: calls.append(source))
    monkeypatch.setattr(
        _qt,
        "check_available",
        lambda **options: (_ for _ in ()).throw(
            VisualizationDependencyError("player missing")
        ),
    )

    with pytest.raises(VisualizationDependencyError, match="player missing"):
        visualize("capture.usdc")
    assert calls == []


@pytest.mark.parametrize("render", [False, True])
def test_gaussian_paths_are_rejected_before_native_decode(tmp_path, monkeypatch, render):
    from open4d.visualization import _api, _qt
    import open4d._api as public

    monkeypatch.setattr(_qt, "check_available", lambda **options: None)
    monkeypatch.setattr(public, "load", lambda *args: pytest.fail("must not decode Gaussian data"))
    with pytest.raises(TypeError, match="native renderer"):
        if render:
            _api.render_gif(tmp_path / "capture.vega", tmp_path / "capture.gif")
        else:
            _api.visualize(tmp_path / "capture.vega")


@pytest.mark.parametrize("scale", [1e-10, 1e10, 1e20])
def test_shading_is_independent_of_mesh_units(scale):
    mesh = sequence()[0].geometry
    expected = shade(mesh.positions, mesh.triangles)
    with np.errstate(over="ignore", invalid="ignore"):
        actual = shade(mesh.positions * np.float32(scale), mesh.triangles)
    np.testing.assert_allclose(actual, expected, rtol=1e-6)


def test_stride_keeps_source_playback_speed(monkeypatch):
    from open4d.demo import mesh_sequence
    from open4d.visualization import _qt

    options = []
    monkeypatch.setattr(_qt, "check_available", lambda **kw: None)
    monkeypatch.setattr(_qt, "play", lambda frames, args: options.append(args))
    with mesh_sequence(side=2, frames=12, fps=30) as source:
        visualize(source, stride=3)
        visualize(source, stride=3, fps=24)
    assert options[0].fps == 10
    assert options[1].fps == 24


@pytest.mark.parametrize("values", [
    {"width": 100.5}, {"height": True}, {"x": True, "y": 0},
    {"point_size": float("nan")}, {"distance": 0},
    {"distance": float("inf")}, {"elevation": float("nan")},
    {"azimuth": float("inf")}, {"color": (1, 0)},
    {"color": (1, 0, float("nan"))}, {"background": (256, 0, 0)},
])
def test_invalid_viewer_options_fail_before_rendering(monkeypatch, values):
    from open4d.visualization import _qt

    monkeypatch.setattr(_qt, "check_available", lambda **kw: None)
    monkeypatch.setattr(_qt, "play", lambda *args: pytest.fail("invalid options reached Qt"))
    with pytest.raises(ValueError):
        visualize(sequence(), **values)


@pytest.mark.player
@pytest.mark.parametrize("prefetch_error", [True, False])
def test_lazy_decode_failure_returns_to_viewer_caller(monkeypatch, prefetch_error):
    import os
    QtWidgets = pytest.importorskip("PyQt6.QtWidgets")
    QtCore = pytest.importorskip("PyQt6.QtCore")
    from open4d.visualization import _qt
    from open4d.visualization._api import ViewerOptions

    if (QtWidgets.QApplication.instance() is None
            and os.environ.get("OPEN4D_TEST_RENDER") != "1"
            and os.environ.get("OPEN4D_TEST_GUI") != "1"):
        monkeypatch.setenv("QT_QPA_PLATFORM", "offscreen")
    application = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
    callback_errors = []

    class Frames:
        def __len__(self):
            return 2

        def __getitem__(self, index):
            if index == 1:
                raise RuntimeError("cannot decode frame 1")
            return to_render_frame(sequence()[0], [0, 1, 2])

        def prefetch(self, index):
            if prefetch_error:
                self[index]

    class Scene:
        def __init__(self, frames, args):
            self.application = application
            self.view = QtWidgets.QWidget()
            self.index = 0

        def show_frame(self, index):
            self.index = index % 2
            return frames[self.index]

    def catch_callback_error(*error):
        callback_errors.append(error)
        application.closeAllWindows()

    frames = Frames()
    monkeypatch.setattr(_qt, "Scene", Scene)
    monkeypatch.setattr(_qt, "_qt", lambda: (QtWidgets, QtCore, None))
    monkeypatch.setattr(sys, "excepthook", catch_callback_error)
    deadline = QtCore.QTimer()
    deadline.setSingleShot(True)
    deadline.timeout.connect(application.closeAllWindows)
    deadline.start(1000)
    try:
        with pytest.raises(RuntimeError, match="cannot decode frame 1"):
            _qt.play(frames, ViewerOptions(fps=30))
        assert callback_errors == []
    finally:
        deadline.stop()
        application.closeAllWindows()


@pytest.fixture
def gif_scene(monkeypatch):
    from open4d.visualization import _qt

    closed = []

    class Scene:
        def __init__(self, frames, args):
            self.index = 0
            self.view = SimpleNamespace(resize=lambda *a: None, close=lambda: closed.append(True))
            self.application = SimpleNamespace(processEvents=lambda: None)

        def show_frame(self, index):
            self.index = index

        def grab(self, image_module):
            return image_module.new("RGB", (4, 4), (self.index * 5, 0, 0))

    monkeypatch.setattr(_qt, "Scene", Scene)
    return closed


def test_gif_keeps_fractional_frame_duration(tmp_path, gif_scene):
    Image = pytest.importorskip("PIL.Image")
    from open4d.visualization import _qt
    from open4d.visualization._api import ViewerOptions

    output = tmp_path / "test.gif"
    _qt.record(range(30), ViewerOptions(fps=30), output)
    with Image.open(output) as result:
        duration = 0
        for index in range(result.n_frames):
            result.seek(index)
            duration += result.info["duration"]
    assert duration == 1000
    assert gif_scene == [True]


def test_failed_gif_write_keeps_existing_output(tmp_path, monkeypatch, gif_scene):
    Image = pytest.importorskip("PIL.Image")
    from open4d.visualization import _qt
    from open4d.visualization._api import ViewerOptions
    from pathlib import Path

    output = tmp_path / "test.gif"
    output.write_bytes(b"original")

    def fail_save(self, path, **options):
        Path(path).write_bytes(b"partial")
        raise OSError("disk full")

    monkeypatch.setattr(Image.Image, "save", fail_save)
    with pytest.raises(OSError, match="disk full"):
        _qt.record(range(3), ViewerOptions(fps=30), output)
    assert output.read_bytes() == b"original"
    assert list(tmp_path.iterdir()) == [output]
    assert gif_scene == [True]
