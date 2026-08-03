import torch

from data.block_sampling import extract_blocks, stitch_blocks


def test_extract_and_stitch_round_trip():
    volume = torch.arange(1 * 5 * 5 * 5, dtype=torch.float32).reshape(1, 5, 5, 5)
    blocks, origins = extract_blocks(volume, block_size=3, stride=2)
    reconstructed = stitch_blocks(blocks, origins, volume.shape)
    assert torch.allclose(reconstructed, volume)
