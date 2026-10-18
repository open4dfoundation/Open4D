"""Lazy, licensed example data, a versioned asset catalog and verified downloads.

No research datasets or trained weights are bundled. ``get(id)`` returns a
catalog asset whose ``download()`` fetches checksum-pinned files (or tells
you how to acquire restricted data yourself) and whose ``load()`` opens them
with a format-aware loader. Each record keeps data, weights and code licenses
separate; a license label is information, not a grant of rights. The built-in
synthetic samples are deterministic and MIT licensed. ``DatasetSpec`` still
describes one ad hoc external download.
"""

from ._assets import (Asset, AssetPair, LicenseNotAccepted, ManualAcquisitionRequired,
                      catalog, get)
from ._catalog import CatalogError, hugging_face_revision, validate_catalog
from ._download import DatasetSpec, DownloadableDataset
from ._loaders import GaussianSplatSequence, LossyConversionWarning, read_gaussian_ply
from ._synthetic import SyntheticGaussianSequence, SyntheticMeshSequence

__all__ = ["Asset", "AssetPair", "CatalogError", "DatasetSpec", "DownloadableDataset",
           "GaussianSplatSequence", "LicenseNotAccepted", "LossyConversionWarning",
           "ManualAcquisitionRequired", "SyntheticGaussianSequence", "SyntheticMeshSequence",
           "catalog", "get", "hugging_face_revision", "read_gaussian_ply", "validate_catalog"]
