from __future__ import annotations

import json
import os
from pathlib import Path
import sys
from types import SimpleNamespace
import xml.etree.ElementTree as ET

import numpy as np
import pytest

from open4d import Frame, MemoryFrameProvider, Sequence, TriangleMesh
from open4d.codec import CodecError
from open4d.codec import _tracked, _tracked_worker
from open4d.codec._tracked import TSMC_CODEC, TVMC_CODEC
from open4d.io._mesh import write_obj

pytestmark = pytest.mark.cpu


def sequence():
    return Sequence(MemoryFrameProvider([
        Frame(10 + index, index / 24, TriangleMesh(
            [[float(index), 0, 0], [index + 1.0, 0, 0], [float(index), 1, 0]], [[0, 1, 2]],
        ), {"frame": index}) for index in range(2)
    ], metadata={"subject": "test"}))


def backend(tmp_path, codec):
    root = tmp_path / codec.id
    tools = root / ("TVMC" if codec.id == "tvmc" else "tsmc")
    tools.mkdir(parents=True)
    for name in ("get_reference_center.py", "get_transformation.py",
                 "extract_reference_mesh.py", "get_displacements.py"):
        (tools / name).write_text("# Research entry point\n")
    return root


def options(root):
    return dict(backend=root, python=sys.executable, encoder=sys.executable,
                decoder=sys.executable, dotnet=sys.executable)


def encoded_payload(request):
    settings = json.loads(request.read_text())
    output = Path(settings["output"])
    (output / "reference.drc").write_bytes(b"reference")
    if settings["codec"] == "tvmc":
        for index in range(settings["frames"]):
            (output / f"displacement_{index:06d}.drc").write_bytes(b"offsets")
            np.save(output / f"displacement_{index:06d}.npy", np.arange(3, dtype=np.uint32))
    else:
        for name in ("B_matrix.txt", "T_matrix.txt", "delta_trajectories_encoded.npy", "entropy_model.npz"):
            (output / name).write_bytes(b"native data")
    return settings


@pytest.mark.parametrize("codec", [TVMC_CODEC, TSMC_CODEC])
def test_codec_directory_round_trip_preserves_timing_and_cleans_up(tmp_path, monkeypatch, codec):
    root = backend(tmp_path, codec)
    requests = []

    def run(python, action, request):
        settings = json.loads(request.read_text())
        requests.append(settings)
        assert python == str(Path(sys.executable).absolute())
        if action == "encode":
            encoded_payload(request)
            assert len(list(Path(settings["input"]).glob("*.obj"))) == 2
        else:
            for index in range(settings["frames"]):
                write_obj(Path(settings["output"]) / f"frame_{index:06d}.obj",
                          [[index, 0, 0], [index + 1, 0, 0], [index, 1, 0]], [[0, 1, 2]])

    monkeypatch.setattr(_tracked, "_run", run)
    source = sequence()
    destination = tmp_path / f"with spaces{codec.suffixes[0]}"
    assert codec.encode(source, destination, **options(root)) == destination
    assert codec.can_decode(destination)
    assert not Path(requests[0]["input"]).exists()
    assert not list(root.rglob("*.obj"))
    with codec.decode(destination, backend=root, python=sys.executable, decoder=sys.executable) as decoded:
        assert decoded.timestamps == source.timestamps
        assert decoded.metadata == source.metadata
        assert decoded.has_vertex_correspondence is True
        assert decoded[1].frame_index == 11
        assert decoded[1].metadata == source[1].metadata
        np.testing.assert_array_equal(decoded[1].geometry.positions, source[1].geometry.positions)
        temporary = Path(requests[1]["output"])
        assert temporary.exists()
    assert not temporary.exists()


@pytest.mark.parametrize("codec", [TVMC_CODEC, TSMC_CODEC])
def test_empty_directory_created_at_publication_is_preserved(tmp_path, monkeypatch, codec):
    root = backend(tmp_path, codec)
    destination = tmp_path / "encoded"
    exists = Path.exists
    ready = False
    competing_inode = None

    def encode(python, action, request):
        nonlocal ready
        encoded_payload(request)
        ready = True

    def create_after_check(path):
        nonlocal competing_inode
        found = exists(path)
        if path == destination and ready and not found:
            path.mkdir()
            competing_inode = path.stat().st_ino
        return found

    monkeypatch.setattr(_tracked, "_run", encode)
    monkeypatch.setattr(Path, "exists", create_after_check)
    with pytest.raises(FileExistsError):
        codec.encode(sequence(), destination, **options(root))
    assert competing_inode is not None
    assert destination.stat().st_ino == competing_inode
    assert list(destination.iterdir()) == []
    assert not list(tmp_path.glob(".encoded-*"))


@pytest.mark.parametrize("codec", [TVMC_CODEC, TSMC_CODEC])
def test_failed_publication_and_restore_preserve_original_backup(tmp_path, monkeypatch, codec):
    root = backend(tmp_path, codec)
    destination = tmp_path / "encoded"
    destination.mkdir()
    (destination / "existing.txt").write_text("original")
    rename = Path.rename
    backup = None

    def fail_publication(source, target):
        nonlocal backup
        target = Path(target)
        if source == destination:
            backup = target
        elif target == destination and source != backup:
            destination.mkdir()
            (destination / "competing.txt").write_text("other writer")
            raise OSError("publication failed")
        return rename(source, target)

    monkeypatch.setattr(_tracked, "_run", lambda python, action, request: encoded_payload(request))
    monkeypatch.setattr(Path, "rename", fail_publication)
    with pytest.raises(OSError) as error:
        codec.encode(sequence(), destination, overwrite=True, **options(root))
    assert backup is not None
    assert (backup / "existing.txt").read_text() == "original"
    assert (destination / "competing.txt").read_text() == "other writer"
    assert str(backup) in str(error.value)
    assert sorted(tmp_path.iterdir()) == sorted([root, destination, backup])


@pytest.mark.parametrize("codec", [TVMC_CODEC, TSMC_CODEC])
@pytest.mark.parametrize("publication_failure", [False, True])
def test_overwrite_cleans_backup_after_publication_or_restore(
    tmp_path, monkeypatch, codec, publication_failure,
):
    root = backend(tmp_path, codec)
    destination = tmp_path / "encoded"
    destination.mkdir()
    (destination / "existing.txt").write_text("original")
    rename = Path.rename

    def fail_publication(source, target):
        if publication_failure and source.name == "encoded" and Path(target) == destination:
            raise OSError("publication failed")
        return rename(source, target)

    monkeypatch.setattr(_tracked, "_run", lambda python, action, request: encoded_payload(request))
    monkeypatch.setattr(Path, "rename", fail_publication)
    if publication_failure:
        with pytest.raises(OSError, match="publication failed"):
            codec.encode(sequence(), destination, overwrite=True, **options(root))
        assert (destination / "existing.txt").read_text() == "original"
    else:
        codec.encode(sequence(), destination, overwrite=True, **options(root))
        assert (destination / "reference.drc").read_bytes() == b"reference"
        assert not (destination / "existing.txt").exists()
    assert sorted(tmp_path.iterdir()) == sorted([root, destination])


@pytest.mark.parametrize("codec", [TVMC_CODEC, TSMC_CODEC])
def test_failed_encode_preserves_existing_destination(tmp_path, monkeypatch, codec):
    root = backend(tmp_path, codec)
    destination = tmp_path / "encoded"
    destination.mkdir()
    sentinel = destination / "existing.txt"
    sentinel.write_text("keep")
    monkeypatch.setattr(_tracked, "_run", lambda *args: None)
    with pytest.raises(CodecError, match="produced no reference.drc"):
        codec.encode(sequence(), destination, overwrite=True, **options(root))
    assert sentinel.read_text() == "keep"
    assert not list(tmp_path.glob(".encoded-*"))


def test_missing_backend_and_invalid_options_fail_before_launch(tmp_path):
    with pytest.raises(CodecError, match="OPEN4D_TVMC_ROOT"):
        TVMC_CODEC.encode(sequence(), tmp_path / "output", backend=tmp_path / "missing")
    for kwargs in ({"key_frame": -1}, {"num_centers": True}, {"quantization": 31}):
        with pytest.raises(ValueError):
            TVMC_CODEC.encode(sequence(), tmp_path / "output", **kwargs)
    with pytest.raises(ValueError, match="components"):
        TSMC_CODEC.encode(sequence(), tmp_path / "output", components=7)
    with pytest.raises(TypeError, match="only to TSMC"):
        TVMC_CODEC.encode(sequence(), tmp_path / "output", components=5)
    with pytest.raises(ValueError, match="preceding frame"):
        TSMC_CODEC.encode(sequence(), tmp_path / "output", key_frame=0)


def test_decode_rejects_wrong_codec_and_cleans_up_incomplete_output(tmp_path, monkeypatch):
    source = tmp_path / "artifact.tvmc"
    source.mkdir()
    manifest = {"codec": "tvmc", "version": 1,
                "frames": [{"frame_index": 10, "timestamp": 0}, {"frame_index": 11, "timestamp": 0.1}]}
    (source / "metadata.json").write_text(json.dumps(manifest))
    assert not TSMC_CODEC.can_decode(source)
    with pytest.raises(CodecError, match="invalid tsmc"):
        TSMC_CODEC.decode(source)
    decoded_directories = []

    def incomplete(python, action, request):
        output = Path(json.loads(request.read_text())["output"])
        decoded_directories.append(output)
        write_obj(output / "frame_000000.obj", [[0, 0, 0], [1, 0, 0], [0, 1, 0]], [[0, 1, 2]])

    monkeypatch.setattr(_tracked, "_run", incomplete)
    with pytest.raises(CodecError, match="decoded 1 frames, expected 2"):
        TVMC_CODEC.decode(source, backend=tmp_path / "not required for tvmc decode", decoder=sys.executable)
    assert not decoded_directories[0].exists()


@pytest.mark.parametrize("changed", [
    {"version": True}, {"metadata": []}, {"allow_nonmonotonic_timestamps": 1},
    {"frames": [{"frame_index": -1, "timestamp": 0}]},
    {"frames": [{"frame_index": 0, "timestamp": 0, "metadata": []}]},
    {"frames": [{"frame_index": 0, "timestamp": 1}, {"frame_index": 1, "timestamp": 0}]},
])
def test_tracked_manifest_rejects_invalid_metadata_before_native_work(tmp_path, changed):
    source = tmp_path / "take.tvmc"
    source.mkdir()
    value = {"version": 1, "codec": "tvmc", "frames": [{"frame_index": 0, "timestamp": 0}]}
    (source / "metadata.json").write_text(json.dumps(value | changed))
    with pytest.raises(CodecError):
        _tracked._manifest(source, "tvmc")
    assert not TVMC_CODEC.can_decode(source)


@pytest.mark.parametrize("codec", [TVMC_CODEC, TSMC_CODEC])
def test_research_stage_commands_use_private_working_directory(tmp_path, monkeypatch, codec):
    root = backend(tmp_path, codec)
    editor = root / "tvm-editing/TVMEditor.Test/bin/Release/net10.0/TVMEditor.Test.dll"
    editor.parent.mkdir(parents=True)
    editor.write_bytes(b"editor")
    inputs = tmp_path / "input"
    inputs.mkdir()
    centers = tmp_path / "tracked"
    centers.mkdir()
    for index in range(2):
        (inputs / f"mesh_{index:03d}.obj").write_text("input")
        np.savetxt(centers / f"mesh_{index:03d}.xyz", [[0, 0, 0], [1, 1, 1]])
    settings = options(root) | {
        "codec": codec.id, "input": str(inputs), "backend": str(root),
        "frames": 2, "centers": str(centers), "num_centers": 2, "key_frame": 1,
    }
    calls = []

    def run(command, cwd=None):
        calls.append(([str(item) for item in command], cwd))
        if str(command[1]).endswith("extract_reference_mesh.py"):
            destination = Path(command[command.index("--outputDir") + 1])
            destination.mkdir(parents=True)
            (destination / "decimated_reference_mesh.obj").write_text("reference")

    monkeypatch.setenv("TSMC_EDITOR_BUILD", "ignored")
    monkeypatch.setattr(_tracked_worker, "_run", run)
    tools, reference, displacements = _tracked_worker._fit(settings)
    names = [Path(command[1]).name for command, _ in calls]
    assert names == ["get_reference_center.py", "get_transformation.py", "TVMEditor.Test.dll",
                     "extract_reference_mesh.py", "TVMEditor.Test.dll", "get_displacements.py"]
    assert all(cwd.is_relative_to(tmp_path / "pipeline") for _, cwd in calls)
    assert reference.is_relative_to(tmp_path / "pipeline")
    assert not list(root.rglob("*.obj"))
    assert "--key" in calls[3][0]
    assert calls[2][0][2] == ("basketball" if codec.id == "tvmc" else "open4d")


def test_tracking_xml_escapes_user_paths(tmp_path):
    path = tmp_path / "tracking.xml"
    source = tmp_path / "A & B"
    _tracked_worker._tracking_config(path, source, tmp_path / "centers", {
        "frames": 2, "grid_resolution": 128, "num_centers": 50,
    })
    parsed = ET.parse(path)
    assert parsed.findtext("inDir") == str(source)
    assert parsed.findtext("lastIndex") == "1"
    assert parsed.findtext("pointCount") == "50"


def test_worker_failure_reports_native_diagnostic(tmp_path, monkeypatch):
    def failed(command, **kwargs):
        assert command[1].endswith("_tracked_worker.py")
        assert kwargs["cwd"] == tmp_path
        kwargs["stdout"].write("missing native dependency\n")
        return SimpleNamespace(returncode=2)

    monkeypatch.setattr(_tracked.subprocess, "run", failed)
    with pytest.raises(CodecError, match="missing native dependency"):
        _tracked._run(sys.executable, "encode", tmp_path / "request.json")


def test_entropy_parameters_preserve_native_column_models(tmp_path):
    delta = np.array([[0.125, 4.0], [-0.75, 4.0], [0.33333, 4.0]])
    source = tmp_path / "delta.npy"
    model = tmp_path / "model.npz"
    np.save(source, delta)
    _tracked_worker._save_entropy_model(source, model)
    expected = np.round(delta * 10000).astype(np.int32)
    with np.load(model) as saved:
        np.testing.assert_array_equal(saved["shape"], delta.shape)
        np.testing.assert_array_equal(saved["means"], expected.mean(axis=0))
        np.testing.assert_allclose(saved["stds"], [expected[:, 0].std(), 1])
        assert saved["minimum"] == expected.min()
        assert saved["maximum"] == expected.max()


@pytest.mark.parametrize("delta", [
    np.array([[0.125, 4.0], [-0.75, 4.0], [0.33333, 4.0]]),
    np.zeros((3, 2)), np.full((3, 2), 0.125),
])
def test_entropy_payload_decodes_without_original_array(tmp_path, delta):
    constriction = pytest.importorskip("constriction")
    source = tmp_path / "delta.npy"
    model_path = tmp_path / "model.npz"
    encoded = tmp_path / "encoded.npy"
    np.save(source, delta)
    _tracked_worker._save_entropy_model(source, model_path)
    expected = np.round(delta * 10000).astype(np.int32)
    with np.load(model_path) as saved:
        model = constriction.stream.model.QuantizedGaussian(int(saved["minimum"]), int(saved["maximum"]))
        encoder = constriction.stream.stack.AnsCoder()
        encoder.encode_reverse(expected.ravel(), model,
                               np.tile(saved["means"], 3), np.tile(saved["stds"], 3))
        np.save(encoded, encoder.get_compressed())
    source.unlink()
    actual = _tracked_worker._decode_entropy(encoded, model_path)
    np.testing.assert_array_equal(actual, expected / 10000)


@pytest.mark.parametrize("changed", [
    {"shape": [2.5, 2]}, {"minimum": 0, "maximum": 0},
    {"minimum": 0, "maximum": 2**24}, {"minimum": 0.5},
    {"means": [np.nan, 0]}, {"stds": [0, 1]}, {"stds": [np.inf, 1]},
    {"scaling_factor": 0}, {"scaling_factor": [1, 2]},
])
def test_invalid_entropy_model_rejected_before_native_decoder(tmp_path, changed):
    pytest.importorskip("constriction")
    encoded, model = tmp_path / "encoded.npy", tmp_path / "model.npz"
    np.save(encoded, np.array([1], dtype=np.uint32))
    np.savez(model, **({"shape": [2, 2], "minimum": -1, "maximum": 1,
                       "means": [0., 0.], "stds": [1., 1.], "scaling_factor": 10000} | changed))
    with pytest.raises(ValueError, match="entropy"):
        _tracked_worker._decode_entropy(encoded, model)


@pytest.mark.parametrize("delta", [np.array([[np.nan]]), np.array([[300000.]]),
                                  np.array([[0., 2000.]])])
def test_unrepresentable_entropy_input_is_rejected(tmp_path, delta):
    source, model = tmp_path / "delta.npy", tmp_path / "model.npz"
    np.save(source, delta)
    with pytest.raises(ValueError, match="entropy"):
        _tracked_worker._save_entropy_model(source, model)


def test_tvmc_native_payload_decodes_without_original_geometry(tmp_path):
    encoder = os.environ.get("OPEN4D_TEST_TVMC_ENCODER")
    decoder = os.environ.get("OPEN4D_TEST_TVMC_DECODER")
    if not encoder or not decoder:
        pytest.skip("set OPEN4D_TEST_TVMC_ENCODER and OPEN4D_TEST_TVMC_DECODER to native Draco tools")
    o3d = pytest.importorskip("open3d")
    reference = tmp_path / "reference.obj"
    mesh = o3d.geometry.TriangleMesh.create_box()
    o3d.io.write_triangle_mesh(str(reference), mesh, write_vertex_normals=False)
    mesh = o3d.io.read_triangle_mesh(str(reference))
    vertices = np.asarray(mesh.subdivide_midpoint(number_of_iterations=1).vertices)
    displacements = tmp_path / "displacements"
    displacements.mkdir()
    encoded = tmp_path / "take.tvmc"
    encoded.mkdir()
    offsets = np.random.default_rng(4).uniform(-0.1, 0.1, (2, len(vertices), 3))
    for index in range(2):
        np.savetxt(displacements / f"displacements_open4d_{index:03d}.txt", offsets[index])
        cloud = o3d.t.geometry.PointCloud(o3d.core.Tensor(offsets[index], dtype=o3d.core.float32))
        o3d.t.io.write_point_cloud(str(tmp_path / f"dis_open4d_{index:03d}.ply"), cloud, write_ascii=True)
    _tracked_worker._tvmc_encode({
        "output": str(encoded), "encoder": encoder, "decoder": decoder,
        "frames": 2, "quantization": 14,
    }, reference, displacements)
    (encoded / "metadata.json").write_text(json.dumps({
        "codec": "tvmc", "version": 1,
        "frames": [{"frame_index": index, "timestamp": index / 24} for index in range(2)],
    }))
    import shutil

    shutil.rmtree(displacements)
    reference.unlink()
    for path in tmp_path.glob("*.ply"):
        path.unlink()
    with TVMC_CODEC.decode(encoded, python=sys.executable, decoder=decoder) as decoded:
        from scipy.spatial import cKDTree

        for index in range(2):
            distance, _ = cKDTree(vertices + offsets[index]).query(decoded[index].geometry.positions)
            assert distance.max() < 0.001
        assert decoded.timestamps == (0.0, 1 / 24)
