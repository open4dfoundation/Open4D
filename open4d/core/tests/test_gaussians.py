from __future__ import annotations

import dataclasses
import importlib
import json
import os
import subprocess
import sys
from pathlib import Path

import numpy as np
import pytest

from open4d import gaussians


def splats(count=2, degree=1):
    return gaussians.GaussianSplats(
        positions=np.arange(count * 3).reshape(count, 3),
        scales=np.ones((count, 3)), rotations=np.tile([2, 0, 0, 0], (count, 1)),
        opacities=np.full(count, 0.5), spherical_harmonics=np.zeros((count, (degree + 1) ** 2, 3)),
    )


def test_gaussian_geometry_conventions():
    frame = splats()
    np.testing.assert_array_equal(frame.rotations, [[1, 0, 0, 0], [1, 0, 0, 0]])
    assert frame.positions.dtype == np.float32
    assert len(frame) == 2
    assert frame.sh_degree == 1


@pytest.mark.parametrize("field,value,match", [
    ("positions", [[0, 0]], "positions"),
    ("positions", [[1e100, 0, 0], [0, 0, 0]], "finite"),
    ("scales", [[0, 1, 1], [1, 1, 1]], "positive"),
    ("rotations", np.zeros((2, 4)), "nonzero"),
    ("opacities", [0, 1.1], "between"),
    ("opacities", [[0], [1]], "shape"),
    ("spherical_harmonics", np.zeros((2, 3, 3)), "square"),
    ("spherical_harmonics", np.zeros((3, 4, 3)), "shape"),
])
def test_invalid_gaussian_attributes(field, value, match):
    with pytest.raises(ValueError, match=match):
        dataclasses.replace(splats(), **{field: value})


def write_ply(path, *, omit=()):
    names = ["x", "y", "z", "opacity", "scale_0", "scale_1", "scale_2",
             "rot_0", "rot_1", "rot_2", "rot_3", "f_dc_0", "f_dc_1", "f_dc_2"]
    values = [1, 2, 3, 0, 0, np.log(2), np.log(3), 2, 0, 0, 0, 0.1, 0.2, 0.3]
    names += [f"f_rest_{i}" for i in range(9)]
    values += list(range(9))
    pairs = [(name, value) for name, value in zip(names, values) if name not in omit]
    path.write_text("ply\nformat ascii 1.0\nelement vertex 1\n" +
                    "".join(f"property float {name}\n" for name, _ in pairs) +
                    "end_header\n" + " ".join(str(value) for _, value in pairs) + "\n")


def test_load_gaussian_ply_preserves_sh_order_and_activates_parameters(tmp_path):
    pytest.importorskip("plyfile")
    path = tmp_path / "frame.ply"
    write_ply(path)
    result = gaussians.load_gaussians(path)
    np.testing.assert_allclose(result.positions, [[1, 2, 3]])
    np.testing.assert_allclose(result.scales, [[1, 2, 3]])
    np.testing.assert_allclose(result.opacities, [0.5])
    np.testing.assert_allclose(result.rotations, [[1, 0, 0, 0]])
    np.testing.assert_allclose(result.spherical_harmonics,
                               [[[0.1, 0.2, 0.3], [0, 3, 6], [1, 4, 7], [2, 5, 8]]])


@pytest.mark.parametrize("omit,match", [(('scale_1',), 'missing'), (('f_rest_4',), 'incomplete')])
def test_rejects_incomplete_gaussian_ply(tmp_path, omit, match):
    pytest.importorskip("plyfile")
    path = tmp_path / "frame.ply"
    write_ply(path, omit=omit)
    with pytest.raises(ValueError, match=match):
        gaussians.load_gaussians(path)


def test_missing_ply_dependency_has_install_hint(monkeypatch):
    monkeypatch.setitem(sys.modules, "plyfile", None)
    with pytest.raises(ImportError, match=r"open4d\[gaussians\]"):
        gaussians.load_gaussians("unused.ply")


@pytest.fixture
def cli_runtime(tmp_path):
    runtime = tmp_path / "runtime with spaces" / "gs_tools"
    package = runtime / "gs_tools"
    package.mkdir(parents=True)
    (package / "__init__.py").write_text("")
    (package / "cli.py").write_text(
        "import json, sys\nfrom pathlib import Path\n"
        "args = sys.argv[1:]\nrun = Path(args[args.index('-m') + 1])\n"
        "run.mkdir(parents=True, exist_ok=True)\n"
        "(run / 'arguments.json').write_text(json.dumps(args))\n"
        "if '--fail' in args: raise SystemExit(7)\n"
        "for i in (1, 2):\n"
        "    frame = run / 'frames' / f'{i:04d}' / 'point_cloud.ply'\n"
        "    frame.parent.mkdir(parents=True, exist_ok=True)\n"
        "    frame.write_text('test fixture')\n"
    )
    return runtime


def test_reconstruction_uses_isolated_runtime_and_preserves_arguments(tmp_path, cli_runtime):
    scene = tmp_path / "camera images"
    scene.mkdir()
    output = tmp_path / "encoded scene"
    result = gaussians.reconstruct_gaussians(scene, output, runtime=cli_runtime,
                                             options=("--max_frames", "2"))
    assert result.method == "queen"
    assert len(result.frame_paths) == 2
    arguments = json.loads((output / "arguments.json").read_text())
    assert arguments[arguments.index("-s") + 1] == str(scene)
    assert arguments[-4:] == ["--log_ply", "--log_compressed", "--max_frames", "2"]
    assert "gs_tools" not in sys.modules


def test_reconstruction_failure_is_not_reported_as_success(tmp_path, cli_runtime):
    with pytest.raises(subprocess.CalledProcessError) as error:
        gaussians.reconstruct_gaussians(tmp_path, tmp_path / "failed", runtime=cli_runtime,
                                        options=("--fail",))
    assert error.value.returncode == 7
    assert not (tmp_path / "failed").exists()


@pytest.mark.parametrize("argument", ["-m", "-melsewhere", "--model_path=elsewhere",
                                      "--model_p", "--source_path", "--config"])
def test_reconstruction_options_cannot_override_managed_paths(tmp_path, cli_runtime, argument):
    with pytest.raises(ValueError, match="options"):
        gaussians.reconstruct_gaussians(tmp_path, tmp_path / "out", runtime=cli_runtime,
                                       options=(argument, "elsewhere"))
    assert not (tmp_path / "out").exists()


def test_reconstruction_keeps_generator_options(tmp_path, cli_runtime):
    output = tmp_path / "result"
    gaussians.reconstruct_gaussians(tmp_path, output, runtime=cli_runtime,
                                   options=iter(("--max_frames", "2")))
    assert json.loads((output / "arguments.json").read_text())[-2:] == ["--max_frames", "2"]


def test_3dgstream_does_not_return_initial_model_as_completed_sequence(tmp_path, cli_runtime, monkeypatch):
    scene = tmp_path / "scene"
    for index in range(2):
        (scene / f"frame{index:06d}").mkdir(parents=True)
    initial = tmp_path / "initial"
    ply = initial / "point_cloud" / "iteration_10" / "point_cloud.ply"
    ply.parent.mkdir(parents=True)
    ply.write_text("fixture")
    ntc = tmp_path / "ntc.pth"
    ntc.write_text("fixture")
    monkeypatch.setattr(gaussians, "_run", lambda *args: None)
    with pytest.raises(RuntimeError, match="every Gaussian frame"):
        gaussians.reconstruct_gaussians(scene, tmp_path / "output", method="3dgstream",
                                       runtime=cli_runtime, initial_model=initial,
                                       initial_iterations=10, ntc=ntc)
    assert ply.read_text() == "fixture"
    assert not (tmp_path / "output").exists()


def test_reconstruction_refuses_existing_output_before_launch(tmp_path, cli_runtime):
    output = tmp_path / "existing"
    output.mkdir()
    with pytest.raises(FileExistsError):
        gaussians.reconstruct_gaussians(tmp_path, output, runtime=cli_runtime)
    assert not (output / "arguments.json").exists()


def test_native_render_commands_do_not_call_destructive_extractor(monkeypatch, tmp_path):
    monkeypatch.setitem(sys.modules, "streamer", None)
    root = Path(__file__).resolve().parents[2] / "reconstruction" / "gs_tools"
    monkeypatch.syspath_prepend(str(root))
    try:
        base = importlib.import_module("gs_tools.methods.base")
        queen = importlib.import_module("gs_tools.methods.queen")
        gstream = importlib.import_module("gs_tools.methods.gstream")
        spec = base.RunSpec(tmp_path, tmp_path / "out")
        assert "--render_compressed" in queen.render_command(spec)
        assert "--render_compressed" not in queen.render_command(spec, compressed=False)
        assert queen.render_command(spec, compressed=False)[1] == "render_fvv_compressed.py"
        with pytest.raises(NotImplementedError, match="viewer"):
            gstream.render_command(spec)
    finally:
        for name in list(sys.modules):
            if name == "gs_tools" or name.startswith("gs_tools."):
                sys.modules.pop(name)


def test_vega_encoding_rolls_back_failed_run(tmp_path, monkeypatch):
    runtime = tmp_path / "vega"
    (runtime / "vega").mkdir(parents=True)
    (runtime / "vega" / "encoder.py").touch()

    def fail(runtime, python, request, work):
        Path(request["output"]).mkdir()
        raise subprocess.CalledProcessError(1, [python])

    monkeypatch.setattr(gaussians, "_vega_command", fail)
    output = tmp_path / "encoded"
    with pytest.raises(subprocess.CalledProcessError):
        gaussians.encode_gaussians([splats(), splats()], output, runtime=runtime)
    assert not output.exists()
    assert not list(tmp_path.glob(".open4d-vega-*"))


def test_vega_decode_retains_neural_appearance(tmp_path, monkeypatch):
    def decode(runtime, python, request, work):
        frame = splats()
        for index in range(2):
            np.savez(work / f"frame_{index:06d}.npz", **{
                field: getattr(frame, field) for field in ("positions", "scales", "rotations", "opacities")
            })

    monkeypatch.setattr(gaussians, "_vega_command", decode)
    native_bitstream(tmp_path)
    run = gaussians.VegaRun(tmp_path, tmp_path, sys.executable)
    first, frame = run.decode()
    assert frame.appearance.run is run
    assert first.appearance.frame_index == 0
    assert frame.appearance.frame_index == 1
    assert not hasattr(frame, "spherical_harmonics")
    np.testing.assert_array_equal(frame.positions, splats().positions)


def native_bitstream(path, count=2):
    path.mkdir(exist_ok=True)
    frames = [{"frame_idx": index, "group_id": 0,
               "frame_type": "key" if index == 0 else "residual",
               "file": f"frame_{index:04d}.pt"} for index in range(count)]
    (path / "manifest.json").write_text(json.dumps({"frames": frames}))
    (path / "color_model.pt").write_bytes(b"test fixture")
    for frame in frames:
        (path / frame["file"]).write_bytes(b"test fixture")


@pytest.mark.parametrize("opacity", [0, 1])
def test_vega_rejects_infinite_training_logits(tmp_path, opacity):
    frame = dataclasses.replace(splats(), opacities=np.full(2, opacity))
    with pytest.raises(ValueError, match="strictly between"):
        gaussians.encode_gaussians([frame, frame], tmp_path / "encoded")


@pytest.mark.parametrize("incomplete", ["count", "chunk"])
def test_vega_does_not_publish_incomplete_bitstream(tmp_path, monkeypatch, incomplete):
    runtime = tmp_path / "vega"
    (runtime / "vega").mkdir(parents=True)
    (runtime / "vega" / "encoder.py").touch()

    def incomplete_output(runtime, python, request, work):
        output = Path(request["output"])
        native_bitstream(output, count=2 if incomplete == "chunk" else 3)
        if incomplete == "chunk":
            (output / "frame_0001.pt").unlink()

    monkeypatch.setattr(gaussians, "_vega_command", incomplete_output)
    with pytest.raises((ValueError, FileNotFoundError)):
        gaussians.encode_gaussians([splats(), splats()], tmp_path / "encoded", runtime=runtime)
    assert not (tmp_path / "encoded").exists()


def test_vega_preserves_destination_created_during_encoding(tmp_path, monkeypatch):
    runtime = tmp_path / "vega"
    (runtime / "vega").mkdir(parents=True)
    (runtime / "vega" / "encoder.py").touch()
    destination = tmp_path / "encoded"

    def encode(runtime, python, request, work):
        native_bitstream(Path(request["output"]))
        destination.mkdir()

    monkeypatch.setattr(gaussians, "_vega_command", encode)
    with pytest.raises(FileExistsError):
        gaussians.encode_gaussians([splats(), splats()], destination, runtime=runtime)
    assert list(destination.iterdir()) == []


def test_vega_static_objects_reuse_key_geometry():
    from open4d._gaussian_worker import _reconstruct_frames

    class Player:
        def reconstruct(self, chunk):
            if "position" in chunk:
                self.position = chunk["position"]
            return self.position

    chunks = [{"position": 0}, {"position": 10}, {}]
    decoded = [position for _, position in _reconstruct_frames(Player(), chunks)]
    assert decoded == [0, 10, 0]


def test_vega_does_not_return_partially_decoded_sequence(tmp_path, monkeypatch):
    native_bitstream(tmp_path)
    monkeypatch.setattr(gaussians, "_vega_command", lambda *args: None)
    run = gaussians.VegaRun(tmp_path, tmp_path, sys.executable)
    with pytest.raises(RuntimeError, match="every frame"):
        run.decode()


@pytest.mark.gpu
@pytest.mark.slow
def test_real_vega_round_trip(tmp_path):
    if os.environ.get("OPEN4D_TEST_GAUSSIANS") != "1":
        pytest.skip("Set OPEN4D_TEST_GAUSSIANS=1 with the native CUDA runtime configured")
    rng = np.random.default_rng(42)
    first = gaussians.GaussianSplats(
        positions=rng.uniform(-0.5, 0.5, (48, 3)), scales=np.full((48, 3), 0.06),
        rotations=np.tile([1, 0, 0, 0], (48, 1)), opacities=np.full(48, 0.8),
        spherical_harmonics=rng.uniform(-0.4, 0.4, (48, 1, 3)),
    )
    second = dataclasses.replace(first, positions=first.positions + [0.01, 0, 0])
    encoded = gaussians.encode_gaussians(
        [first, second], tmp_path / "vega", python=os.environ.get("OPEN4D_GS_PYTHON"),
        key_iterations=2, residual_iterations=2,
    )
    restored = encoded.decode()
    assert len(restored) == 2
    np.testing.assert_allclose(restored[0].positions, first.positions, atol=1e-6)
    colors = restored[1].appearance.colors(np.tile([0, 0, 1], (len(restored[1]), 1)))
    assert colors.shape == (len(restored[1]), 3)
    assert np.all((colors >= 0) & (colors <= 1))


def test_public_encode_decode_dispatch_to_vega(tmp_path, monkeypatch):
    import open4d
    from types import SimpleNamespace

    frames = [splats(), splats()]
    output = tmp_path / "capture.vega"
    calls = []
    def encode(values, path, **options):
        calls.append((values, path, options))
        return SimpleNamespace(path=path)
    monkeypatch.setattr(gaussians, "encode_gaussians", encode)
    monkeypatch.setattr(gaussians, "decode_gaussians", lambda path, **options: tuple(frames))
    assert open4d.encode(frames, output, codec="vega", key_iterations=10) == output
    assert calls == [(frames, output, {"key_iterations": 10})]
    assert open4d.decode(output) == tuple(frames)


def test_public_reconstruction_dispatch(tmp_path, monkeypatch):
    import open4d

    calls = []
    monkeypatch.setattr(gaussians, "reconstruct_gaussians",
                        lambda *args, **options: calls.append((args, options)))
    open4d.reconstruct(tmp_path / "scene", tmp_path / "result", method="queen", config="camera.json")
    assert calls == [((tmp_path / "scene", tmp_path / "result"),
                      {"method": "queen", "config": "camera.json"})]
    with pytest.raises(ValueError, match="method"):
        open4d.reconstruct([], method="unknown")
