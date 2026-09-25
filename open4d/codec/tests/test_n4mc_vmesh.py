"""N4MC carries a shared model and independent latents, not temporal residuals."""
import json
from zipfile import ZipFile

import numpy as np
import pytest

import open4d
from open4d.codec import CodecError, inspect_vmesh, pack_vmesh, unpack_vmesh
from open4d.codec import _n4mc

pytestmark = pytest.mark.cpu


def legacy_archive(path, *, omit=None, duplicate=False, normalization=None):
    manifest = dict(schema="open4d.n4mc-sequence/v1", codec="n4mc",
                    normalization=normalization or dict(center=[1, 2, 3], scale=2.),
                    metadata={"scene": "fixture"}, allow_nonmonotonic_timestamps=False,
                    frames=[dict(frame_index=7, timestamp=.125, metadata={"key": "first"}),
                            dict(frame_index=19, timestamp=.875, metadata={"key": "last"})])
    # These bytes only test carriage, never neural reconstruction.
    payloads = {"checkpoint.pt": b"opaque shared model", "frame_000000.npz": b"opaque latent 0",
                "frame_000001.npz": b"opaque latent 1"}
    with ZipFile(path, "w") as archive:
        archive.writestr("manifest.json", json.dumps(manifest))
        for name, data in payloads.items():
            if name != omit:
                archive.writestr(name, data)
        if duplicate:
            with pytest.warns(UserWarning, match="Duplicate"):
                archive.writestr("checkpoint.pt", b"ambiguous")
        archive.writestr("scratch.obj", b"never carry dense meshes")
    return manifest, payloads


def test_n4d_migration_preserves_native_payloads_without_loading_models(tmp_path, monkeypatch):
    source = tmp_path / "legacy.n4d"
    manifest, payloads = legacy_archive(source)
    monkeypatch.setattr(_n4mc, "_backend", lambda: pytest.fail("repacking must not load a model"))
    artifact = pack_vmesh(source, tmp_path / "motion.vmesh")
    source.unlink()
    assert artifact.read_bytes()[0] == 0x60
    info = inspect_vmesh(artifact)
    assert info["codec"] == "n4mc"
    assert info["representation"] == "neural_tsdf"
    assert info["dependency_mode"] == "shared-model-independent-frames"
    assert info["sequence"]["normalization"] == manifest["normalization"]
    assert info["sequence"]["frames"] == manifest["frames"]
    assert _n4mc.N4MC_CODEC.can_decode(artifact)
    recovered = unpack_vmesh(artifact, tmp_path / "recovered")
    assert sorted(p.name for p in recovered.iterdir()) == sorted(["metadata.json", *payloads])
    for name, data in payloads.items():
        assert (recovered / name).read_bytes() == data
    repacked = pack_vmesh(recovered, tmp_path / "again.vmesh")
    assert artifact.read_bytes() == repacked.read_bytes()
    with pytest.raises(CodecError, match="contains n4mc"):
        open4d.load(artifact, codec="tsmc")


@pytest.mark.parametrize("options", [dict(omit="checkpoint.pt"), dict(omit="frame_000001.npz"),
                                      dict(duplicate=True), dict(normalization={"center": [0, 0, 0], "scale": 0})])
def test_invalid_n4d_never_publishes_vmesh(tmp_path, options):
    source = tmp_path / "bad.n4d"
    legacy_archive(source, **options)
    destination = tmp_path / "result.vmesh"
    with pytest.raises(CodecError):
        pack_vmesh(source, destination)
    assert not destination.exists()


def test_n4mc_native_usdc_preserves_compressed_state_without_decoding(tmp_path, monkeypatch):
    pytest.importorskip("pxr.Usd")
    source = tmp_path / "legacy.n4d"
    legacy_archive(source)
    artifact = pack_vmesh(source, tmp_path / "motion.vmesh")
    monkeypatch.setattr(_n4mc, "_backend", lambda: pytest.fail("native USD must not decode N4MC"))
    with open4d.NativeSequence(artifact) as native:
        usd = open4d.save(native, tmp_path / "native.usdc")
    source.unlink()
    expected = artifact.read_bytes()
    artifact.unlink()
    with open4d.load(usd) as restored:
        assert restored.codec == "n4mc"
        assert restored.timestamps == (.125, .875)
        assert restored.frame_indices == (7, 19)
        assert restored.path.read_bytes() == expected
        repacked = open4d.save(restored, tmp_path / "restored.vmesh")
    assert repacked.read_bytes() == expected


def test_corrupt_n4mc_vmesh_fails_before_loading_model(tmp_path, monkeypatch):
    source = tmp_path / "original.n4d"
    legacy_archive(source)
    artifact = pack_vmesh(source, tmp_path / "original.vmesh")
    data = artifact.read_bytes()
    assert b"opaque shared model" in data
    artifact.write_bytes(data.replace(b"opaque shared model", b"broken shared model"))
    monkeypatch.setattr(_n4mc, "_backend", lambda: pytest.fail("corrupt carriage must not load a model"))
    with pytest.raises(CodecError, match="SHA-256"):
        open4d.load(artifact)


@pytest.mark.torch
@pytest.mark.slow
def test_real_n4mc_mesh_usdc_vmesh_decode_matches_legacy_latents(tmp_path):
    pytest.importorskip("pxr.Usd")
    pytest.importorskip("torch")
    pytest.importorskip("trimesh")
    pytest.importorskip("point_cloud_utils")
    pytest.importorskip("skimage")
    from open4d.codec.tests.test_research_cpu import moving_cube

    with moving_cube() as source:
        usd = open4d.save(source, tmp_path / "source.usdc")
    artifact = open4d.encode(usd, tmp_path / "cube.vmesh", codec="n4mc", device="cpu",
                             resolution=7, epochs=30, hidden_channels=(4, 8),
                             latent_channels=4, learning_rate=3e-3)
    native = unpack_vmesh(artifact, tmp_path / "native")
    info = inspect_vmesh(artifact)
    # Reconstruct the legacy packaging from exactly the same native model and
    # latents, proving the new carrier does not change neural reconstruction.
    legacy = tmp_path / "same-latents.n4d"
    metadata = info["sequence"]
    manifest = {k: v for k, v in metadata.items() if k not in ("version", "native")}
    manifest["schema"] = "open4d.n4mc-sequence/v1"
    with ZipFile(legacy, "w") as archive:
        archive.writestr("manifest.json", json.dumps(manifest))
        for record in info["files"][1:]:
            archive.write(native / record["name"], record["name"])
    with open4d.load(artifact, options={"device": "cpu"}) as decoded, open4d.load(legacy, options={"device": "cpu"}) as original:
        assert len(decoded) == len(original) == 2
        assert decoded.timestamps == original.timestamps == (1.25, 2.75)
        assert decoded.metadata == original.metadata
        assert decoded.metadata["capture"] == "synthetic-moving-cube"
        for left, right in zip(decoded, original):
            assert left.frame_index == right.frame_index
            assert left.metadata == right.metadata
            assert len(left.geometry.positions) and len(left.geometry.triangles)
            np.testing.assert_array_equal(left.geometry.positions, right.geometry.positions)
            np.testing.assert_array_equal(left.geometry.triangles, right.geometry.triangles)
        recovered = open4d.save(decoded, tmp_path / "decoded.usdc")
        with open4d.load(recovered) as reopened:
            assert reopened.timestamps == decoded.timestamps
            for left, right in zip(reopened, decoded):
                np.testing.assert_array_equal(left.geometry.positions, right.geometry.positions)
                np.testing.assert_array_equal(left.geometry.triangles, right.geometry.triangles)
