"""3DGS runs, made viewable -- QUEEN, 3DGStream, or any plain 3DGS output.

The easiest of the exporters here, and the last to be written, which is the
wrong way round: these runs already store Gaussians in the one format every
splat viewer reads, so making them viewable is a copy plus a manifest. Vega
needed its colour decoded through a hash grid first and ReRF cannot be decoded
in this process at all; this needs neither. Until it existed, the bundled
client's free camera showed Vega and nothing else -- not because the other
methods lacked geometry, but because nothing exported theirs.

Deliberately not a trainer wrapper. `queen` and `gstream` in this package run
the upstream trainers; this reads whatever they left on disk, so it works on a
run produced by any 3DGS implementation, including ones this repository has
never heard of. `gs_tools.outputs.gaussian_frames` is what absorbs the three
unrelated on-disk layouts.

Two things it does not attempt:

* **The scene name is guessed from the directory.** A 3DGS run records no
  subject name anywhere reliable, so a run called ``verify`` becomes a scene
  called ``verify``. Pass ``scene`` to put it beside another method's clip of
  the same subject -- which is the only way `Compare` can line them up.
* **No rig.** These runs carry their own ``cameras.json`` in formats that differ
  per implementation, and the bundle's shared camera comes from the ORBIT corpus
  via `gs_tools.cameras`. A 3DGS run of an ORBIT object gets a rig like any
  other clip; one of some other capture is explore-only.
"""

from __future__ import annotations

import shutil
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import numpy as np
from streamer import bundle

from ..io import ply, splat
from ..outputs import Kind, detect, gaussian_frames

name = "gaussian"
#: No upstream tree of its own: this reads output, and several trainers write it.
upstream = None


@dataclass
class GaussianExportOptions:
    """What the export needs beyond the input and output paths."""

    #: How many frames; None means every one the run holds.
    frames: int | None = None
    #: ``"ply"`` copies the run's own files. ``"splat"`` re-encodes to 32 bytes
    #: per Gaussian, which is several times smaller and drops every
    #: spherical-harmonic band above degree 0 -- see `gs_tools.io.splat`.
    frame_format: str = "ply"
    #: Subject name, shared with other methods' clips of the same thing.
    scene: str | None = None
    #: Method label in the viewer; defaults to the run's manifest or "gaussian".
    method: str | None = None
    fps: int = 30
    extra: dict[str, Any] = field(default_factory=dict)


FORMATS = ("ply", "splat")


def _method_name(source: Path, found, options: GaussianExportOptions) -> str:
    """What to label this in the viewer.

    A run's own manifest is the only honest source, since the directory name
    says whatever its author felt like. Falling back to the representation name
    rather than to the directory keeps two unrelated runs from both claiming to
    be the method called ``output``.
    """
    if options.method:
        return options.method
    recorded = (found.detail or {}).get("method")
    return recorded or "gaussian"


def build_clips(
    source: Path | str,
    out_dir: Path | str,
    options: GaussianExportOptions | None = None,
) -> tuple[str, list[bundle.Clip], dict[str, Any]]:
    """Copy (or re-encode) a run's per-frame Gaussians into ``out_dir``."""
    options = options or GaussianExportOptions()
    if options.frame_format not in FORMATS:
        raise ValueError(
            f"unknown frame format {options.frame_format!r}; expected one of "
            + ", ".join(FORMATS)
        )
    source = Path(source).expanduser().resolve()
    out_dir = Path(out_dir).expanduser().resolve()

    found = detect(source)
    if found.kind is not Kind.GAUSSIAN_RUN:
        raise ValueError(
            f"{source} is {found.kind.value}, not a 3DGS run; expected one of the "
            "layouts gs_tools.outputs.gaussian_frames resolves"
        )

    entries = gaussian_frames(source)
    if options.frames is not None:
        entries = entries[: options.frames]
    if not entries:
        raise ValueError(f"{source} has no frames to export")

    scene = options.scene or source.name
    method = _method_name(source, found, options)
    frames_at = bundle.frame_dir(out_dir, f"{source.name}-{method}")
    clip_name = frames_at.name

    written: list[str] = []
    counts: list[int] = []
    lower = np.full(3, np.inf)
    upper = np.full(3, -np.inf)
    degrees: set[int] = set()

    for index, ply_path in entries:
        if options.frame_format == "splat":
            cloud = splat.from_ply(ply_path)
            target = splat.write(frames_at / f"frame_{index:04d}.splat", cloud)
            positions = cloud.positions
            degrees.add(0)
        else:
            # Copied rather than rewritten: the run's PLY is already the
            # interchange format, and re-encoding it would only add a chance to
            # get an activation or an SH ordering wrong.
            target = frames_at / f"frame_{index:04d}.ply"
            shutil.copyfile(ply_path, target)
            fields = ply.read(target)
            positions = fields["xyz"]
            degrees.add(int(fields["sh_degree"]))

        written.append(str(target.relative_to(out_dir)))
        counts.append(len(positions))
        lower = np.minimum(lower, positions.min(axis=0))
        upper = np.maximum(upper, positions.max(axis=0))
        print(
            f"      {clip_name} frame {index:04d}  {len(positions):7d} gaussians  "
            f"{target.stat().st_size / 1e6:5.2f} MB",
            flush=True,
        )

    notes = [
        f"3DGS run, {len(written)} frames, read from {source.name} as-is — "
        "not retrained or resampled",
    ]
    if options.frame_format == "splat":
        notes.append(
            "frames re-encoded to .splat (32 bytes per Gaussian): every "
            "spherical-harmonic band above degree 0 is dropped, so appearance "
            "no longer changes with view direction"
        )
    else:
        notes.append(
            "sh_degree " + ", ".join(str(d) for d in sorted(degrees))
            + ": view-dependent bands preserved as the run wrote them"
        )
    if options.scene is None:
        notes.append(
            f"scene name taken from the directory ({scene}); pass --scene to line "
            "this up with another method's clip of the same subject"
        )

    clip = bundle.Clip(
        name=clip_name,
        representation="gaussians",
        scene=scene,
        method=method,
        frames=written,
        counts=counts,
        bounds_min=lower.tolist(),
        bounds_max=upper.tolist(),
        notes=notes,
        detail={
            "source": str(source),
            "frame_format": options.frame_format,
            "frame_indices": [index for index, _ in entries],
            "sh_degrees": sorted(degrees),
            "iterations": (found.detail or {}).get("iterations", []),
        },
    )
    return f"3DGS — {source.name}", [clip], {"baseline": method}


def export(
    source: Path | str,
    out_dir: Path | str,
    options: GaussianExportOptions | None = None,
) -> Path:
    """Build a one-clip bundle from a 3DGS run and return its directory."""
    options = options or GaussianExportOptions()
    out_dir = Path(out_dir).expanduser().resolve()
    title, clips, detail = build_clips(source, out_dir, options)
    bundle.write(
        out_dir,
        title=title,
        source=str(source),
        clips=clips,
        fps=options.fps,
        detail=detail,
    )
    return out_dir
