from __future__ import annotations

from pathlib import Path

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


def test_documentation_does_not_advertise_usd_frame_directories():
    readme = Path(frame_sources.__file__).with_name("README.md").read_text(
        encoding="utf-8"
    )

    assert "Folder of `.usd` frames" not in readme


def test_example_helpers_advertise_raw_vdmc_as_a_sequence_source(tmp_path):
    bitstream = tmp_path / "capture.vmesh"
    bitstream.write_bytes(b"raw bitstream")

    assert frame_sources.source_kind(bitstream) == "sequence-file"
    assert ".vmesh" in frame_sources.supported_formats()


def test_raw_vmesh_fps_is_forwarded_to_the_public_loader(tmp_path, monkeypatch):
    bitstream = tmp_path / "capture.vmesh"
    bitstream.write_bytes(b"raw bitstream")
    received = {}
    monkeypatch.setattr(
        frame_sources,
        "_open_sequence",
        lambda path, **options: received.update(path=path, **options),
    )

    frame_sources.open_sequence(bitstream, fps=24)

    assert received == {"path": bitstream, "fps": 24}
