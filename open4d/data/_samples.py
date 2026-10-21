"""Byte-reproducible Open4D sample archives for the public data catalog.

The archives are built only with IEEE-754 ``+ - * /`` on Python floats, fixed
text formatting and uncompressed USTAR tar members with zeroed metadata, so
every platform and Python version produces identical bytes. The catalog
records their SHA-256; a local build stands in for a download until the
archives are uploaded, and its digest proves the build is still reproducible.
"""

from __future__ import annotations

import io
import json
import struct
import tarfile

from open4d.demo import _LICENSE

MESH_ID = "open4d-synthetic-mesh"
GAUSSIAN_ID = "open4d-synthetic-gaussian"
VERSION = "1.0.0"
FPS = 30.0
FRAMES = 8
_SIDE = 6
_SPLATS = 24
_SH_DEGREE = 1


def _wave(phase):
    """A smooth periodic bump in [0, 1] built from exact arithmetic only."""
    phase -= int(phase)
    if phase < 0:
        phase += 1.0
    return 16.0 * phase * phase * (1.0 - phase) * (1.0 - phase)


def _number(value):
    text = f"{value:.6f}"
    return "0.000000" if text == "-0.000000" else text


def _mesh_frames():
    faces = []
    for row in range(_SIDE - 1):
        for column in range(_SIDE - 1):
            a = row * _SIDE + column + 1
            faces += [f"f {a} {a + 1} {a + _SIDE + 1}", f"f {a} {a + _SIDE + 1} {a + _SIDE}"]
    for frame in range(FRAMES):
        lines = [f"# Open4D synthetic mesh frame {frame}; MIT; see LICENSE"]
        for row in range(_SIDE):
            for column in range(_SIDE):
                x = column / (_SIDE - 1) - 0.5
                y = row / (_SIDE - 1) - 0.5
                z = 0.15 * _wave(x + 0.5 + frame / FRAMES) * _wave(y + 0.5)
                lines.append(f"v {_number(x)} {_number(y)} {_number(z)}")
        yield f"frame_{frame:04d}.obj", ("\n".join(lines + faces) + "\n").encode("ascii")


def _circle(index, count):
    """A point on the unit circle by the rational parameterization."""
    t = 4.0 * index / count - 2.0  # (-2, 2) covers most of the circle evenly enough
    return (1.0 - t * t) / (1.0 + t * t), 2.0 * t / (1.0 + t * t)


def _gaussian_frames():
    coefficients = (_SH_DEGREE + 1) ** 2
    names = ["x", "y", "z", "nx", "ny", "nz", "f_dc_0", "f_dc_1", "f_dc_2"]
    names += [f"f_rest_{i}" for i in range(3 * (coefficients - 1))]
    names += ["opacity", "scale_0", "scale_1", "scale_2", "rot_0", "rot_1", "rot_2", "rot_3"]
    header = "".join(["ply\nformat binary_little_endian 1.0\n",
                      "comment Open4D synthetic Gaussians; MIT; see LICENSE\n",
                      f"element vertex {_SPLATS}\n"]
                     + [f"property float {name}\n" for name in names] + ["end_header\n"])
    for frame in range(FRAMES):
        rows = []
        for index in range(_SPLATS):
            x, y = _circle(index, _SPLATS)
            height = 0.4 * _wave(index / _SPLATS + frame / FRAMES) - 0.2
            dc = [1.2 * x, 1.2 * y, 0.8 * height]
            # Graphdeco stores f_rest channel-major: all R, then all G, then all B.
            rest = [0.05 * (channel + 1) * (k + 1) * (1.0 if index % 2 else -1.0)
                    for channel in range(3) for k in range(coefficients - 1)]
            raw = [x, y, height, 0.0, 0.0, 0.0, *dc, *rest,
                   1.5 - 0.125 * (index % 4),          # pre-sigmoid opacity
                   -3.0, -3.0 - 0.25 * (frame % 2), -3.5,  # log scales
                   1.0, 0.0, 0.0, 0.125 * (index % 3)]  # wxyz, unnormalized
            rows.append(struct.pack(f"<{len(raw)}f", *raw))
        yield f"frame_{frame:04d}.ply", header.encode("ascii") + b"".join(rows)


def _tar(folder, members):
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w", format=tarfile.USTAR_FORMAT) as archive:
        for name, data in sorted(members.items()):
            member = tarfile.TarInfo(f"{folder}/{name}")
            member.size, member.mtime, member.mode = len(data), 0, 0o644
            member.uid = member.gid = 0
            member.uname = member.gname = ""
            archive.addfile(member, io.BytesIO(data))
    return buffer.getvalue()


def _metadata(asset_id, extra):
    return (json.dumps({"id": asset_id, "version": VERSION, "license": "MIT",
                        "generator": "open4d.data._samples", "synthetic": True,
                        "fps": FPS, "frame_count": FRAMES, **extra},
                       indent=2, sort_keys=True) + "\n").encode("ascii")


def build(asset_id, version=VERSION):
    """Return ``{file name: bytes}`` for one catalog sample, deterministically."""
    if version != VERSION:
        raise KeyError(f"no deterministic builder for {asset_id}@{version}")
    if asset_id == MESH_ID:
        members = dict(_mesh_frames())
        members["metadata.json"] = _metadata(asset_id, {
            "vertices": _SIDE * _SIDE, "triangles": 2 * (_SIDE - 1) ** 2,
            "coordinate_convention": "right-handed, +Z up", "units": "unitless"})
    elif asset_id == GAUSSIAN_ID:
        members = dict(_gaussian_frames())
        members["metadata.json"] = _metadata(asset_id, {
            "gaussians": _SPLATS, "sh_degree": _SH_DEGREE,
            "coordinate_convention": "right-handed, +Z up", "units": "unitless",
            "parameter_convention": "graphdeco-ply"})
    else:
        raise KeyError(f"no deterministic builder for {asset_id}@{version}")
    members["LICENSE"] = _LICENSE.encode("utf-8")
    folder = f"{asset_id}-{version}"
    return {f"{folder}.tar": _tar(folder, members)}


BUILDERS = {(MESH_ID, VERSION), (GAUSSIAN_ID, VERSION)}
