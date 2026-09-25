"""Carriage/interchange tests; opaque fixtures do not claim native codec fidelity."""
import hashlib
import json
from pathlib import Path
import shutil

import pytest

import open4d
from open4d.codec import CodecError, inspect_vmesh, pack_vmesh, unpack_vmesh
from open4d.codec._native_profiles import NEURAL_CODECS, layout

pytestmark = pytest.mark.cpu


def test_gstream_additions_are_visible_only_in_their_own_frame(tmp_path, monkeypatch):
    import sys
    from types import SimpleNamespace
    import open4d._native_worker as worker

    torch = pytest.importorskip("torch")
    fields = ("_xyz", "_features_dc", "_features_rest", "_opacity", "_scaling", "_rotation")
    queried_counts, rendered = [], []

    class Model:
        def __init__(self, *args):
            pass

        def load_ply(self, path, scale):
            value = 100 + int(Path(path).stem[6:]) if "added_" in path else 0
            for name in fields:
                setattr(self, name, torch.tensor([[float(value)]]))

        def query_ntc(self):
            queried_counts.append(len(self._xyz))

        def update_by_ntc(self):
            self._xyz = self._xyz + 1

    class Network:
        def __init__(self, **kwargs):
            pass

        def cuda(self):
            return self

    class Cache:
        def __init__(self, *args):
            pass

        def load_state_dict(self, state):
            pass

    monkeypatch.setitem(sys.modules, "scene.gaussian_model", SimpleNamespace(GaussianModel=Model))
    monkeypatch.setitem(sys.modules, "tinycudann", SimpleNamespace(NetworkWithInputEncoding=Network))
    monkeypatch.setitem(sys.modules, "ntc", SimpleNamespace(NeuralTransformationCache=Cache))
    monkeypatch.setattr(torch, "load", lambda *a, **k: {"xyz_bound_min": 0, "xyz_bound_max": 1})
    monkeypatch.setattr(worker, "_save_gaussians", lambda model, path: rendered.append(model._xyz.clone()))
    (tmp_path / "ntc_config.json").write_text(json.dumps({"network": {}, "encoding": {}}))
    metadata = {"frames": [{}, {}, {}], "native": {
        "sh_degree": 1, "rotate_sh": False, "only_mlp": False, "added": [False, True, True],
    }}

    worker._gstream(tmp_path, tmp_path, metadata)

    assert queried_counts == [1, 1]
    assert [frame.flatten().tolist() for frame in rendered] == [[0], [1, 101], [2, 102]]


def native_run(tmp_path, codec):
    root = tmp_path / codec
    root.mkdir()
    options = {}
    if codec == "vega":
        (root / "manifest.json").write_text(json.dumps({"frames": [dict(frame_idx=i, group_id=3, frame_type="key" if i == 0 else "residual", file=f"frame_{i:04d}.pt") for i in range(2)]}))
        for name in ("color_model.pt", "frame_0000.pt", "frame_0001.pt"):
            (root / name).write_bytes(bytes(range(256)) * 5000)
    elif codec == "queen":
        for i in (1, 2):
            frame = root / f"frames/{i:04d}"
            (frame / "compressed").mkdir(parents=True)
            (frame / "point_cloud.ply").write_bytes(b"opaque dense fixture")
            (frame / "compressed/point_cloud.pkl").write_bytes(b"opaque compressed fixture")
        options["config"] = dict(sh_degree=1, gate_params=["on"] + ["none"] * 6)
    elif codec == "3dgstream":
        (root / "init").mkdir()
        (root / "init/point_cloud.ply").write_bytes(b"opaque initial fixture")
        frame = root / "frame000001"
        frame.mkdir()
        (frame / "NTC.pth").write_bytes(b"opaque transform fixture")
        (frame / "point_cloud.ply").write_bytes(b"must not include this dense frame")
        (root / "cfg_args.json").write_text(json.dumps(dict(sh_degree=1, rotate_sh=True, only_mlp=False, iterations_s2=0)))
        (root / "ntc_config.json").write_text(json.dumps(dict(network={}, encoding={})))
    else:
        config = dict(native=dict(group_size=2, pca=True), voxel_size=8,
                      fine_model_and_render=dict(rgbnet_dim=12, stepsize=.5),
                      data=dict(datadir="", white_bkgd=False, inverse_y=True, flip_x=False, flip_y=False),
                      render=dict(near=.1, far=10.0))
        (root / "decoder_config.json").write_text(json.dumps(config))
        (root / "model_kwargs.json").write_text('{}')
        (root / "rgb_net.tar").write_bytes(b"opaque shared network")
        for i in (0, 1):
            headers = [dict(quality=8 - half, size=[1, 2, 8, 8, 8]) for half in range(i + 1)]
            (root / f"header_{i}.json").write_text(json.dumps({"headers": headers}))
            for name in (f"mask_{i}.rerf", f"feature_{i}_8.rerf0", f"feature_{i}_8.rerf1"):
                (root / name).write_bytes(b"opaque features")
        for name in ("feature_1_7.rerf0", "feature_1_7.rerf1", "pca_m_1.npy", "deform_1.npy", "deform_mask_1.rerf"):
            (root / name).write_bytes(b"opaque residual dependencies")
    return root, options


@pytest.mark.parametrize("codec", sorted(NEURAL_CODECS))
def test_native_import_excludes_dense_exports_and_survives_source_removal(tmp_path, codec):
    source, options = native_run(tmp_path, codec)
    with open4d.import_native(source, codec=codec, timestamps=[.125, .875], frame_indices=[12, 40],
                             metadata={"units": "m"}, frame_metadata=[{"label": "key"}, {"label": "later"}], **options) as native:
        expected = native.path.read_bytes()
        shutil.rmtree(source)
        artifact = open4d.save(native, tmp_path / "isolated.vmesh")
    assert artifact.read_bytes() == expected
    with open4d.load(artifact) as loaded:
        assert isinstance(loaded, open4d.NativeSequence)
        assert loaded.codec == codec and loaded.timestamps == (.125, .875)
        assert loaded.frame_indices == (12, 40) and loaded.metadata == {"units": "m"}
        manifest = loaded.manifest
        expected_names = [name for name, _ in layout(codec, 2, manifest["native"])]
        extracted = loaded.unpack(tmp_path / "native")
        assert sorted(p.name for p in extracted.iterdir()) == sorted(expected_names)
        assert not any(p.suffix == ".obj" for p in extracted.iterdir())
        assert not (extracted / "frame000001/point_cloud.ply").exists()
        repacked = pack_vmesh(extracted, tmp_path / "repacked.vmesh")
        assert repacked.read_bytes() == expected
    with pytest.raises(ValueError, match="closed"):
        loaded.decode()


@pytest.mark.parametrize("codec", sorted(NEURAL_CODECS))
def test_self_contained_usdc_restores_identical_native_bytes_without_runtime(tmp_path, codec, monkeypatch):
    pytest.importorskip("pxr.Usd")
    import open4d.native as module
    monkeypatch.setattr(module, "run", lambda *a, **k: pytest.fail("USD interchange must not execute native models"))
    source, options = native_run(tmp_path, codec)
    with open4d.import_native(source, codec=codec, timestamps=[1.125, 3.75], frame_indices=[5, 11], **options) as original:
        expected = original.path.read_bytes()
        usd = open4d.save(original, tmp_path / "take.usdc")
    shutil.rmtree(source)
    with open4d.load(usd) as restored:
        assert restored.timestamps == (1.125, 3.75)
        assert restored.frame_indices == (5, 11)
        assert restored.path.read_bytes() == expected
        output = open4d.save(restored, tmp_path / "again.vmesh")
    assert output.read_bytes() == expected
    output2 = open4d.encode(usd, tmp_path / "encode.vmesh", codec=codec)
    assert output2.read_bytes() == expected


@pytest.mark.parametrize("codec", sorted(NEURAL_CODECS))
def test_public_encode_accepts_native_research_outputs(tmp_path, codec):
    source, options = native_run(tmp_path, codec)
    result = open4d.encode(source, tmp_path / "encoded.vmesh", codec=codec, fps=12, **options)
    assert inspect_vmesh(result)["sequence"]["frames"][1]["timestamp"] == 1 / 12
    with pytest.raises(CodecError, match="contains"):
        open4d.load(result, codec="tsmc")
    with pytest.raises(TypeError, match="timestamps"):
        open4d.load(result, fps=24)


@pytest.mark.parametrize("codec,missing", [("queen", "frames/0002/compressed/point_cloud.pkl"),
                                         ("3dgstream", "frame000001/NTC.pth"),
                                         ("rerf", "deform_mask_1.rerf"),
                                         ("rerf", "feature_1_7.rerf1"),
                                         ("rerf", "pca_m_1.npy")])
def test_dense_frames_cannot_substitute_for_native_dependencies(tmp_path, codec, missing):
    source, options = native_run(tmp_path, codec)
    (source / missing).unlink()
    target = tmp_path / "result.vmesh"
    with pytest.raises((CodecError, FileNotFoundError)):
        open4d.encode(source, target, codec=codec, **options)
    assert not target.exists()


def test_usd_rejects_tampered_payload_and_timeline(tmp_path):
    Usd = pytest.importorskip("pxr.Usd")
    source, options = native_run(tmp_path, "queen")
    with open4d.import_native(source, codec="queen", **options) as native:
        usd = open4d.save(native, tmp_path / "take.usdc")
    stage = Usd.Stage.Open(str(usd))
    prim = stage.GetPrimAtPath("/Open4DNative")
    prim.GetAttribute("open4d:frameIndex").Set(999, 0)
    stage.GetRootLayer().Save()
    with pytest.raises(CodecError, match="timeline"):
        open4d.load(usd)
    payload = prim.GetAttribute("open4d:payload:chunk000000")
    values = payload.Get()
    values[100] ^= 1
    payload.Set(values)
    stage.GetRootLayer().Save()
    with pytest.raises(CodecError, match="SHA-256"):
        open4d.load(usd)


def test_unrecognized_independent_research_codecs_are_not_native_profiles(tmp_path):
    for codec in ("klt", "qndf", "qndf_int8", "obj"):
        with pytest.raises(ValueError, match="codec"):
            open4d.import_native(tmp_path, codec=codec)


def test_native_overwrite_is_atomic_and_explicit(tmp_path):
    source, options = native_run(tmp_path, "queen")
    with open4d.import_native(source, codec="queen", **options) as native:
        target = tmp_path / "keep.vmesh"
        target.write_bytes(b"keep")
        with pytest.raises(FileExistsError):
            open4d.save(native, target)
        assert target.read_bytes() == b"keep"
        open4d.save(native, target, overwrite=True)
        assert target.read_bytes() == native.path.read_bytes()


def test_native_usd_rejects_duplicate_times_before_publishing(tmp_path):
    pytest.importorskip("pxr.Usd")
    source, options = native_run(tmp_path, "queen")
    destination = tmp_path / "duplicate.usdc"
    with open4d.import_native(source, codec="queen", timestamps=[0, 0], **options) as native:
        with pytest.raises(CodecError, match="strictly increasing"):
            open4d.save(native, destination)
    assert not destination.exists()
