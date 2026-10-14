"""The 3DGS PLY interchange format, read and written.

Every Gaussian-splatting tool in the field reads the PLY that INRIA's original
3DGS writes, so it is the one format that lets a viewer -- SIBR, SuperSplat,
the bundled WebGL one in ``streamer.client`` -- open output from a method that
has never heard of it. That is what this module is for: the baselines in
``open4d/reconstruction`` each store Gaussians in their own container (Vega
ships ``frame_XXXX.pt`` chunks whose colour lives in a hash grid), and
normalising them to this PLY is what makes them viewable at all.

The layout, from ``GaussianModel.construct_list_of_attributes``, is a
``binary_little_endian`` vertex element whose float32 properties are, in order::

    x y z  nx ny nz  f_dc_0..2  f_rest_0..N  opacity  scale_0..2  rot_0..3

Three things about it are easy to get wrong, and all three are decisions this
module makes once:

* **The values are raw, not activated.** ``opacity`` is a logit, ``scale`` is a
  log, ``rot`` is an unnormalised quaternion. A viewer applies
  sigmoid/exp/normalize itself. Writing activated values produces a file that
  loads and renders as fog.
* **Colour is a spherical-harmonic coefficient, not RGB.** ``f_dc`` is the
  degree-0 band, so ``rgb = 0.5 + C0 * f_dc``; :func:`rgb_to_sh_dc` is the
  inverse. Storing RGB directly shifts and rescales every colour.
* **``f_rest`` is channel-major.** Upstream flattens
  ``features_rest.transpose(1, 2)``, so the index is ``channel * n_bands +
  band``, not the band-major order the name suggests. Only matters above
  degree 0, which is why writing ``sh_rest=None`` is both allowed and the
  common case here: a baked colour has no view-dependent bands to carry.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any

import numpy as np

#: The degree-0 spherical-harmonic constant, ``1 / (2 * sqrt(pi))``.
SH_C0 = 0.28209479177387814

_HEADER_MAGIC = b"ply"


def rgb_to_sh_dc(rgb: np.ndarray) -> np.ndarray:
    """Linear RGB in [0, 1] -> the ``f_dc`` coefficients that decode back to it."""
    return (np.asarray(rgb, dtype=np.float32) - 0.5) / SH_C0


def sh_dc_to_rgb(f_dc: np.ndarray) -> np.ndarray:
    """``f_dc`` -> linear RGB, the inverse of :func:`rgb_to_sh_dc`."""
    return 0.5 + SH_C0 * np.asarray(f_dc, dtype=np.float32)


def attribute_names(n_rest: int = 0) -> list[str]:
    """The property names, in file order, for ``n_rest`` per-channel SH bands."""
    names = ["x", "y", "z", "nx", "ny", "nz", "f_dc_0", "f_dc_1", "f_dc_2"]
    names += [f"f_rest_{i}" for i in range(3 * n_rest)]
    names += ["opacity", "scale_0", "scale_1", "scale_2"]
    names += [f"rot_{i}" for i in range(4)]
    return names


def _as_2d(array, name: str, width: int) -> np.ndarray:
    values = np.asarray(array, dtype=np.float32)
    if values.ndim == 1 and width == 1:
        values = values[:, None]
    if values.ndim != 2 or values.shape[1] != width:
        raise ValueError(f"{name}: expected (N, {width}), got {tuple(values.shape)}")
    return values


def write(
    path: Path | str,
    *,
    xyz,
    scale_raw,
    rot_raw,
    opacity_raw,
    sh_dc,
    sh_rest=None,
    normals=None,
) -> Path:
    """Write one 3DGS PLY. Values are raw (pre-activation); see the module docstring.

    ``sh_dc`` is ``(N, 3)`` degree-0 coefficients -- pass
    ``rgb_to_sh_dc(rgb)`` if what you have is colour. ``sh_rest`` is
    ``(N, n_bands, 3)`` and may be ``None``.
    """
    path = Path(path)
    xyz = _as_2d(xyz, "xyz", 3)
    count = xyz.shape[0]
    scale_raw = _as_2d(scale_raw, "scale_raw", 3)
    rot_raw = _as_2d(rot_raw, "rot_raw", 4)
    opacity_raw = _as_2d(opacity_raw, "opacity_raw", 1)
    sh_dc = _as_2d(sh_dc, "sh_dc", 3)

    for name, values in (
        ("scale_raw", scale_raw),
        ("rot_raw", rot_raw),
        ("opacity_raw", opacity_raw),
        ("sh_dc", sh_dc),
    ):
        if values.shape[0] != count:
            raise ValueError(f"{name}: {values.shape[0]} rows, but xyz has {count}")

    if normals is None:
        # Upstream writes zeros here and no renderer reads them; a Gaussian has
        # no surface normal to record in the first place.
        normals = np.zeros((count, 3), dtype=np.float32)
    else:
        normals = _as_2d(normals, "normals", 3)

    columns = [xyz, normals, sh_dc]
    n_rest = 0
    if sh_rest is not None:
        rest = np.asarray(sh_rest, dtype=np.float32)
        if rest.ndim != 3 or rest.shape[0] != count or rest.shape[2] != 3:
            raise ValueError(f"sh_rest: expected (N, bands, 3), got {tuple(rest.shape)}")
        n_rest = rest.shape[1]
        # Channel-major, matching upstream's `transpose(1, 2).flatten(start_dim=1)`.
        columns.append(np.ascontiguousarray(rest.transpose(0, 2, 1)).reshape(count, -1))
    columns += [opacity_raw, scale_raw, rot_raw]

    table = np.concatenate(columns, axis=1).astype("<f4", copy=False)
    names = attribute_names(n_rest)
    if table.shape[1] != len(names):
        raise AssertionError(f"packed {table.shape[1]} columns for {len(names)} properties")

    header = ["ply", "format binary_little_endian 1.0", f"element vertex {count}"]
    header += [f"property float {name}" for name in names]
    header += ["end_header", ""]

    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as handle:
        handle.write("\n".join(header).encode("ascii"))
        handle.write(table.tobytes(order="C"))
    return path


#: PLY scalar type names -> NumPy codes, for the mixed-type headers real
#: producers write. QUEEN adds an ``int vertex_id`` column to its Gaussians, so
#: assuming every property is float32 makes its output unreadable.
_PLY_TYPES = {
    "float": "f4", "float32": "f4", "float64": "f8", "double": "f8",
    "char": "i1", "int8": "i1", "uchar": "u1", "uint8": "u1",
    "short": "i2", "int16": "i2", "ushort": "u2", "uint16": "u2",
    "int": "i4", "int32": "i4", "uint": "u4", "uint32": "u4",
}


def _read_header(handle) -> tuple[int, list[tuple[str, str]], bool]:
    """(vertex count, [(property, NumPy code)], little-endian) from an open file."""
    if handle.read(3) != _HEADER_MAGIC:
        raise ValueError("not a PLY file")
    handle.seek(0)
    count: int | None = None
    names: list[tuple[str, str]] = []
    little = True
    in_vertex = False
    while True:
        line = handle.readline()
        if not line:
            raise ValueError("PLY header has no end_header")
        text = line.decode("ascii", "replace").strip()
        if text.startswith("format"):
            if "ascii" in text:
                raise ValueError("ascii PLY is not supported; 3DGS writes binary")
            little = "little_endian" in text
        elif text.startswith("element "):
            parts = text.split()
            in_vertex = parts[1] == "vertex"
            if in_vertex:
                count = int(parts[2])
        elif text.startswith("property ") and in_vertex:
            parts = text.split()
            if parts[1] == "list":
                raise ValueError(
                    f"list properties are not supported in a vertex element: {text!r}"
                )
            code = _PLY_TYPES.get(parts[1])
            if code is None:
                raise ValueError(f"unsupported property type in {text!r}")
            names.append((parts[2], code))
        elif text == "end_header":
            break
    if count is None:
        raise ValueError("PLY header declares no vertex element")
    return count, names, little


def count(path: Path | str) -> int:
    """The Gaussian count, from the header alone -- no payload is read."""
    with Path(path).open("rb") as handle:
        return _read_header(handle)[0]


def read(path: Path | str) -> dict[str, Any]:
    """Read a 3DGS PLY back into raw arrays, the inverse of :func:`write`."""
    path = Path(path)
    with path.open("rb") as handle:
        n, properties, little = _read_header(handle)
        order = "<" if little else ">"
        # A structured dtype rather than one flat float32 block: the properties
        # are not all the same width, and reading them as if they were shifts
        # every column after the first odd one.
        record = np.dtype([(name, order + code) for name, code in properties])
        rows = np.frombuffer(handle.read(n * record.itemsize), dtype=record, count=n)
    names = [name for name, _ in properties]
    present = set(names)

    def take(keys: list[str]) -> np.ndarray:
        missing = [key for key in keys if key not in present]
        if missing:
            raise ValueError(f"{path.name} is missing {', '.join(missing)}")
        return np.stack(
            [rows[key].astype(np.float32) for key in keys], axis=-1
        )

    n_rest = sum(1 for name in names if name.startswith("f_rest_")) // 3
    result: dict[str, Any] = {
        "count": n,
        "xyz": take(["x", "y", "z"]),
        "sh_dc": take(["f_dc_0", "f_dc_1", "f_dc_2"]),
        "opacity_raw": take(["opacity"]),
        "scale_raw": take(["scale_0", "scale_1", "scale_2"]),
        "rot_raw": take([f"rot_{i}" for i in range(4)]),
        "sh_degree": int(round((n_rest + 1) ** 0.5)) - 1 if n_rest else 0,
    }
    if n_rest:
        rest = take([f"f_rest_{i}" for i in range(3 * n_rest)])
        result["sh_rest"] = rest.reshape(n, 3, n_rest).transpose(0, 2, 1)
    return result
