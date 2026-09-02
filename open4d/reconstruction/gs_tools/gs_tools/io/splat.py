"""The 32-byte-per-Gaussian ``.splat`` frame format.

A 3DGS PLY is an interchange format, not a delivery one. It stores every
attribute as float32 and every spherical-harmonic band it was trained with, so
one frame of a degree-3 run is 248 bytes per Gaussian -- and a 30-frame clip of
a few hundred thousand Gaussians is hundreds of megabytes, which is the reason
a bundle cannot simply be downloaded. This is the same content quantised to a
fixed 32 bytes:

===========  ======  =======================================================
byte range   type    meaning
===========  ======  =======================================================
``0..11``    3f32    position, world space
``12..23``   3f32    scale, **activated** -- world-space standard deviations
``24..27``   4u8     colour RGB and opacity, ``round(v * 255)``
``28..31``   4u8     rotation ``(w, x, y, z)``, ``round(q * 128) + 128``
===========  ======  =======================================================

The rotation encoding is not symmetric: ``q = 1`` would be 256, which clamps to
255 and comes back as 0.992. A reader must therefore **renormalise** the
quaternion, which both readers here do -- building a covariance from a non-unit
one scales every Gaussian by about 1.6%.

Three consequences worth stating plainly, because each is a real loss:

* **Every SH band above degree 0 is dropped.** View-dependent appearance goes
  with them, so a frame written here looks the same from every direction. That
  is the same caveat the Vega exporter already carries for its baked colour --
  and for Vega it costs nothing, because that colour was baked before it ever
  reached a PLY. For a QUEEN or 3DGStream run it is a genuine reduction, which
  is why writing this is opt-in rather than the default.
* **Values are stored activated**, the opposite of the PLY convention. A reader
  must *not* apply exp/sigmoid/normalize; :func:`from_ply` is where that
  happens, once. Getting this backwards renders as fog either way, so the two
  formats are deliberately not interchangeable byte-for-byte.
* **Rotation keeps 8 bits per component.** Enough for a unit quaternion at this
  scale, but it is quantisation, and re-exporting from a ``.splat`` would
  compound it. Bundles keep the PLY as the source of truth.

The byte layout matches the widely used ``.splat`` files by field order and
size. Interoperability with other tools that read the extension is *not*
verified here -- nothing in this repository reads one -- so the layout above,
not any external tool, is the specification this module implements.
"""

from __future__ import annotations

from pathlib import Path

import numpy as np
from open4d.core import GaussianCloud

from . import ply

#: Bytes per Gaussian. Fixed, so a frame's Gaussian count is its file size / 32.
SPLAT_BYTES = 32

#: Rotation is stored as ``round(q * QUANT_SCALE) + QUANT_OFFSET``.
QUANT_SCALE = 128.0
QUANT_OFFSET = 128


def encode(cloud: GaussianCloud) -> bytes:
    """One frame of Gaussians as ``.splat`` bytes.

    Takes `open4d.core.GaussianCloud` rather than loose arrays because that type
    already guarantees what this encoding assumes and cannot check cheaply:
    scales activated and nonnegative, quaternions unit, opacity in [0, 1].
    """
    if not isinstance(cloud, GaussianCloud):
        raise TypeError("cloud must be an open4d.core.GaussianCloud")
    count = len(cloud.positions)
    if cloud.colors is None:
        raise ValueError(
            "cloud has no colors; .splat carries a single RGB per Gaussian, so "
            "there is nothing to write without one"
        )

    payload = np.empty((count, SPLAT_BYTES), dtype=np.uint8)
    floats = payload[:, :24].view(np.float32).reshape(count, 6)
    floats[:, :3] = cloud.positions
    floats[:, 3:] = cloud.scales

    payload[:, 24:27] = np.clip(np.rint(cloud.colors * 255.0), 0, 255).astype(np.uint8)
    payload[:, 27] = np.clip(np.rint(cloud.opacities * 255.0), 0, 255).astype(np.uint8)
    payload[:, 28:32] = np.clip(
        np.rint(cloud.rotations * QUANT_SCALE) + QUANT_OFFSET, 0, 255
    ).astype(np.uint8)
    return payload.tobytes()


def write(path: Path | str, cloud: GaussianCloud) -> Path:
    """Write ``cloud`` as a ``.splat`` frame and return the path."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(encode(cloud))
    return path


def count(path: Path | str) -> int:
    """Gaussians in a ``.splat`` file, from its size alone."""
    size = Path(path).stat().st_size
    if size % SPLAT_BYTES:
        raise ValueError(
            f"{path} is {size} bytes, not a multiple of {SPLAT_BYTES}; "
            "this is not a .splat frame"
        )
    return size // SPLAT_BYTES


def decode(data: bytes) -> GaussianCloud:
    """``.splat`` bytes back to a `GaussianCloud`, for tests and round-trips."""
    if len(data) % SPLAT_BYTES:
        raise ValueError(f"{len(data)} bytes is not a multiple of {SPLAT_BYTES}")
    payload = np.frombuffer(data, dtype=np.uint8).reshape(-1, SPLAT_BYTES)
    floats = payload[:, :24].copy().view(np.float32).reshape(-1, 6)
    # Renormalised, because the quantisation is not symmetric: w = 1 encodes as
    # round(128) + 128 = 256, which clamps to 255 and decodes to 0.992. Without
    # this the covariance is built from a non-unit quaternion and every Gaussian
    # is scaled by ~1.6% -- small, wrong, and invisible until measured.
    rotations = (payload[:, 28:32].astype(np.float32) - QUANT_OFFSET) / QUANT_SCALE
    norms = np.linalg.norm(rotations, axis=1, keepdims=True)
    rotations = np.divide(
        rotations, norms, out=np.zeros_like(rotations), where=norms > 0
    )
    return GaussianCloud(
        positions=floats[:, :3].copy(),
        scales=floats[:, 3:].copy(),
        rotations=rotations,
        opacities=payload[:, 27].astype(np.float32) / 255.0,
        colors=payload[:, 24:27].astype(np.float32) / 255.0,
    )


def from_ply(path: Path | str) -> GaussianCloud:
    """A 3DGS PLY as a `GaussianCloud`, with the activations applied once.

    This is the bridge every Gaussian producer in this repository crosses to
    reach core's canonical form: PLY stores raw training parameters, core stores
    activated ones, and doing the conversion here means no exporter has to
    remember which is which.

    Only the degree-0 band is read. ``f_rest`` is left on the floor, which is
    the loss this format's docstring describes.
    """
    fields = ply.read(path)
    scales = np.exp(fields["scale_raw"])
    opacities = 1.0 / (1.0 + np.exp(-fields["opacity_raw"].reshape(-1)))
    rotations = fields["rot_raw"]
    norms = np.linalg.norm(rotations, axis=1, keepdims=True)
    # A zero quaternion is a corrupt file rather than something to normalise; the
    # GaussianCloud constructor rejects it, and its message says so.
    rotations = np.divide(rotations, norms, out=np.zeros_like(rotations), where=norms > 0)
    return GaussianCloud(
        positions=fields["xyz"],
        scales=scales,
        rotations=rotations,
        opacities=opacities,
        colors=np.clip(ply.sh_dc_to_rgb(fields["sh_dc"].reshape(-1, 3)), 0.0, 1.0),
    )
