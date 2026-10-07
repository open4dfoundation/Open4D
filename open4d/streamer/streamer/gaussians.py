"""Open4D's own Gaussian sequences as a bundle clip.

`export` is the mesh and point-cloud half of this; this is the other half. Until
it existed the only way a Gaussian clip reached the client was `gs_tools`, which
reads trainer output off disk -- so a sequence Open4D itself had loaded (a QUEEN
or 3DGStream `GaussianRun`, a Gaussian ``.vmesh`` opened as a `NativeSequence`,
Vega's decoded frames, or a list of `GaussianSplats`) had a codec registry entry
and a renderer waiting for it and no path between the two.

Two frame formats, the two the registry already lists for ``gaussians``:

``ply``
    Graphdeco's 3DGS PLY: raw training parameters at float32, every SH band
    kept. The interchange form, and the default for the reason `gs_tools.io.splat`
    gives -- dropping view-dependent appearance should be asked for.
``splat``
    32 bytes a Gaussian, degree-0 colour only. See `codecs` for what it costs.

Either can keep only a fraction of each frame's Gaussians -- ``splat@25%`` --
which is what gives a Gaussian clip a ladder worth adapting along. The two
formats alone differ by about 5x; a frame of 63k Gaussians is 10 MB as PLY and
2 MB as ``.splat``, so at 30 fps the cheaper of them is still 480 Mbit/s and no
real link has a rung to fall back to. Gaussians are ranked by opacity times
volume^(2/3), a projected-footprint proxy: the global significance LightGaussian
prunes by, without the per-view hit counts that would need a renderer here.

Vega frames carry no SH at all: their colour is a network evaluated per view
direction. They are baked here from one direction, outside the bounds, which is
what the `gs_tools` Vega exporter does too and says the same thing about.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable

import numpy as np

from . import bundle

PLY_FORMAT = "ply"
SPLAT_FORMAT = "splat"
FORMATS = (PLY_FORMAT, SPLAT_FORMAT)

#: Degree-0 SH constant, Graphdeco's ``C0``: ``rgb = 0.5 + C0 * f_dc``.
SH_C0 = 0.28209479177387814

#: Where a neural appearance is baked from: an eye on a ring around the bounds,
#: at this azimuth (radians) and elevation (fraction of the bounds diagonal).
#: The same placement as `gs_tools.methods.vega`'s defaults.
BAKE_AZIMUTH = 0.0
BAKE_ELEVATION = 0.15

#: Opacity is stored as a logit, which is infinite at 0 and 1.
_OPACITY_EPSILON = 1e-6


@dataclass(frozen=True)
class GaussianFrames:
    """Gaussian frames, loaded one at a time, and their timing if known."""

    count: int
    load: Callable[[int], Any]
    #: Seconds per frame from the source, or None when it carries no timing --
    #: a trainer's output directory, or a bare list of splats.
    timestamps: tuple[float, ...] | None = None
    #: The method that produced them, when the source says.
    method: str | None = None

    @property
    def fps(self) -> float | None:
        if not self.timestamps or len(self.timestamps) < 2:
            return None
        span = self.timestamps[-1] - self.timestamps[0]
        return (len(self.timestamps) - 1) / span if span > 0 else None


def is_gaussian_frame(value: object) -> bool:
    """`GaussianSplats` or Vega's `NeuralGaussianFrame`."""
    from open4d import GaussianSplats, NeuralGaussianFrame

    return isinstance(value, (GaussianSplats, NeuralGaussianFrame))


def frames_of(source: Any) -> GaussianFrames:
    """Whatever Open4D hands back for a Gaussian sequence, as `GaussianFrames`.

    A `NativeSequence` is decoded here, which runs its method's runtime -- the
    same trust boundary `NativeSequence.decode` documents. A ``neural_field``
    sequence (ReRF) is refused: it cannot be decoded in a browser at all, and
    its route to a client is server-side rendering through `live` or `adopt`.
    """
    from open4d import GaussianRun, NativeSequence
    from open4d.gaussians import VegaRun

    if isinstance(source, GaussianFrames):
        return source
    if isinstance(source, GaussianRun):
        paths = source.frame_paths
        return GaussianFrames(len(paths), source.load_frame, method=source.method)
    if isinstance(source, VegaRun):
        frames = source.decode()
        return GaussianFrames(len(frames), frames.__getitem__, method="vega")
    if isinstance(source, NativeSequence):
        if source.representation not in ("gaussian_splats", "neural_gaussians"):
            raise TypeError(
                f"a {source.codec} sequence is {source.representation}, which a "
                "browser cannot decode; render it server-side with streamer.live "
                "or streamer.adopt instead"
            )
        frames = tuple(source.decode())
        return GaussianFrames(
            len(frames), frames.__getitem__,
            timestamps=tuple(source.timestamps), method=source.codec,
        )
    if isinstance(source, (list, tuple)):
        frames = tuple(source)
        if not frames:
            raise ValueError("no Gaussian frames to export")
        if not all(is_gaussian_frame(frame) for frame in frames):
            raise TypeError(
                "a list of frames must hold only GaussianSplats or "
                "NeuralGaussianFrame"
            )
        return GaussianFrames(len(frames), frames.__getitem__)
    raise TypeError(f"{type(source).__name__} is not a Gaussian sequence")


def _bake_eye(positions: np.ndarray) -> np.ndarray:
    lower, upper = positions.min(axis=0), positions.max(axis=0)
    radius = float(np.linalg.norm(upper - lower)) or 1.0
    direction = np.array(
        [math.sin(BAKE_AZIMUTH), BAKE_ELEVATION, math.cos(BAKE_AZIMUTH)]
    )
    return (lower + upper) / 2.0 + radius * direction


def _colour_coefficients(frame: Any) -> np.ndarray:
    """``(N, K, 3)`` SH for ``frame``; one band, baked, for a neural frame."""
    sh = getattr(frame, "spherical_harmonics", None)
    if sh is not None:
        return np.asarray(sh, dtype=np.float32)
    positions = np.asarray(frame.positions, dtype=np.float64)
    directions = positions - _bake_eye(positions)
    norms = np.linalg.norm(directions, axis=1, keepdims=True)
    directions = np.divide(directions, norms, out=np.zeros_like(directions),
                           where=norms > 0)
    directions[norms[:, 0] == 0] = (0.0, 0.0, 1.0)
    rgb = np.asarray(frame.appearance.colors(directions), dtype=np.float32)
    return ((rgb - 0.5) / SH_C0)[:, None, :]


@dataclass(frozen=True)
class _Splats:
    """One frame's arrays, activated, with colour as SH: what the writers take."""

    positions: np.ndarray
    scales: np.ndarray
    rotations: np.ndarray
    opacities: np.ndarray
    sh: np.ndarray

    def __len__(self) -> int:
        return len(self.positions)

    def keep(self, fraction: float) -> "_Splats":
        """The ``fraction`` most significant Gaussians, in their original order."""
        if fraction >= 1.0:
            return self
        count = max(1, math.ceil(len(self) * fraction))
        volume = np.prod(self.scales.astype(np.float64), axis=1)
        significance = self.opacities.astype(np.float64) * volume ** (2.0 / 3.0)
        chosen = np.sort(np.argsort(-significance, kind="stable")[:count])
        return _Splats(*(getattr(self, name)[chosen] for name in
                         ("positions", "scales", "rotations", "opacities", "sh")))


def _splats(frame: Any) -> _Splats:
    return _Splats(
        positions=np.asarray(frame.positions, dtype=np.float32),
        scales=np.asarray(frame.scales, dtype=np.float32),
        rotations=np.asarray(frame.rotations, dtype=np.float32),
        opacities=np.asarray(frame.opacities, dtype=np.float32),
        sh=_colour_coefficients(frame),
    )


def _ply_bytes(frame: _Splats) -> bytes:
    """Graphdeco's layout: raw parameters, ``f_rest`` channel-major."""
    count, sh = len(frame), frame.sh
    rest = sh[:, 1:, :].transpose(0, 2, 1).reshape(count, -1)
    opacity = np.clip(frame.opacities.astype(np.float64),
                      _OPACITY_EPSILON, 1 - _OPACITY_EPSILON)
    columns = [
        frame.positions,
        np.zeros((count, 3), dtype=np.float32),
        sh[:, 0, :],
        rest,
        np.log(opacity / (1 - opacity))[:, None],
        np.log(frame.scales.astype(np.float64)),
        frame.rotations,
    ]
    names = ["x", "y", "z", "nx", "ny", "nz", "f_dc_0", "f_dc_1", "f_dc_2"]
    names += [f"f_rest_{i}" for i in range(rest.shape[1])]
    names += ["opacity", "scale_0", "scale_1", "scale_2"]
    names += [f"rot_{i}" for i in range(4)]
    body = np.hstack([np.asarray(c, dtype=np.float32) for c in columns])
    header = "".join(
        ["ply\nformat binary_little_endian 1.0\n", f"element vertex {count}\n"]
        + [f"property float {name}\n" for name in names]
        + ["end_header\n"]
    )
    return header.encode("ascii") + body.astype("<f4").tobytes()


def _splat_bytes(frame: _Splats) -> bytes:
    """The 32-byte layout `codecs` registers; see `gs_tools.io.splat`."""
    count, sh = len(frame), frame.sh
    payload = np.empty((count, 32), dtype=np.uint8)
    floats = payload[:, :24].view(np.float32).reshape(count, 6)
    floats[:, :3] = frame.positions
    floats[:, 3:] = frame.scales
    rgb = np.clip(0.5 + SH_C0 * sh[:, 0, :], 0.0, 1.0)
    payload[:, 24:27] = np.rint(rgb * 255.0).astype(np.uint8)
    payload[:, 27] = np.clip(np.rint(frame.opacities * 255.0), 0, 255).astype(np.uint8)
    payload[:, 28:32] = np.clip(np.rint(frame.rotations * 128.0) + 128,
                                0, 255).astype(np.uint8)
    return payload.tobytes()


def from_frames(
    source: Any,
    out_dir: Path | str,
    *,
    name: str,
    frame_format: str = PLY_FORMAT,
    keep: float = 1.0,
    scene: str | None = None,
    method: str | None = None,
    notes: list[str] | None = None,
    detail: dict[str, Any] | None = None,
) -> bundle.Clip:
    """Write Gaussian frames into ``out_dir`` as one clip and describe it.

    ``keep`` is the fraction of each frame's Gaussians written, most
    significant first; see the module docstring for the ranking.
    """
    if not 0.0 < keep <= 1.0:
        raise ValueError(f"keep must be in (0, 1]; got {keep}")
    if frame_format not in FORMATS:
        raise ValueError(
            f"unknown Gaussian frame format {frame_format!r}; expected one of "
            + ", ".join(FORMATS)
        )
    frames = frames_of(source)
    if not frames.count:
        raise ValueError(f"{name} has no frames")
    out_dir = Path(out_dir).expanduser().resolve()
    frames_at = bundle.frame_dir(out_dir, name)

    written: list[str] = []
    counts: list[int] = []
    degrees: set[int] = set()
    baked = False
    lower = np.full(3, np.inf)
    upper = np.full(3, -np.inf)
    for index in range(frames.count):
        frame = frames.load(index)
        if not is_gaussian_frame(frame):
            raise TypeError(f"{name} frame {index} is a {type(frame).__name__}")
        baked = baked or getattr(frame, "spherical_harmonics", None) is None
        splats = _splats(frame).keep(keep)
        degrees.add(int(math.isqrt(splats.sh.shape[1])) - 1)
        writer = _splat_bytes if frame_format == SPLAT_FORMAT else _ply_bytes
        target = frames_at / f"frame_{index:06d}.{frame_format}"
        target.write_bytes(writer(splats))
        written.append(str(target.relative_to(out_dir)))
        positions = splats.positions.astype(np.float64)
        counts.append(len(positions))
        lower = np.minimum(lower, positions.min(axis=0))
        upper = np.maximum(upper, positions.max(axis=0))

    notes = list(notes or [])
    if keep < 1.0:
        notes.append(
            f"{keep:.0%} of each frame's Gaussians kept, ranked by opacity times "
            "volume^(2/3); the rest are dropped"
        )
    if baked:
        notes.append(
            "colour baked from one direction outside the bounds: this method's "
            "appearance is a network evaluated per view, and a frame file holds "
            "one colour per Gaussian"
        )
    if frame_format == SPLAT_FORMAT:
        notes.append(
            "frames are .splat (32 bytes per Gaussian): every spherical-harmonic "
            "band above degree 0 is dropped, and colour, opacity and rotation are "
            "quantised to 8 bits"
        )
    return bundle.Clip(
        name=frames_at.name,
        representation="gaussians",
        scene=scene or frames_at.name,
        method=method or frames.method or "gaussians",
        frames=written,
        counts=counts,
        bounds_min=lower.tolist(),
        bounds_max=upper.tolist(),
        notes=notes,
        detail={
            **(detail or {}),
            "frame_format": frame_format,
            **({"keep": keep} if keep < 1.0 else {}),
            "sh_degrees": sorted(degrees),
        },
    )
