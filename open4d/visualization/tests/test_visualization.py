from __future__ import annotations

import sys

import numpy as np
import pytest

from open4d import Frame, MemoryFrameProvider, Sequence, TriangleMesh
from open4d.visualization import (
    VisualizationDependencyError,
    render_gif,
    visualize,
)
from open4d.visualization._frames import LazyRenderSequence, shade

pytestmark = pytest.mark.cpu


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
