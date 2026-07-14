from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Iterable
import glob

import numpy as np
import torch
from torch.utils.data import Dataset


@dataclass(frozen=True)
class TSDFDatasetConfig:
    root: str
    pattern: str = "data/*.npz"
    tsdf_key: str = "sdf"
    split: str = "train"
    split_ratio: tuple[float, float, float] = (0.8, 0.1, 0.1)
    split_seed: int = 0
    value_range: tuple[float, float] = (-1.0, 1.0)
    range_tolerance: float = 1e-5
    narrow_band_threshold: float = 0.1
    limit: int | None = None


def _to_channel_first(volume: np.ndarray) -> np.ndarray:
    if volume.ndim == 3:
        volume = volume[None, ...]
    elif volume.ndim == 4 and volume.shape[-1] == 1:
        volume = np.moveaxis(volume, -1, 0)
    else:
        raise ValueError(
            "Expected TSDF volume with shape (D, H, W) or (D, H, W, 1), "
            f"received {tuple(volume.shape)}."
        )
    return np.ascontiguousarray(volume.astype(np.float32, copy=False))


def _stable_split(
    paths: list[Path],
    split: str,
    split_ratio: tuple[float, float, float],
    seed: int,
) -> list[Path]:
    if split not in {"train", "val", "test"}:
        raise ValueError(f"Unsupported split '{split}'.")
    ratios = np.asarray(split_ratio, dtype=np.float64)
    if ratios.shape != (3,) or np.any(ratios < 0) or ratios.sum() <= 0:
        raise ValueError(f"Invalid split ratios: {split_ratio}")

    rng = np.random.default_rng(seed)
    permuted = list(paths)
    rng.shuffle(permuted)
    normalized = ratios / ratios.sum()

    total = len(permuted)
    train_end = int(round(total * normalized[0]))
    val_end = train_end + int(round(total * normalized[1]))
    train_end = min(train_end, total)
    val_end = min(val_end, total)

    partitions = {
        "train": permuted[:train_end],
        "val": permuted[train_end:val_end],
        "test": permuted[val_end:],
    }
    return sorted(partitions[split])


def discover_npz_paths(root: str | Path, pattern: str) -> list[Path]:
    search_root = Path(root)
    matched = [Path(path) for path in glob.glob(str(search_root / pattern))]
    return sorted(path for path in matched if path.is_file())


class TSDFVolumeDataset(Dataset):
    def __init__(self, config: TSDFDatasetConfig):
        self.config = config
        all_paths = discover_npz_paths(config.root, config.pattern)
        if not all_paths:
            raise FileNotFoundError(
                f"No .npz files matched pattern '{config.pattern}' under '{config.root}'."
            )

        split_paths = _stable_split(
            paths=all_paths,
            split=config.split,
            split_ratio=config.split_ratio,
            seed=config.split_seed,
        )
        if config.limit is not None:
            split_paths = split_paths[: config.limit]
        if not split_paths:
            raise ValueError(f"Split '{config.split}' is empty for pattern '{config.pattern}'.")
        self.paths = split_paths

    @classmethod
    def from_mapping(cls, mapping: dict, split: str) -> "TSDFVolumeDataset":
        dataset_cfg = TSDFDatasetConfig(
            root=mapping["root"],
            pattern=mapping.get("pattern", "data/*.npz"),
            tsdf_key=mapping.get("tsdf_key", "sdf"),
            split=split,
            split_ratio=tuple(mapping.get("split_ratio", (0.8, 0.1, 0.1))),
            split_seed=int(mapping.get("split_seed", 0)),
            value_range=tuple(mapping.get("value_range", (-1.0, 1.0))),
            range_tolerance=float(mapping.get("range_tolerance", 1e-5)),
            narrow_band_threshold=float(mapping.get("narrow_band_threshold", 0.1)),
            limit=mapping.get("limit"),
        )
        return cls(dataset_cfg)

    def __len__(self) -> int:
        return len(self.paths)

    def _load_npz(self, path: Path) -> dict:
        data = np.load(path)
        if self.config.tsdf_key not in data:
            raise KeyError(f"'{self.config.tsdf_key}' not found in {path}")

        tsdf = _to_channel_first(data[self.config.tsdf_key])
        min_allowed, max_allowed = self.config.value_range
        min_value = float(tsdf.min())
        max_value = float(tsdf.max())
        tol = self.config.range_tolerance
        if min_value < min_allowed - tol or max_value > max_allowed + tol:
            raise ValueError(
                f"TSDF values in {path} out of expected range {self.config.value_range}: "
                f"min={min_value:.6f}, max={max_value:.6f}"
            )

        narrow_band_ratio = float(np.mean(np.abs(tsdf) <= self.config.narrow_band_threshold))
        frame_id = path.stem
        return {
            "tsdf": torch.from_numpy(tsdf),
            "path": str(path),
            "frame_id": frame_id,
            "shape": torch.tensor(tsdf.shape[1:], dtype=torch.long),
            "min_value": min_value,
            "max_value": max_value,
            "narrow_band_ratio": narrow_band_ratio,
        }

    def __getitem__(self, index: int) -> dict:
        sample = self._load_npz(self.paths[index])
        sample["index"] = torch.tensor(index, dtype=torch.long)
        return sample


def summarize_dataset(dataset: Iterable[dict]) -> dict[str, float]:
    mins = []
    maxs = []
    narrow_band = []
    for sample in dataset:
        mins.append(float(sample["min_value"]))
        maxs.append(float(sample["max_value"]))
        narrow_band.append(float(sample["narrow_band_ratio"]))
    return {
        "num_samples": float(len(mins)),
        "global_min": min(mins),
        "global_max": max(maxs),
        "mean_narrow_band_ratio": float(np.mean(narrow_band)),
    }
