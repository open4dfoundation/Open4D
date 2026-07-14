import numpy as np

from data.dataset import TSDFVolumeDataset


def test_dataset_loads_channel_first_npz(tmp_path):
    root = tmp_path / "dataset"
    data_dir = root / "data"
    data_dir.mkdir(parents=True)
    tsdf = np.zeros((9, 9, 9, 1), dtype=np.float32)
    np.savez_compressed(data_dir / "0000.npz", sdf=tsdf)
    np.savez_compressed(data_dir / "0001.npz", sdf=tsdf)
    np.savez_compressed(data_dir / "0002.npz", sdf=tsdf)

    dataset = TSDFVolumeDataset.from_mapping(
        {
            "root": str(root),
            "pattern": "data/*.npz",
            "split_ratio": [0.34, 0.33, 0.33],
            "split_seed": 0,
            "narrow_band_threshold": 0.1,
        },
        split="train",
    )

    sample = dataset[0]
    assert tuple(sample["tsdf"].shape) == (1, 9, 9, 9)
    assert sample["path"].endswith(".npz")
