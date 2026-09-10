"""OBJ reading and writing on torch tensors, replacing `pytorch3d.io`.

The signatures mirror the PyTorch3D functions the codecs called, including the
`(verts, faces, aux)` triple whose middle element is addressed as
`faces.verts_idx`, so the call sites needed no reshaping.

Only the geometry PyTorch3D's loader was asked for is read. Every Open4D call
site passed `load_textures=False`, so materials and texture coordinates are
parsed no further than skipping their lines; the argument is accepted and
ignored to keep those calls working, and asking for textures is refused rather
than silently returning nothing.
"""

from __future__ import annotations

from pathlib import Path
from typing import NamedTuple

import torch
import numpy as np

from open4d.io._mesh import read_obj, write_obj

__all__ = ["Faces", "load_obj", "save_obj"]


class Faces(NamedTuple):
    """The face index tuple PyTorch3D returns; Open4D only reads `verts_idx`."""

    verts_idx: torch.Tensor
    normals_idx: torch.Tensor | None = None
    textures_idx: torch.Tensor | None = None


class Properties(NamedTuple):
    """Stand-in for PyTorch3D's `aux`, which no Open4D call site inspects."""

    normals: torch.Tensor | None = None
    verts_uvs: torch.Tensor | None = None


def load_obj(
    path: str | Path,
    load_textures: bool = False,
    device: str | torch.device = "cpu",
    dtype: torch.dtype = torch.float32,
):
    """Read an OBJ, returning `(verts, faces, aux)` as PyTorch3D did.

    Polygons with more than three corners are triangulated as a fan, which is
    what PyTorch3D does and is correct for the convex faces these meshes hold.
    """
    if load_textures:
        raise NotImplementedError(
            "open4d.torch_ops.io.load_obj reads geometry only; no Open4D call "
            "site loads textures. Use trimesh if you need materials."
        )

    if not torch.empty((), dtype=dtype).is_floating_point():
        raise TypeError("vertex dtype must be floating point")
    positions, corners, _ = read_obj(Path(path), dtype=np.float64)
    verts = torch.as_tensor(positions, dtype=dtype, device=device)
    if not torch.isfinite(verts).all():
        raise ValueError("vertex coordinates exceed the requested dtype")
    faces = torch.as_tensor(corners.astype(np.int64), device=device)
    return verts, Faces(verts_idx=faces), Properties()


def save_obj(path: str | Path, verts: torch.Tensor, faces: torch.Tensor) -> None:
    """Write vertices and triangles as an OBJ, with 1-based indices."""
    from .mesh import _validate_mesh

    _validate_mesh(verts, faces)
    write_obj(Path(path), verts.detach().to(dtype=torch.float64, device="cpu").numpy(),
              faces.detach().cpu().numpy())
