"""Lazy, licensed example data and checksum-verified external downloads.

No research datasets or trained weights are bundled. The built-in examples
are deterministic synthetic data under Open4D's MIT license. Use an explicit
``DatasetSpec`` for external assets; its license describes the data, not the
downloader. Model/checkpoint assets require their own separate specification.
"""

from ._download import DatasetSpec, DownloadableDataset
from ._synthetic import SyntheticGaussianSequence, SyntheticMeshSequence

__all__ = ["DatasetSpec", "DownloadableDataset", "SyntheticMeshSequence",
           "SyntheticGaussianSequence"]
