import numpy as np
import pytest


def test_native_non_utf8_diagnostic_is_a_codec_error():
    import sys
    from open4d.codec import CodecError, _o4d

    with pytest.raises(CodecError, match="exited 2"):
        _o4d._run([sys.executable, "-c", "import os; os.write(2, b'bad\\xff'); exit(2)"], "native")


def test_native_timeout_is_bounded_and_explained(monkeypatch):
    import sys
    from open4d.codec import CodecError, _o4d

    monkeypatch.setenv("OPEN4D_NATIVE_TIMEOUT", "0.1")
    with pytest.raises(CodecError, match="timed out"):
        _o4d._run([sys.executable, "-c", "import time; time.sleep(1)"], "native")


def test_native_timeout_is_opt_in(monkeypatch):
    from open4d.codec import _native

    monkeypatch.delenv("OPEN4D_NATIVE_TIMEOUT", raising=False)
    assert _native._timeout() is None


@pytest.mark.parametrize("value", ["soon", "0", "-5", "inf", "nan"])
def test_invalid_native_timeout_is_a_codec_error(monkeypatch, value):
    import sys
    from open4d.codec import CodecError, _o4d

    monkeypatch.setenv("OPEN4D_NATIVE_TIMEOUT", value)
    with pytest.raises(CodecError, match="OPEN4D_NATIVE_TIMEOUT"):
        _o4d._run([sys.executable, "-c", "pass"], "native")


@pytest.mark.parametrize("identifier", ["npz", "rle"])
def test_array_codec_refuses_to_write_arrays_it_cannot_read(tmp_path, monkeypatch, identifier):
    from open4d import Frame, MemoryFrameProvider, Sequence, TriangleMesh
    from open4d.codec import CodecError, _npz

    codec = {c.id: c for c in _npz.REFERENCE_CODECS}[identifier]
    mesh = TriangleMesh(np.random.default_rng(0).random((64, 3)), [[0, 1, 2]])
    source = Sequence(MemoryFrameProvider([Frame(0, 0., mesh)]))
    monkeypatch.setattr(_npz, "_MAX_ARRAY_BYTES", 512)
    destination = tmp_path / "large.o4d"
    with pytest.raises(CodecError, match="limit"):
        codec.encode(source, destination)
    assert not destination.exists()
    monkeypatch.setattr(_npz, "_MAX_ARRAY_BYTES", 4096)
    monkeypatch.setattr(_npz, "_MAX_RLE_MEMBER_BYTES", 2 * 4096 + 8 + 4096)
    with codec.decode(codec.encode(source, destination)) as decoded:
        np.testing.assert_array_equal(decoded[0].geometry.positions, mesh.positions)


def test_n4mc_reconstruction_uses_tsdf_sampling_extent():
    pytest.importorskip("torch")
    pytest.importorskip("skimage")
    pytest.importorskip("trimesh")
    from open4d.codec import _n4mc

    axis = np.linspace(-1.1, 1.1, 48)
    grid = np.stack(np.meshgrid(axis, axis, axis, indexing="ij"), axis=-1)
    volume = np.linalg.norm(grid, axis=-1) - 0.8
    mesh = _n4mc._reconstruct_mesh(volume, _n4mc._backend()[3])
    radii = np.linalg.norm(mesh.vertices, axis=1)
    np.testing.assert_allclose(radii, 0.8, atol=0.002)


def test_n4mc_ssim_weight_changes_loss_and_gradients():
    torch = pytest.importorskip("torch")
    from open4d.codec._research import research_module

    losses = research_module("n4mc.losses")
    prediction = torch.full((1, 1, 8, 8, 8), 0.5, requires_grad=True)
    outputs = {"reconstruction": prediction, "rate_bpv": torch.tensor(0.)}
    config = dict(lambda_rec=0., lambda_rate=0., lambda_band=0., lambda_sign=0.)
    zero, _ = losses.compute_rd_loss(outputs, torch.zeros_like(prediction), {**config, "lambda_ssim": 0.})
    weighted, terms = losses.compute_rd_loss(outputs, torch.zeros_like(prediction), {**config, "lambda_ssim": 0.5})
    assert weighted > zero
    assert weighted.item() == pytest.approx(0.5 * terms["ssim_loss"].item())
    weighted.backward()
    assert prediction.grad.abs().sum() > 0
