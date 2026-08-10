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


def _corner(token: str) -> int:
    """OBJ indices are 1-based and may be `v`, `v/vt`, `v//vn`, or `v/vt/vn`."""
    return int(token.split("/", 1)[0]) - 1


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

    positions: list[tuple[float, float, float]] = []
    corners: list[tuple[int, int, int]] = []
    with open(path, "r", encoding="utf-8", errors="replace") as stream:
        for line in stream:
            if line.startswith("v "):
                x, y, z = line.split()[1:4]
                positions.append((float(x), float(y), float(z)))
            elif line.startswith("f "):
                indices = [_corner(token) for token in line.split()[1:]]
                for i in range(1, len(indices) - 1):
                    corners.append((indices[0], indices[i], indices[i + 1]))

    verts = torch.tensor(positions, dtype=dtype, device=device)
    faces = torch.tensor(corners, dtype=torch.int64, device=device)
    if corners and int(faces.max()) >= len(positions):
        raise ValueError(
            f"{path} references vertex {int(faces.max()) + 1} but declares "
            f"{len(positions)}"
        )
    return verts, Faces(verts_idx=faces), Properties()


def save_obj(path: str | Path, verts: torch.Tensor, faces: torch.Tensor) -> None:
    """Write vertices and triangles as an OBJ, with 1-based indices."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    v = verts.detach().cpu().numpy()
    f = faces.detach().cpu().numpy() + 1
    with open(path, "w", encoding="utf-8") as stream:
        for x, y, z in v:
            stream.write(f"v {x:f} {y:f} {z:f}\n")
        for a, b, c in f:
            stream.write(f"f {a} {b} {c}\n")
