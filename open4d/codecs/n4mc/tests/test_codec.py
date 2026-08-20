import torch

from models.tsdf_autoencoder import TSDFCompressionAutoencoder


def test_codec_forward_shapes():
    model = TSDFCompressionAutoencoder(
        hidden_channels=(8, 16, 32),
        latent_channels=64,
        embed_hwd=4,
    )
    sample = torch.randn(2, 1, 33, 35, 31)
    outputs = model(sample)

    assert outputs["reconstruction"].shape == sample.shape
    assert tuple(outputs["quantized_latent"].shape) == (2, 64, 4, 4, 4)
    assert tuple(outputs["bottleneck_shape"].tolist()) == (5, 5, 4)
    assert float(outputs["rate_bpv"].item()) > 0.0


def test_codec_decode_from_saved_quantized_latent(tmp_path):
    model = TSDFCompressionAutoencoder(
        hidden_channels=(8, 16, 32),
        latent_channels=32,
        embed_hwd=4,
    )
    sample = torch.randn(1, 1, 33, 35, 31)
    encoded = model.encode(sample)
    artifact = tmp_path / "frame.pt"
    torch.save({
        "model": model.state_dict(),
        "quantized_latent": encoded["quantized_latent"],
        "bottleneck_shape": encoded["bottleneck_shape"],
        "original_shape": encoded["original_shape"],
    }, artifact)
    saved = torch.load(artifact, weights_only=True)
    fresh_model = TSDFCompressionAutoencoder(
        hidden_channels=(8, 16, 32), latent_channels=32, embed_hwd=4,
    )
    fresh_model.load_state_dict(saved["model"])
    reconstruction = fresh_model.decode_quantized_latent(
        saved["quantized_latent"],
        bottleneck_shape=saved["bottleneck_shape"],
        original_shape=saved["original_shape"],
    )

    assert tuple(reconstruction.shape) == tuple(sample.shape)
