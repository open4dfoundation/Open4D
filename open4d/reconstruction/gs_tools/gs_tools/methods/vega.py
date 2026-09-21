"""Vega, made viewable.

Unlike QUEEN and 3DGStream this is not a trainer wrapper. Vega's own
`orbitvega.prepare` produces the run; what was missing was any way to *look* at
what it produced. Its bitstream is per-object ``frame_XXXX.pt`` chunks holding
geometry only -- position, scale, rotation, opacity -- with colour living in a
hierarchical hash grid (`vega.color_encoding`) that is queried per Gaussian per
view direction at render time. So there is no file in a Vega run that any
Gaussian-splatting viewer can open, and the fix is not a format shim: colour
has to be *decoded* first, which means running Vega's own model.

This adapter does exactly that and nothing more. It drives
`vega.player.StreamingPlayer` -- the same client-side reassembly Vega's live
demo uses, so key/residual handling is upstream's, not a reimplementation --
decodes colour through the object's own `HierarchicalColorModel`, and writes
one 3DGS PLY per frame.

Two consequences of that route are worth being explicit about, because both are
visible in the output:

* **Colour is baked, and the export is degree-0.** The hash grid is
  view-dependent; a PLY's ``f_dc`` is not. Colour is therefore evaluated once,
  from one camera azimuth, and frozen -- a free-camera viewer will show the
  subject's appearance from that direction no matter where it is orbited to.
  Re-export at a different ``--bake-azimuth`` to see the appearance from
  somewhere else. Writing the full spherical-harmonic bands instead is not an
  option: the hash grid is not an SH expansion, and fitting one per Gaussian
  would be a different piece of work with its own error.
* **The grid is queried at each Gaussian's original position.**
  `HierarchicalColorModel._normalize` maps position into [0, 1] against the
  bbox the model was trained on and *clamps*, so feeding it anything outside
  those bounds collapses colour onto the box faces. That is why the bake camera
  is derived from the model's own bbox rather than from a scene layout.

Reading the bitstream needs no server: `vega.player.BitstreamClient` fetches
over ``urllib``, and a ``file://`` base URL is a valid thing to hand it, so the
chunk server `orbitvega.scene_export` starts is skipped here.

Runs in the ``open4d-gs`` environment: decoding colour needs torch and
tinycudann, the same as Vega itself.
"""

from __future__ import annotations

import json
import math
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import numpy as np

from .. import upstream_import
from streamer import bundle

from .. import io
from ..io import ply
from ..outputs import Kind, detect

name = "vega"
upstream = "vega"


@dataclass
class VegaExportOptions:
    """What the export needs beyond the input and output paths."""

    #: Object names to export; empty means every object in the catalog.
    objects: tuple[str, ...] = ()
    #: How many frames per object; None means the whole encoded sequence.
    frames: int | None = None
    #: Camera azimuth, in degrees about the world up axis, that colour is
    #: evaluated from. 0 looks along +Z towards the subject.
    bake_azimuth_deg: float = 0.0
    #: Bake-camera height above the bbox centre, as a fraction of its radius.
    #: Slightly above eye level, so the top of a head is not lit as if from below.
    bake_elevation: float = 0.15
    #: ``"ply"`` writes 3DGS PLY. ``"splat"`` re-encodes to 32 bytes a
    #: Gaussian, which for this exporter loses nothing structural: Vega's
    #: colour is baked to a single band here anyway, so there are no
    #: spherical-harmonic coefficients above degree 0 to drop. Roughly half the
    #: bytes a frame, and half of what a client has to move.
    frame_format: str = "ply"
    #: "cuda", "cpu", or None to take CUDA when it is there.
    device: str | None = None
    fps: int = 30
    extra: dict[str, Any] = field(default_factory=dict)


FORMATS = io.GAUSSIAN_FORMATS


def _write_frame(path_without_suffix: Path, options: VegaExportOptions, **fields):
    """One frame, in the format asked for.

    Shared by both export paths so they cannot disagree about it -- the
    bitstream path and the pre-baked scene path write the same fields and
    previously each called `ply.write` directly.

    `.splat` goes through the PLY rather than around it. Encoding straight from
    the arrays would mean a second implementation of the same quantisation, and
    the round trip is checked: `gs_tools.io.splat` keeps position and scale
    exactly. The PLY is removed afterwards, since keeping both doubles the
    export for a file no client asks for.
    """
    if options.frame_format not in FORMATS:
        raise ValueError(
            f"frame_format {options.frame_format!r} is not one of "
            + ", ".join(FORMATS)
        )
    ply_path = ply.write(path_without_suffix.with_suffix(".ply"), **fields)
    if options.frame_format != "splat":
        return ply_path
    cloud = io.splat.from_ply(ply_path)
    written = io.splat.write(path_without_suffix.with_suffix(".splat"), cloud)
    ply_path.unlink()
    return written


def _format_notes(options: VegaExportOptions) -> list[str]:
    """What the chosen format costs, said in the clip rather than assumed."""
    if options.frame_format != "splat":
        return []
    return [
        "delivered as .splat (32 bytes a Gaussian) rather than 3DGS PLY: about "
        "half the bytes a frame, and nothing structural is dropped because this "
        "export bakes colour to degree 0 anyway — opacity, rotation and colour "
        "are quantised to 8 bits, position and scale are exact",
    ]


def _torch():
    import torch

    return torch


def _vega_modules():
    """Vega's player, camera helpers and view-direction helper.

    Imported from the vendored tree rather than a copy: the key/residual
    reassembly in `StreamingPlayer.reconstruct` is the part that is easy to get
    subtly wrong, and it is already written and tested next door.
    """
    with upstream_import.on_path("vega"):
        from vega.cameras import Camera, look_at_RT
        from vega.player import BitstreamClient, StreamingPlayer
        from vega.rasterize import view_directions

    return Camera, look_at_RT, BitstreamClient, StreamingPlayer, view_directions


def objects_in(source: Path) -> list[dict[str, Any]]:
    """The exportable objects at ``source``, as ``{"name", "dir"}`` entries.

    A catalog lists them; a bare bitstream directory is itself one object.
    """
    source = Path(source)
    catalog_path = source / "catalog.json"
    if catalog_path.is_file():
        catalog = json.loads(catalog_path.read_text())
        return [
            {"name": entry["name"], "dir": entry.get("dir", entry["name"]), "catalog": entry}
            for entry in catalog.get("objects", [])
        ]
    return [{"name": source.name, "dir": ".", "catalog": {}}]


def _bake_camera(bbox_min, bbox_max, options: VegaExportOptions, device: str):
    """A camera outside the object's bbox, used only for its centre position.

    Only the camera centre matters -- `view_directions` is the sole consumer --
    so the image size and field of view here are placeholders. Built through
    Vega's own `look_at_RT` so the convention (+Z forward, y down) matches what
    the colour model was trained against.
    """
    Camera, look_at_RT, *_ = _vega_modules()
    centre = (np.asarray(bbox_min) + np.asarray(bbox_max)) / 2.0
    radius = float(np.linalg.norm(np.asarray(bbox_max) - np.asarray(bbox_min))) or 1.0
    azimuth = math.radians(options.bake_azimuth_deg)
    eye = centre + radius * np.array(
        [math.sin(azimuth), options.bake_elevation, math.cos(azimuth)], dtype=np.float64
    )
    rotation, translation = look_at_RT(
        eye.astype(np.float32), centre.astype(np.float32), np.array([0.0, 1.0, 0.0])
    )
    return Camera(
        R=rotation,
        T=translation,
        fovx=1.0,
        fovy=1.0,
        width=1,
        height=1,
        device=device,
    )


def _export_bitstream(
    object_dir: Path,
    out_dir: Path,
    clip_name: str,
    options: VegaExportOptions,
) -> bundle.Clip:
    """Decode one object's bitstream to a PLY per frame."""
    torch = _torch()
    _, _, BitstreamClient, StreamingPlayer, view_directions = _vega_modules()
    device = options.device or ("cuda" if torch.cuda.is_available() else "cpu")

    client = BitstreamClient(f"file://{object_dir}")
    manifest = client.get_manifest()
    color_model = client.get_color_model(device)
    player = StreamingPlayer(color_model, device=device)

    bbox_min = color_model.bbox_min.detach().cpu().numpy()
    bbox_max = color_model.bbox_max.detach().cpu().numpy()
    camera = _bake_camera(bbox_min, bbox_max, options, device)

    entries = manifest["frames"]
    if options.frames is not None:
        entries = entries[: options.frames]
    if not entries:
        raise ValueError(f"{object_dir} has no frames to export")

    frames_at = bundle.frame_dir(out_dir, clip_name)
    clip_name = frames_at.name
    frames: list[str] = []
    counts: list[int] = []
    lower = np.full(3, np.inf)
    upper = np.full(3, -np.inf)

    for entry in entries:
        index = entry["frame_idx"]
        chunk = client.get_frame_chunk(index)
        gaussians = player.reconstruct(chunk)
        with torch.no_grad():
            dirs = view_directions(camera, gaussians.get_xyz)
            if chunk["frame_type"] == "key":
                rgb = color_model.forward_key(gaussians.get_xyz, dirs)
            else:
                rgb = color_model.forward_residual(gaussians.get_xyz, dirs, index)
            rgb = rgb.clamp(0.0, 1.0)

        xyz = gaussians.xyz.detach().cpu().numpy()
        path = _write_frame(
            frames_at / f"frame_{index:04d}", options,
            xyz=xyz,
            scale_raw=gaussians.scale_raw.detach().cpu().numpy(),
            rot_raw=gaussians.rot_raw.detach().cpu().numpy(),
            opacity_raw=gaussians.opacity_raw.detach().cpu().numpy(),
            sh_dc=ply.rgb_to_sh_dc(rgb.cpu().numpy()),
        )
        # Residual frames only carry the objects that changed, so the tiny hash
        # for a frame already written is dead weight from here on.
        color_model.drop_tiny_hash(index)
        frames.append(str(path.relative_to(out_dir)))
        counts.append(len(gaussians))
        lower = np.minimum(lower, xyz.min(axis=0))
        upper = np.maximum(upper, xyz.max(axis=0))
        print(
            f"      {clip_name} frame {index:04d}  {chunk['frame_type']:8s} "
            f"{len(gaussians):7d} gaussians  {path.stat().st_size / 1e6:5.2f} MB",
            flush=True,
        )

    return bundle.Clip(
        name=clip_name,
        representation="gaussians",
        scene=clip_name,
        method=name,
        frames=frames,
        counts=counts,
        bounds_min=lower.tolist(),
        bounds_max=upper.tolist(),
        notes=[
            f"colour baked at azimuth {options.bake_azimuth_deg:g}° and frozen "
            "(Vega's colour is a view-dependent hash grid; a PLY's f_dc is not)",
            "sh_degree 0: no view-dependent bands",
            *_format_notes(options),
        ],
        detail={
            "source": str(object_dir),
            "bbox_min": bbox_min.tolist(),
            "bbox_max": bbox_max.tolist(),
            "frame_types": [entry["frame_type"] for entry in entries],
        },
    )


def _export_scene(scene_dir: Path, out_dir: Path, options: VegaExportOptions) -> bundle.Clip:
    """Convert an `orbitvega.scene_export` directory, whose colour is already baked.

    Cheaper and lower-fidelity than :func:`_export_bitstream` in exactly one
    way: the bake camera was chosen when that export ran, not here. It needs no
    hash grid and no CUDA, which is the point -- the merged multi-object scene
    is Vega's own tool's output, so this reads it rather than reproducing the
    layout logic.
    """
    torch = _torch()
    scene = json.loads((scene_dir / "scene_manifest.json").read_text())
    entries = scene.get("frames") or [
        {"frame_idx": index, "file": path.name}
        for index, path in enumerate(sorted(scene_dir.glob("frame_*.pt")))
    ]
    if options.frames is not None:
        entries = entries[: options.frames]

    frames_at = bundle.frame_dir(out_dir, scene_dir.name)
    clip_name = frames_at.name
    frames: list[str] = []
    counts: list[int] = []
    lower = np.full(3, np.inf)
    upper = np.full(3, -np.inf)

    for entry in entries:
        index = entry.get("frame_idx", len(frames))
        payload = torch.load(scene_dir / entry["file"], weights_only=False, map_location="cpu")
        xyz = payload["xyz"].float().numpy()
        path = _write_frame(
            frames_at / f"frame_{index:04d}", options,
            xyz=xyz,
            scale_raw=payload["scale_raw"].float().numpy(),
            rot_raw=payload["rot_raw"].float().numpy(),
            opacity_raw=payload["opacity_raw"].float().numpy(),
            sh_dc=ply.rgb_to_sh_dc(payload["rgb"].float().clamp(0, 1).numpy()),
        )
        frames.append(str(path.relative_to(out_dir)))
        counts.append(int(xyz.shape[0]))
        lower = np.minimum(lower, xyz.min(axis=0))
        upper = np.maximum(upper, xyz.max(axis=0))
        print(
            f"      {clip_name} frame {index:04d}  {xyz.shape[0]:7d} gaussians  "
            f"{path.stat().st_size / 1e6:5.2f} MB",
            flush=True,
        )

    colour = scene.get("colour", {})
    return bundle.Clip(
        name=clip_name,
        representation="gaussians",
        scene=clip_name,
        method=name,
        frames=frames,
        counts=counts,
        bounds_min=lower.tolist(),
        bounds_max=upper.tolist(),
        notes=[
            "colour baked by orbitvega.scene_export at azimuth "
            f"{colour.get('bake_azimuth_deg', '?')}° and frozen",
            f"layout={scene.get('layout')}: "
            + ", ".join(entry["name"] for entry in scene.get("objects", [])),
            *_format_notes(options),
        ],
        detail={"source": str(scene_dir), "scene_manifest": scene.get("layout")},
    )


def build_clips(
    source: Path | str, out_dir: Path | str, options: VegaExportOptions | None = None
) -> tuple[str, list[bundle.Clip], dict[str, Any]]:
    """Write one Vega run's frames into ``out_dir`` and describe them.

    Separate from :func:`export` so several sources can be combined into one
    bundle -- a Vega catalog next to a ReRF run is exactly the comparison this
    is for, and it needs one index over both.
    """
    options = options or VegaExportOptions()
    source = Path(source).expanduser().resolve()
    out_dir = Path(out_dir).expanduser().resolve()
    found = detect(source)

    if found.kind is Kind.VEGA_SCENE_EXPORT:
        clips = [_export_scene(source, out_dir, options)]
        title = f"Vega — {source.name} (merged scene)"
    elif found.kind in (Kind.VEGA_CATALOG, Kind.VEGA_BITSTREAM):
        entries = objects_in(source)
        if options.objects:
            wanted = set(options.objects)
            missing = wanted - {entry["name"] for entry in entries}
            if missing:
                raise ValueError(
                    f"{source} has no object(s) {', '.join(sorted(missing))}; it has "
                    + ", ".join(entry["name"] for entry in entries)
                )
            entries = [entry for entry in entries if entry["name"] in wanted]
        clips = [
            _export_bitstream(
                (source / entry["dir"]).resolve(), out_dir, entry["name"], options
            )
            for entry in entries
        ]
        title = "Vega — " + ", ".join(clip.name for clip in clips)
    else:
        raise ValueError(
            f"{source} is {found.kind.value}, not a Vega output; expected a "
            "catalog directory, one object's bitstream, or a scene_export directory"
        )

    detail = {
        "baseline": "vega",
        "bake_azimuth_deg": options.bake_azimuth_deg,
        "bake_elevation": options.bake_elevation,
    }
    return title, clips, detail


def export(source: Path | str, out_dir: Path | str, options: VegaExportOptions | None = None) -> Path:
    """Export a Vega run at ``source`` into a viewable bundle at ``out_dir``.

    ``source`` may be a catalog directory (every object, or the subset named in
    ``options.objects``), one object's bitstream directory, or an
    `orbitvega.scene_export` directory.
    """
    options = options or VegaExportOptions()
    out_dir = Path(out_dir).expanduser().resolve()
    title, clips, detail = build_clips(source, out_dir, options)
    bundle.write(out_dir, title=title, source=source, clips=clips, fps=options.fps, detail=detail)
    return out_dir
