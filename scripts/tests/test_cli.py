from __future__ import annotations

import json
import shlex

import pytest

from open4d import import_native, load, save
from open4d._cli import main
from open4d.demo import mesh_sequence, write_demo


pytestmark = pytest.mark.cpu


def npz_vmesh(tmp_path, monkeypatch):
    from open4d.codec import _api
    from open4d.codec._npz import NumPyZipCodec

    monkeypatch.setitem(_api._CODECS, "npz", NumPyZipCodec())
    with mesh_sequence(side=3, frames=2) as sequence:
        return save(sequence, tmp_path / "existing.vmesh", codec="npz")


def queen_vmesh(tmp_path):
    # Opaque payloads: header inspection never deserializes them.
    root = tmp_path / "queen"
    for index in (1, 2):
        frame = root / f"frames/{index:04d}"
        (frame / "compressed").mkdir(parents=True)
        (frame / "point_cloud.ply").write_bytes(b"opaque dense fixture")
        (frame / "compressed/point_cloud.pkl").write_bytes(b"opaque compressed fixture")
    config = dict(sh_degree=1, gate_params=["on"] + ["none"] * 6)
    with import_native(root, codec="queen", config=config, timestamps=[0.5, 1.0]) as native:
        return save(native, tmp_path / "queen.vmesh")


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
    path = npz_vmesh(tmp_path, monkeypatch)
    assert main(["inspect", str(path), "--json", "--decode"]) == 0
    info = json.loads(capsys.readouterr().out)
    assert info["frame_count"] == 2
    assert info["first_frame"]["vertices"] == 9
    assert info["topology"] == "fixed"
    assert info["container"]["codec"] == "npz"


def test_inspect_reads_mesh_vmesh_header_without_decoding(tmp_path, capsys, monkeypatch):
    path = npz_vmesh(tmp_path, monkeypatch)
    monkeypatch.setattr("open4d._cli.load", lambda *a, **k: pytest.fail("inspect must not decode"))
    assert main(["inspect", str(path), "--json"]) == 0
    info = json.loads(capsys.readouterr().out)
    with load(path) as sequence:
        timestamps, fps, span = sequence.timestamps, sequence.fps, sequence.duration
    assert info["frame_count"] == 2 and info["fps"] == fps
    assert (info["first_timestamp"], info["last_timestamp"]) == (timestamps[0], timestamps[-1])
    assert info["timestamp_span_seconds"] == span
    assert "topology" not in info and "first_frame" not in info
    container = info["container"]
    assert container["format"] == "vmesh" and container["codec"] == "npz"
    assert container["representation"] == "triangle_mesh" and container["decodes_to_mesh"] is True
    assert container["dependency_mode"] == "independent"
    assert container["payload_files"] == 2
    assert 0 < container["payload_bytes"] < container["file_bytes"] == path.stat().st_size
    assert main(["inspect", str(path)]) == 0
    report = capsys.readouterr().out
    assert "Container: VMESH, codec npz, triangle_mesh" in report
    assert "Payload: 2 files" in report and "pass --decode" in report
    assert main(["inspect", str(path), "--input-fps", "10"]) == 1
    assert "omit --format and --input-fps" in capsys.readouterr().err


def test_inspect_gaussian_vmesh_header_and_mesh_only_commands(tmp_path, capsys, monkeypatch):
    path = queen_vmesh(tmp_path)
    assert main(["inspect", str(path), "--json"]) == 0
    info = json.loads(capsys.readouterr().out)
    assert info["frame_count"] == 2 and info["fps"] == 2
    assert info["timestamp_span_seconds"] == 0.5
    assert info["container"]["representation"] == "gaussian_splats"
    assert info["container"]["dependency_mode"] == "previous-frame"
    assert info["container"]["decodes_to_mesh"] is False
    assert main(["inspect", str(path)]) == 0
    report = capsys.readouterr().out
    assert "codec queen, gaussian_splats" in report and "--decode" not in report
    assert main(["inspect", str(path), "--decode"]) == 1
    assert "queen stores gaussian_splats; --decode reports triangle meshes" in capsys.readouterr().err
    monkeypatch.setattr("open4d._cli.load", lambda *a, **k: pytest.fail("view must not decode"))
    assert main(["view", str(path)]) == 1
    assert "queen stores gaussian_splats; the viewer shows triangle meshes" in capsys.readouterr().err


def test_inspect_vmesh_usd_prim(tmp_path, capsys, monkeypatch):
    pytest.importorskip("pxr.Usd")
    from open4d.native import NativeSequence, save_native

    with NativeSequence(npz_vmesh(tmp_path, monkeypatch)) as native:
        mesh = save_native(native, tmp_path / "mesh.usdc")
    with load(queen_vmesh(tmp_path)) as native:
        gaussians = save(native, tmp_path / "queen.usda")
    assert main(["inspect", str(mesh), "--json"]) == 0
    info = json.loads(capsys.readouterr().out)
    container = info["container"]
    assert container["format"] == "usd" and container["codec"] == "npz"
    assert container["file_bytes"] == mesh.stat().st_size
    assert info["frame_count"] == 2 and "first_frame" not in info
    assert main(["inspect", str(mesh), "--json", "--decode"]) == 0
    assert json.loads(capsys.readouterr().out)["first_frame"]["vertices"] == 9
    assert main(["inspect", str(gaussians)]) == 0
    assert "Container: VMESH in USD, codec queen, gaussian_splats" in capsys.readouterr().out
    assert main(["view", str(gaussians)]) == 1
    assert "queen stores gaussian_splats; the viewer shows" in capsys.readouterr().err


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

    path = tmp_path / "take.vmesh"
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
