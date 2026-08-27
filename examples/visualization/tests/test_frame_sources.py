from __future__ import annotations

import pytest

import frame_sources

pytestmark = pytest.mark.cpu


def test_folder_loader_rejects_an_explicit_zero_fps(tmp_path):
    (tmp_path / "frame.obj").write_text(
        "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", encoding="utf-8"
    )

    with pytest.raises(ValueError, match="fps"):
        frame_sources.open_sequence(tmp_path, fps=0)
