from __future__ import annotations

import os
from pathlib import Path
import warnings

import pytest

from open4d import Frame, MemoryFrameProvider, Sequence, TriangleMesh
from open4d.codec import decode_sequence, encode_sequence
from open4d.io import open_sequence
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


def test_real_rafa_codec_round_trip_renders_a_gif(tmp_path):
    if os.environ.get("OPEN4D_TEST_GUI") != "1":
        pytest.skip("set OPEN4D_TEST_GUI=1 in a desktop session")
    pytest.importorskip("PyQt6")
    image_module = pytest.importorskip("PIL.Image")
    root = Path(__file__).resolve().parents[3]
    dataset = root / "4d_files/Rafa_Approves_hd_4k"
    if not dataset.is_dir():
        pytest.skip("Rafa_Approves_hd_4k is not available")

    source = open_sequence(dataset, fps=30)[:2]
    artifact = encode_sequence(source, tmp_path / "rafa.o4d", codec="deflate")
    decoded = decode_sequence(artifact)
    output = render_gif(
        decoded, tmp_path / "rafa.gif", up="y", width=320, height=320
    )

    with image_module.open(output) as image:
        assert image.size == (320, 320)
        assert image.n_frames == 2
    decoded.close()
