from __future__ import annotations

import json
import shlex

import pytest

from open4d import load, save
from open4d._cli import main
from open4d.demo import mesh_sequence, write_demo


pytestmark = pytest.mark.cpu


def test_demo_and_inspect_existing_ply_workflow(tmp_path, capsys):
    path = tmp_path / "sample with spaces"
    assert main(["demo", str(path), "--side", "4", "--frames", "3", "--fps", "15"]) == 0
    assert "Created 3 PLY frames at 15 fps" in capsys.readouterr().out
    assert main(["inspect", str(path), "--json"]) == 0
    info = json.loads(capsys.readouterr().out)
    assert info["frame_count"] == 3
    assert info["fps"] == 15
    assert info["topology"] == "fixed"
    assert info["has_vertex_correspondence"] is True
    assert info["first_frame"] == {
        "index": 0, "vertices": 16, "triangles": 18,
        "attributes": ["positions", "triangles"],
    }
    assert main(["inspect", str(path)]) == 0
    report = capsys.readouterr().out
    assert "Frames: 3" in report
    assert "16 vertices, 18 triangles" in report


def test_demo_default_destination(tmp_path, monkeypatch, capsys):
    monkeypatch.chdir(tmp_path)
    assert main(["demo", "--side", "3", "--frames", "1"]) == 0
    assert (tmp_path / "open4d-demo" / "frame_000000.ply").is_file()
    assert main(["demo"]) == 1
    assert "destination already exists" in capsys.readouterr().err


def test_sample_repeat_command_preserves_timing(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    original = write_demo(tmp_path / "sample", side=3, frames=3, fps=30000 / 1001)
    notes = (original / "README.md").read_text()
    command = notes.split("```bash\n", 1)[1].split("\n```", 1)[0]
    assert main(shlex.split(command)[1:]) == 0
    with load(original) as first, load(tmp_path / "another-wave") as repeated:
        assert repeated.timestamps == first.timestamps
        assert repeated.metadata == first.metadata


def test_inspect_uses_registered_codec_loader(tmp_path, capsys, monkeypatch):
    from open4d.codec import _api
    from open4d.codec._npz import NumPyZipCodec

    monkeypatch.setitem(_api._CODECS, "test-fixture", NumPyZipCodec())
    with mesh_sequence(side=3, frames=2) as sequence:
        path = save(sequence, tmp_path / "existing.o4d", codec="test-fixture")
    assert main(["inspect", str(path), "--json"]) == 0
    info = json.loads(capsys.readouterr().out)
    assert info["frame_count"] == 2
    assert info["first_frame"]["vertices"] == 9


def test_inspect_closes_loaded_sequence(tmp_path, capsys, monkeypatch):
    path = write_demo(tmp_path / "sample", side=3, frames=2)
    sequence = load(path)
    monkeypatch.setattr("open4d._cli.load", lambda *args, **kwargs: sequence)
    assert main(["inspect", str(path)]) == 0
    assert sequence.closed


def test_view_passes_controls_and_closes_sequence(tmp_path, monkeypatch):
    from open4d.visualization import _qt

    path = write_demo(tmp_path / "sample", side=3, frames=3, fps=10)
    opened = []

    def view(sequence, **options):
        assert sequence.fps == 10  # --fps changes playback, not stored timing.
        assert options == {
            "fps": 20, "up": "y", "stride": 2, "width": 320,
            "height": 240, "wireframe": True,
        }
        assert not sequence.closed
        opened.append(sequence)

    monkeypatch.setattr(_qt, "check_available", lambda: None)
    monkeypatch.setattr("open4d.visualization.visualize", view)
    assert main([
        "view", str(path), "--fps", "20", "--up", "y", "--stride", "2",
        "--width", "320", "--height", "240", "--wireframe",
    ]) == 0
    assert opened[0].closed


def test_missing_player_is_reported_before_decoding(tmp_path, monkeypatch, capsys):
    from open4d.visualization import VisualizationDependencyError, _qt

    path = tmp_path / "take.o4d"
    path.touch()

    def unavailable():
        raise VisualizationDependencyError("Install with: python -m pip install 'open4d[player]'")

    def unexpected_load(*args, **kwargs):
        pytest.fail("must check player dependencies before decoding")

    monkeypatch.setattr(_qt, "check_available", unavailable)
    monkeypatch.setattr("open4d._cli.load", unexpected_load)
    assert main(["view", str(path)]) == 1
    assert "open4d[player]" in capsys.readouterr().err


@pytest.mark.parametrize("arguments", [
    ["demo", "--side", "1"], ["demo", "--frames", "0"], ["demo", "--fps", "nan"],
    ["view", "sample", "--fps", "inf"], ["view", "sample", "--width", "0"],
    ["inspect", "sample", "--input-fps", "-1"],
])
def test_invalid_command_options_return_usage_error(arguments, capsys):
    with pytest.raises(SystemExit) as error:
        main(arguments)
    assert error.value.code == 2
    assert "Traceback" not in capsys.readouterr().err


def test_missing_and_malformed_sources_have_readable_errors(tmp_path, capsys):
    assert main(["inspect", str(tmp_path / "absent")]) == 1
    assert "does not exist" in capsys.readouterr().err
    bad = tmp_path / "broken.ply"
    bad.write_text("not a PLY file")
    assert main(["inspect", str(bad)]) == 1
    error = capsys.readouterr().err
    assert "Could not decode" in error
    assert "Traceback" not in error
