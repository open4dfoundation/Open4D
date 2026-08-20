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
from open4d.visualization._frames import shade

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
