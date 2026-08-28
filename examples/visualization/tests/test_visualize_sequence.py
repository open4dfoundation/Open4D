from __future__ import annotations

import sys

import numpy as np
import pytest

import visualize_sequence as cli
from open4d import Frame, Sequence, TriangleMesh
from open4d.visualization._frames import LazyRenderSequence

pytestmark = pytest.mark.cpu


def test_malformed_codec_source_is_reported_without_a_traceback(
    tmp_path, monkeypatch
):
    path = tmp_path / "broken.o4d"
    path.write_bytes(b"not an Open4D artifact")
    monkeypatch.setattr(sys, "argv", ["visualize_sequence.py", str(path), "--info"])

    with pytest.raises(SystemExit) as caught:
        cli.main()

    message = str(caught.value)
    assert "failed to read the sequence" in message
    assert "Traceback" not in message


def test_malformed_usd_source_is_reported_without_a_traceback(
    tmp_path, monkeypatch
):
    pytest.importorskip("pxr")
    path = tmp_path / "broken.usda"
    path.write_text("not valid USD", encoding="utf-8")
    monkeypatch.setattr(sys, "argv", ["visualize_sequence.py", str(path), "--info"])

    with pytest.raises(SystemExit) as caught:
        cli.main()

    message = str(caught.value)
    assert "failed to read the sequence" in message
    assert "Traceback" not in message


def test_single_sequence_cli_reaches_playback_without_eager_decoding(
    tmp_path, monkeypatch
):
    calls = []

    class Provider:
        frame_count = 20
        timestamps = tuple(index / 30 for index in range(frame_count))
        metadata = {"fps": 30.0}

        def get_frame(self, index):
            calls.append(index)
            positions = np.array(
                [[index, 0, 0], [index + 1, 0, 0], [index, 1, 0]],
                dtype=np.float32,
            )
            return Frame(index, index / 30, TriangleMesh(positions, [[0, 1, 2]]))

    sequence = Sequence(Provider())
    path = tmp_path / "capture.o4d"
    path.touch()
    captured = {}
    monkeypatch.setattr(cli, "open_sequence", lambda *args, **kwargs: sequence)
    monkeypatch.setattr(cli, "describe_source", lambda path: "test sequence")
    monkeypatch.setattr(sys, "argv", ["visualize_sequence.py", str(path)])
    from open4d.visualization import _qt

    def play(frames, args):
        captured["frames"] = frames
        captured["closed_during_playback"] = sequence.closed
        captured["calls_at_playback"] = list(calls)

    monkeypatch.setattr(_qt, "play", play)

    cli.main()

    assert isinstance(captured["frames"], LazyRenderSequence)
    assert captured["closed_during_playback"] is False
    assert captured["calls_at_playback"] == [0]
    assert calls == [0]
    assert sequence.closed is True
