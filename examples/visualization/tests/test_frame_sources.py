from __future__ import annotations

import pytest

import frame_sources
import open4d
from open4d import Frame, MemoryFrameProvider, Sequence, TriangleMesh

pytestmark = pytest.mark.cpu


def test_folder_loader_rejects_an_explicit_zero_fps(tmp_path):
    (tmp_path / "frame.obj").write_text(
        "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", encoding="utf-8"
    )

    with pytest.raises(ValueError, match="fps"):
        frame_sources.open_sequence(tmp_path, fps=0)


def test_example_helpers_treat_codec_artifacts_as_whole_sequences(tmp_path):
    mesh = TriangleMesh(
        [[0.0, 0, 0], [1, 0, 0], [0, 1, 0]], [[0, 1, 2]]
    )
    artifact = open4d.save(
        Sequence(MemoryFrameProvider([Frame(0, 0.0, mesh)])),
        tmp_path / "capture.o4d",
    )

    assert frame_sources.source_kind(artifact) == "sequence-file"
    assert "1 frames" in frame_sources.describe_source(artifact)
    # This is a playback override in the example CLIs, not an import-time
    # timestamp override for a self-describing sequence artifact.
    with frame_sources.open_sequence(artifact, fps=12) as sequence:
        assert len(sequence) == 1
