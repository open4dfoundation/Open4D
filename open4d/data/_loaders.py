"""Format-aware loaders for catalog assets.

Gaussian sequences load as :class:`GaussianSplatSequence`, whose frames are
``GaussianSplats`` with every stored spherical-harmonic coefficient. Converting
to the core ``GaussianCloud`` sequence keeps only the view-independent colour,
so :meth:`GaussianSplatSequence.to_sequence` reports what it would discard and
refuses unless the caller passes ``accept_loss=True``.
"""

from __future__ import annotations

import collections.abc
import re
import warnings
from pathlib import Path

import numpy as np

from open4d.core import Frame, GaussianCloud, MemoryFrameProvider, Sequence
from open4d.gaussians import GaussianSplats, load_gaussians, splats_from_ply_vertices

#: Degree-0 SH basis constant used by Graphdeco renderers: rgb = 0.5 + C0 * dc.
SH_C0 = 0.28209479177387814
_PLY_TYPES = {"char": "i1", "int8": "i1", "uchar": "u1", "uint8": "u1",
              "short": "<i2", "int16": "<i2", "ushort": "<u2", "uint16": "<u2",
              "int": "<i4", "int32": "<i4", "uint": "<u4", "uint32": "<u4",
              "float": "<f4", "float32": "<f4", "double": "<f8", "float64": "<f8"}


class LossyConversionWarning(UserWarning):
    """Data was dropped or altered converting between representations."""


def read_gaussian_ply(path):
    """Read a Graphdeco Gaussian PLY; binary little-endian needs only NumPy.

    Other PLY encodings fall back to ``open4d.load_gaussians`` (``plyfile``).
    """
    path = Path(path)
    with path.open("rb") as stream:
        head = stream.read(65536)
    end = head.find(b"end_header\n")
    if not head.startswith(b"ply\n") or end < 0:
        return load_gaussians(path)
    lines = head[:end].decode("ascii", "replace").splitlines()[1:]
    fields, count, element, simple = [], None, None, True
    for line in lines:
        words = line.split()
        if not words or words[0] in ("comment", "obj_info"):
            continue
        if words[0] == "format":
            simple &= words[1:2] == ["binary_little_endian"]
        elif words[0] == "element":
            element = words[1]
            if element == "vertex" and count is None and not fields:
                count = int(words[2])
            else:
                simple = False  # extra elements: let plyfile handle the layout
        elif words[0] == "property" and element == "vertex":
            if words[1] == "list" or words[1] not in _PLY_TYPES:
                simple = False
            else:
                fields.append((words[2], _PLY_TYPES[words[1]]))
    if not simple or count is None:
        return load_gaussians(path)
    dtype = np.dtype(fields)
    offset = end + len(b"end_header\n")
    if path.stat().st_size - offset < dtype.itemsize * count:
        raise ValueError(f"Gaussian PLY is truncated: {path}")
    vertex = np.fromfile(path, dtype=dtype, count=count, offset=offset)
    return splats_from_ply_vertices(vertex)


def _rgb(frame):
    return 0.5 + SH_C0 * frame.spherical_harmonics[:, 0, :].astype(np.float64)


class GaussianSplatSequence(collections.abc.Sequence):
    """Lazily read per-frame Gaussian PLY files as ``GaussianSplats``."""

    def __init__(self, paths, *, fps, metadata=None):
        self.paths = tuple(Path(path) for path in paths)
        if not self.paths:
            raise ValueError("a Gaussian sequence needs at least one frame")
        self.fps = None if fps is None else float(fps)
        self.metadata = dict(metadata or {})

    def __len__(self):
        return len(self.paths)

    def __getitem__(self, index):
        if isinstance(index, slice):
            return [self[i] for i in range(*index.indices(len(self)))]
        return read_gaussian_ply(self.paths[range(len(self))[index]])

    @property
    def timestamps(self):
        rate = self.fps or 30.0
        return tuple(index / rate for index in range(len(self)))

    def conversion_loss(self, frames=None):
        """Describe what ``to_sequence`` would discard; empty means lossless."""
        frames = list(self) if frames is None else frames
        losses = []
        degree = max(frame.sh_degree for frame in frames)
        if degree:
            losses.append(f"view-dependent spherical harmonics (degree {degree}, "
                          f"{(degree + 1) ** 2 - 1} coefficients per channel) are dropped; "
                          "colors keep only the degree-0 term as 0.5 + C0 * f_dc")
        clipped = sum(int(np.count_nonzero((_rgb(frame) < 0) | (_rgb(frame) > 1)))
                      for frame in frames)
        if clipped:
            losses.append(f"{clipped} color channels outside [0, 1] are clipped")
        return losses

    def to_sequence(self, *, accept_loss=False):
        """Return a core ``Sequence`` of ``GaussianCloud`` frames.

        Raises ``ValueError`` listing the losses unless ``accept_loss=True``;
        accepted losses are warned about and recorded in
        ``metadata["lossy_conversion"]``.
        """
        frames = list(self)
        losses = self.conversion_loss(frames)
        if losses and not accept_loss:
            raise ValueError("converting GaussianSplats to GaussianCloud loses data: "
                             + "; ".join(losses) + ". Pass accept_loss=True to proceed, "
                             "or keep the GaussianSplats frames")
        if losses:
            warnings.warn("GaussianCloud conversion: " + "; ".join(losses),
                          LossyConversionWarning, stacklevel=2)
        result = []
        for index, (frame, timestamp) in enumerate(zip(frames, self.timestamps)):
            cloud = GaussianCloud(positions=frame.positions, scales=frame.scales,
                                  rotations=frame.rotations, opacities=frame.opacities,
                                  colors=np.clip(_rgb(frame), 0.0, 1.0))
            result.append(Frame(index, timestamp, cloud))
        metadata = {**self.metadata, "fps": self.fps, "lossy_conversion": losses,
                    "converted_from": "GaussianSplats"}
        return Sequence(MemoryFrameProvider(result, metadata=metadata))


def _numbered(paths):
    def key(path):
        digits = re.findall(r"\d+", path.stem)
        return (int(digits[-1]) if digits else -1, path.name)
    return sorted(paths, key=key)


def _single_root(root):
    """Descend through a lone top-level archive folder."""
    entries = [path for path in root.iterdir() if not path.name.startswith(".")]
    if len(entries) == 1 and entries[0].is_dir():
        return entries[0]
    return root


def _queen_frames(root):
    """Dense PLY frames of a QUEEN run, mirroring ``GaussianRun.frame_paths``."""
    folders = [path for path in root.rglob("frames") if path.is_dir()]
    if len(folders) != 1:
        raise FileNotFoundError(f"expected one QUEEN 'frames' folder under {root}, "
                                f"found {len(folders)}")
    result = []
    for frame in _numbered(path for path in folders[0].iterdir()
                           if path.is_dir() and path.name.isdigit()):
        canonical = frame / "point_cloud.ply"
        if canonical.is_file():
            result.append(canonical)
            continue
        snapshots = [path for path in (frame / "point_cloud").glob("iteration_*")
                     if path.name[10:].isdigit() and (path / "point_cloud.ply").is_file()]
        if not snapshots:
            raise FileNotFoundError(f"no dense Gaussian PLY saved for {frame}")
        result.append(max(snapshots, key=lambda path: int(path.name[10:])) / "point_cloud.ply")
    return result


def load_content(record, root):
    """Open verified asset content with the loader its ``format`` names."""
    root = Path(root)
    metadata = record.get("metadata", {})
    fps = metadata.get("fps")
    info = {"asset": f"{record['id']}@{record['version']}", "license": {
        role: value["id"] for role, value in record.get("licenses", {}).items()},
        **{key: value for key, value in metadata.items() if value is not None}}
    form = record.get("format")
    if form == "obj-sequence":
        from open4d.io import open_sequence
        frames = _numbered(_single_root(root).glob("*.obj"))
        if not frames:
            raise FileNotFoundError(f"no OBJ frames under {root}")
        return open_sequence(frames[0].parent, format="obj", fps=fps)
    if form == "mesh-directory":
        from open4d.io import open_sequence
        return open_sequence(_single_root(root), fps=fps)
    if form == "ply-gaussian-sequence":
        frames = _numbered(_single_root(root).glob("*.ply"))
        if not frames:
            raise FileNotFoundError(f"no Gaussian PLY frames under {root}")
        return GaussianSplatSequence(frames, fps=fps, metadata=info)
    if form == "queen-frames":
        return GaussianSplatSequence(_queen_frames(root), fps=fps, metadata=info)
    raise NotImplementedError(
        f"{record['id']} is {form or record['kind']} data with no geometry loader; "
        f"use its files at {root} directly (for example with open4d.reconstruct)")


__all__ = ["GaussianSplatSequence", "GaussianSplats", "LossyConversionWarning",
           "SH_C0", "load_content", "read_gaussian_ply"]
