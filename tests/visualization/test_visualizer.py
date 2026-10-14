from __future__ import annotations

import threading

import numpy as np
import pytest

from open4d.core import Frame, GaussianCloud, MemoryFrameProvider, PointCloud, Sequence, TriangleMesh
from open4d.visualization import Visualizer, visualize
from open4d.visualization._frames import to_render_frame, vertex_colors

pytestmark = pytest.mark.cpu


def mesh(index=0, *, uv=True):
    geometry = TriangleMesh([[0., 0, 0], [1., 0, 0], [0., 1, 0]], [[0, 1, 2]],
                            texture_coordinates=[[0., 0], [1., 0], [0., 1]] if uv else None)
    return Frame(index, index / 30, geometry)


@pytest.fixture
def backend(monkeypatch):
    from open4d.visualization import _live_qt

    class Window:
        def __init__(self, options):
            self.is_open = True
            self.options = options
            self.frames = {}
            self.displayed = []
            self.polls = 0
            self.renders = 0
            self.close_count = 0

        def set_geometry(self, key, frame, texture, *, reset_camera):
            self.frames[key] = frame, texture
            self.displayed.append((key, frame.frame_index, reset_camera))

        def remove_geometry(self, key):
            del self.frames[key]

        def set_options(self, options):
            self.options = options

        def poll_events(self):
            self.polls += 1
            return self.is_open

        def render(self):
            self.renders += 1

        def capture(self, *, baseline):
            return np.zeros((4, 8, 3), dtype=np.uint8)

        def close(self):
            self.close_count += 1
            self.is_open = False

    monkeypatch.setattr(_live_qt, "LiveWindow", Window)
    return Window


def test_updates_preserve_texture_and_baseline_and_release_window(backend):
    texture = np.full((2, 3, 3), 255, np.uint8)
    viewer = Visualizer(up="y", width=640)
    assert not viewer.poll_events()
    with viewer:
        window = viewer._backend
        viewer.add_geometry(mesh(), texture=texture)
        viewer.add_geometry(mesh(), baseline=True)
        viewer.update_geometry(mesh(9))
        np.testing.assert_array_equal(window.frames[False, "geometry"][0].positions[2], [0, 0, 1])
        assert window.frames[False, "geometry"][1] is texture
        assert window.frames[True, "geometry"][0].frame_index == 0
        assert window.displayed[-1][-1] is False  # No camera jump on a streamed update.
        viewer.set_options(point_size=7, wireframe=True)
        assert window.options.point_size == 7
        assert viewer.capture_screen().shape == (4, 8, 3)
        assert viewer.remove_geometry(baseline=True)
        assert not viewer.remove_geometry(baseline=True)
    viewer.close()
    assert window.close_count == 1
    assert not viewer.is_open


def test_texture_validation_prevents_partial_geometry_updates(backend):
    with Visualizer() as viewer:
        viewer.add_geometry(mesh())
        with pytest.raises(ValueError, match="texture_coordinates"):
            viewer.update_geometry(mesh(1, uv=False), texture=np.zeros((2, 2, 3), np.uint8))
        assert viewer._backend.frames[False, "geometry"][0].frame_index == 0
        with pytest.raises(ValueError, match="finite floats"):
            viewer.update_geometry(mesh(2), texture=np.ones((2, 2, 3)) * np.nan)
        assert len(viewer._backend.displayed) == 1


def test_geometry_lifecycle_errors_are_actionable(backend):
    viewer = Visualizer()
    with pytest.raises(RuntimeError, match="create_window"):
        viewer.add_geometry(mesh())
    with viewer:
        with pytest.raises(RuntimeError, match="already open"):
            viewer.create_window()
        with pytest.raises(KeyError, match="add_geometry"):
            viewer.update_geometry(mesh())
        viewer.add_geometry(mesh())
        with pytest.raises(ValueError, match="already exists"):
            viewer.add_geometry(mesh())
        with pytest.raises(TypeError, match="geometry must be"):
            viewer.add_geometry(object(), name="invalid")


def test_stream_displays_every_frame_without_accumulating_sources(backend):
    frames = Sequence(MemoryFrameProvider([mesh(index) for index in range(40)]))
    with Visualizer() as viewer:
        assert viewer.stream(frames, baseline=mesh(8)) == 40
        window = viewer._backend
        assert [index for key, index, _ in window.displayed if not key[0]] == list(range(40))
        assert len(window.frames) == 2
        assert window.renders == 40
        assert window.polls >= 40
    assert not frames.closed


def test_stream_waits_for_source_without_freezing_gui_and_reraises_errors(backend):
    ready = threading.Event()

    def source():
        assert ready.wait(timeout=1)
        yield mesh(12)
        raise ValueError("failed network frame")

    with Visualizer() as viewer:
        window = viewer._backend
        poll = window.poll_events

        def keep_gui_alive():
            if window.polls >= 3:
                ready.set()
            return poll()

        window.poll_events = keep_gui_alive
        with pytest.raises(ValueError, match="failed network frame"):
            viewer.stream(source())
        assert window.polls >= 3
        assert window.frames[False, "geometry"][0].frame_index == 12


def test_stream_returns_when_window_closes_while_source_is_blocked(backend):
    release = threading.Event()
    ended = threading.Event()

    def source():
        try:
            release.wait(timeout=2)
            yield mesh()
        finally:
            ended.set()

    with Visualizer() as viewer:
        window = viewer._backend
        poll = window.poll_events

        def close_window():
            if window.polls >= 2:
                window.is_open = False
            return poll()

        window.poll_events = close_window
        assert viewer.stream(source()) == 0
        release.set()
        assert ended.wait(timeout=1)


def test_frame_conversion_supports_points_and_explicit_gaussian_preview():
    points = to_render_frame(Frame(0, 0, PointCloud([[0., 1, 2]], colors=[[1., 0, 0]])), [0, 1, 2])
    assert not points.is_mesh
    assert points.representation == "points"
    cloud = GaussianCloud([[0., 0, 0]], [[1., 1, 1]], [[1., 0, 0, 0]], [0.25], colors=[[0., 1, 0]])
    frame = to_render_frame(Frame(0, 0, cloud), [0, 1, 2])
    assert frame.representation == "gaussians"
    np.testing.assert_array_equal(vertex_colors(frame), [[0, 1, 0, 0.25]])


def test_one_line_comparison_keeps_owned_sources_open(backend, monkeypatch):
    from open4d.visualization import _qt

    monkeypatch.setattr(_qt, "check_available", lambda **options: None)
    sequence = Sequence(MemoryFrameProvider([mesh()]))
    reference = Sequence(MemoryFrameProvider([mesh()]))
    calls = []

    def compare(viewer, current, **options):
        calls.append((current, options))

    monkeypatch.setattr(Visualizer, "play", compare)
    visualize(sequence, baseline=reference)
    assert calls[0][0] is sequence
    assert calls[0][1]["baseline"] is reference
    assert not sequence.closed and not reference.closed


def test_sequence_comparison_rejects_mismatched_lengths(backend):
    with Visualizer() as viewer:
        with pytest.raises(ValueError, match="equal frame counts"):
            viewer.play(Sequence(MemoryFrameProvider([mesh()])),
                        baseline=Sequence(MemoryFrameProvider([mesh(), mesh(1)])))
        assert not viewer._backend.displayed


def test_sequence_comparison_rejects_mismatched_timestamps_before_rendering(backend):
    with Visualizer() as viewer:
        with pytest.raises(ValueError, match="timestamps differ"):
            viewer.play(Sequence(MemoryFrameProvider([mesh()])),
                        baseline=Sequence(MemoryFrameProvider([mesh(2)])))
        assert not viewer._backend.displayed


def test_sequence_comparison_uses_independent_textures(backend):
    current = np.full((2, 2, 3), [255, 0, 0], np.uint8)
    baseline = np.full((2, 2, 3), [0, 255, 0], np.uint8)
    sequence = Sequence(MemoryFrameProvider([mesh()]))
    with Visualizer() as viewer:
        window = viewer._backend
        original = window.poll_events

        def close_after_pair():
            if len(window.displayed) == 2:
                window.is_open = False
            return original()

        window.poll_events = close_after_pair
        viewer.play(sequence, baseline=sequence, texture=current, baseline_texture=baseline)
        np.testing.assert_array_equal(window.frames[False, "geometry"][1], current)
        np.testing.assert_array_equal(window.frames[True, "geometry"][1], baseline)


def test_gui_operations_from_workers_fail_before_loading_qt(backend):
    errors = []

    def worker():
        try:
            Visualizer().create_window()
        except RuntimeError as error:
            errors.append(str(error))

    thread = threading.Thread(target=worker)
    thread.start()
    thread.join()
    assert errors == ["Qt visualization must run on the main thread"]
