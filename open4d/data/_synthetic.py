"""Tiny, deterministic MIT samples without captured data or neural weights."""

from __future__ import annotations

import hashlib
import io
import json
import math
import tempfile
from pathlib import Path

import numpy as np

from open4d.core import Frame, GaussianCloud, MemoryFrameProvider, Sequence
from open4d.demo import _LICENSE, mesh_sequence

from ._download import _inventory, cache_root


class _SyntheticDataset:
    license = "MIT"
    license_url = "https://github.com/open4dfoundation/Open4D/blob/main/LICENSE"
    synthetic = True

    def __init__(self, *, cache_dir=None):
        self.cache_dir = cache_root(cache_dir) / self.name

    @property
    def path(self):
        """Materialize synthetic files on first access; verify them on every use."""
        files = self._files()
        expected = {name: hashlib.sha256(data).hexdigest() for name, data in files.items()}
        if not self.cache_dir.exists() and not self.cache_dir.is_symlink():
            self.cache_dir.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.TemporaryDirectory(prefix=".open4d-synthetic-", dir=self.cache_dir.parent) as temp:
                staged = Path(temp) / "dataset"
                staged.mkdir()
                for name, data in files.items():
                    (staged / name).write_bytes(data)
                try:
                    staged.rename(self.cache_dir)
                except OSError:
                    if not self.cache_dir.exists():
                        raise
        if self.cache_dir.is_symlink() or _inventory(self.cache_dir) != expected:
            raise ValueError(f"invalid synthetic dataset cache: {self.cache_dir}")
        return self.cache_dir

    def _metadata(self):
        return {
            "name": self.name, "synthetic": True, "license": self.license,
            "license_url": self.license_url, "fps": 30.0,
            "source": "Open4D deterministic generator; no captured data or trained model",
        }

    def _common_files(self):
        return {"LICENSE": _LICENSE.encode(),
                "provenance.json": json.dumps(self._metadata(), sort_keys=True).encode()}


class SyntheticMeshSequence(_SyntheticDataset):
    """Eight MIT wave-grid OBJ frames, loadable via ``open4d.load(sample.path)``.

    These procedurally generated meshes replace restricted research samples in
    package examples; they are not basketball capture data.
    """

    name = "synthetic-mesh-v1"
    kind = "mesh"

    def _files(self):
        result = self._common_files()
        with mesh_sequence(side=8, frames=8) as sequence:
            for frame in sequence:
                mesh = frame.geometry
                vertices = ["v " + " ".join(f"{float(v):.9g}" for v in point)
                            for point in mesh.positions]
                faces = ["f " + " ".join(str(int(v) + 1) for v in triangle)
                         for triangle in mesh.triangles]
                result[f"frame_{frame.frame_index:04d}.obj"] = (
                    "\n".join(vertices + faces) + "\n").encode()
        return result

    def load(self):
        """Load the verified cached mesh sequence."""
        from open4d.io import open_sequence
        return open_sequence(self.path)


class SyntheticGaussianSequence(_SyntheticDataset):
    """Eight MIT Gaussian-cloud frames, loaded with ``sample.load()``.

    NPZ stores activated scales/opacities, wxyz quaternions and RGB colors.
    This is generated geometry, not a trained neural codec or checkpoint.
    """

    name = "synthetic-gaussian-v1"
    kind = "gaussian"

    def _frames(self):
        angles = np.linspace(0, 2 * math.pi, 16, endpoint=False)
        for index in range(8):
            phase = 2 * math.pi * index / 8
            yield {
                "positions": np.column_stack((np.cos(angles + phase),
                    np.sin(angles + phase), 0.2 * np.sin(2 * angles + phase))).astype(np.float32),
                "scales": np.full((16, 3), 0.08, dtype=np.float32),
                "rotations": np.tile(np.array([1, 0, 0, 0], dtype=np.float32), (16, 1)),
                "opacities": np.full(16, 0.8, dtype=np.float32),
                "colors": np.column_stack(((np.cos(angles) + 1) / 2,
                    (np.sin(angles) + 1) / 2, np.full(16, 0.5))).astype(np.float32),
            }

    def _files(self):
        result = self._common_files()
        for index, values in enumerate(self._frames()):
            buffer = io.BytesIO()
            np.savez(buffer, **values)
            result[f"frame_{index:04d}.npz"] = buffer.getvalue()
        return result

    def load(self):
        """Return the verified cache as a core ``Sequence`` of GaussianClouds."""
        folder = self.path
        frames = []
        for index in range(8):
            with np.load(folder / f"frame_{index:04d}.npz", allow_pickle=False) as values:
                geometry = GaussianCloud(**{key: values[key] for key in values.files})
            frames.append(Frame(index, index / 30, geometry))
        return Sequence(MemoryFrameProvider(frames, metadata=self._metadata()))
