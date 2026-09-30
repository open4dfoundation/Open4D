from types import SimpleNamespace

import numpy as np
import pytest

torch = pytest.importorskip("torch")
pytest.importorskip("zstd")
pytest.importorskip("trimesh")

from open4d.codec._research import research_module


def test_compression_quantizes_the_variance_of_each_coefficient_column(tmp_path, monkeypatch):
    backend = research_module("klt.klt")
    source = tmp_path / "tsdf"
    source.mkdir()
    grid = np.indices((4, 4, 4), dtype=np.float32)
    volume = grid[0] * .2 + grid[1] ** 2 * .03 + grid[2] ** 3 * .01 - .5
    np.savez_compressed(source / "000000.npz", sdf=volume[..., None])
    quantize = backend.quantize_coeffs
    calls = []

    def check_variances(coefficients, variances, **options):
        torch.testing.assert_close(variances, coefficients.var(dim=0, unbiased=True))
        calls.append(variances)
        return quantize(coefficients, variances, **options)

    monkeypatch.setattr(backend, "quantize_coeffs", check_variances)
    backend.run_compression(SimpleNamespace(
        input_path=str(source), output_path=str(tmp_path / "encoded"),
        num_frames=1, training_frames=[0], block_size=2, num_components=2,
        k_total=16, fps=30,
    ), verify_decode=False)
    assert len(calls) == 1


def test_zero_variance_column_does_not_erase_a_changing_coefficient():
    backend = research_module("klt.klt")
    coefficients = torch.tensor([[-2., 0.], [-1., 0.], [1., 0.], [2., 0.]])
    indices, centers, fixed = backend.quantize_coeffs(
        coefficients, coefficients.var(dim=0, unbiased=True), K_total=16
    )
    reconstructed = backend.decompress_coeffs(indices, centers, fixed, H=2)
    assert fixed == {1: 0.}
    torch.testing.assert_close(reconstructed, coefficients)


def test_constant_coefficients_round_trip_without_quantization_bins():
    backend = research_module("klt.klt")
    coefficients = torch.tensor([[2., -3.], [2., -3.]])
    indices, centers, fixed = backend.quantize_coeffs(
        coefficients, coefficients.var(dim=0, unbiased=True), K_total=16
    )
    reconstructed = backend.decompress_coeffs(indices, centers, fixed, H=2)
    assert fixed == {0: 2., 1: -3.}
    torch.testing.assert_close(reconstructed, coefficients)


def test_single_block_compression_round_trip(tmp_path, monkeypatch):
    backend = research_module("klt.klt")
    source = tmp_path / "tsdf"
    source.mkdir()
    volume = np.linspace(-1., 1., 64, dtype=np.float32).reshape(4, 4, 4)
    np.savez_compressed(source / "000000.npz", sdf=volume[..., None])
    quantize = backend.quantize_coeffs

    def check_single_block(coefficients, variances, **options):
        assert torch.isfinite(variances).all()
        torch.testing.assert_close(variances, torch.zeros_like(variances))
        return quantize(coefficients, variances, **options)

    monkeypatch.setattr(backend, "quantize_coeffs", check_single_block)
    backend.run_compression(SimpleNamespace(
        input_path=str(source), output_path=str(tmp_path / "encoded"),
        num_frames=1, training_frames=[0], block_size=4, num_components=1,
        k_total=16, fps=30,
    ), verify_decode=False)
    folder = tmp_path / "encoded/compressed"
    decoder = torch.load(folder / "decoder_context.pt", weights_only=True)
    indices, centers, fixed, shape = backend.load_quantized_coeffs(
        str(folder / "000000_quantized"), "cpu"
    )
    coefficients = backend.decompress_coeffs(indices, centers, fixed, H=1)
    blocks = backend.reconstruct_blocks_torch(
        coefficients, decoder["basis"], decoder["mean"], 1
    )
    reconstructed = backend.reconstruct_volume_from_blocks_torch(blocks, shape, 4)
    torch.testing.assert_close(reconstructed, torch.from_numpy(volume))
