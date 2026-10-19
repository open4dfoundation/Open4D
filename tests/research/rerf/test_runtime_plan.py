import json
from pathlib import Path

import pytest

from rerf_stream import env
from rerf_stream.bitstream import BitstreamError, validate_bitstream


def toolkit(tmp_path, version):
    root = tmp_path / f"cuda-{version}"
    (root / "bin").mkdir(parents=True)
    (root / "bin/nvcc").touch()
    return env.Toolkit(root, tuple(map(int, version.split("."))), "fixture")


def test_matching_torch_toolkit_wins_over_shell_default(tmp_path):
    incompatible = toolkit(tmp_path, "12.4")
    matching = toolkit(tmp_path, "11.8")
    assert env.select_toolkit((11, 8), [incompatible, matching]) == matching
    with pytest.raises(RuntimeError, match="refuse"):
        env.select_toolkit((11, 8), [incompatible], explicit=incompatible.home)


def test_architecture_comes_from_device_and_toolkit():
    assert env.select_arch_list((8, 9), (11, 8), {}) == "8.9"
    assert env.select_arch_list((8, 9), (11, 7), {}) == "8.6+PTX"
    assert env.select_arch_list((8, 6), (11, 8), {}) == "8.6"
    assert env.platform_problems(system="Darwin", machine="arm64", version=(3, 13))


def stream(tmp_path):
    for name in ("model_kwargs.json", "rgb_net.tar", "mask_0.rerf", "mask_1.rerf",
                 "feature_0_8.rerf0", "feature_0_8.rerf1", "feature_1_8.rerf0",
                 "feature_1_8.rerf1", "deform_1.npy", "deform_mask_1.rerf"):
        (tmp_path / name).write_bytes(b"opaque")
    for frame in (0, 1):
        (tmp_path / f"header_{frame}.json").write_text(json.dumps({"headers": [{"quality": 8, "size": [1, 2, 8, 8, 8]}]}))


def test_strict_stream_refuses_motion_and_channel_fallbacks(tmp_path):
    stream(tmp_path)
    validate_bitstream(tmp_path, [0, 1], group_size=2, pca=False, n_channel=2)
    (tmp_path / "deform_1.npy").unlink()
    with pytest.raises(BitstreamError, match="deform_1"):
        validate_bitstream(tmp_path, [0, 1], group_size=2, pca=False, n_channel=2)
    (tmp_path / "deform_1.npy").write_bytes(b"opaque")
    (tmp_path / "feature_1_8.rerf1").unlink()
    (tmp_path / "feature_1_8.rerf").write_bytes(b"not a per-channel payload")
    with pytest.raises(BitstreamError, match="feature_1"):
        validate_bitstream(tmp_path, [0, 1], group_size=2, pca=False, n_channel=2)


@pytest.mark.parametrize("frames", [[1], [0, 0], [1, 0], [0, 2]])
def test_strict_stream_refuses_incomplete_temporal_history(tmp_path, frames):
    stream(tmp_path)
    with pytest.raises(BitstreamError):
        validate_bitstream(tmp_path, frames, group_size=2, pca=False, n_channel=2)
