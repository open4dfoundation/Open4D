import numpy as np
import pytest


def test_native_non_utf8_diagnostic_is_a_codec_error():
    import sys
    from open4d.codec import CodecError, _vmesh

    with pytest.raises(CodecError, match="exited 2"):
        _vmesh._run([sys.executable, "-c", "import os; os.write(2, b'bad\\xff'); exit(2)"], "native")


def test_native_timeout_is_bounded_and_explained(monkeypatch):
    import sys
    from open4d.codec import CodecError, _vmesh

    monkeypatch.setenv("OPEN4D_NATIVE_TIMEOUT", "0.1")
    with pytest.raises(CodecError, match="timed out"):
        _vmesh._run([sys.executable, "-c", "import time; time.sleep(1)"], "native")


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
