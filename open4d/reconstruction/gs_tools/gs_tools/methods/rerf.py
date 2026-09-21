"""ReRF, made viewable -- by bundling renders, not by producing them.

ReRF stores a neural volume, so unlike every other method in this module its
output is not Gaussians and cannot be turned into a PLY. What a compressed
sequence holds is a DCT-coded, arithmetic-coded feature voxel grid
(`feature_<frame>_<quality>.rerf*`), an occupancy mask, per-frame motion
vectors, a PCA basis per P-frame, and the shared colour MLP. The only decoder
for that is ReRF's, and it only runs under Python 3.8 -- its entropy coder
``ac_dc/`` ships as a CPython 3.8 binary with no sources.

**Rendering moved out of this module.** It used to shell out to upstream's
``rerf_render.py`` through a wrapper in the vendored tree. That is now
`rerf_stream.export`, in ``open4d/reconstruction/rerf``, which does the job
better in three ways that matter:

* it renders at the **corpus's own intrinsics**, where upstream's loader
  letterboxes 4:3 footage into 16:9 -- the pose was right and the framing was
  not, so a render could not be compared pixel-for-pixel against the
  photograph beside it;
* it can write **quality rungs** as a clip's variants, which is what lets
  anything downstream choose a rate; and
* it emits the **capture rig** and marks what each clip depicts, so depth maps
  are not scored against colour photographs.

So this module keeps what it is still the right home for -- reading a
bitstream's codec configuration, and turning an image sequence that already
exists into bundle clips -- and points at `rerf_stream` for the rest.
:func:`build_clips` raises with that instruction rather than rendering.

What is inferred rather than asked for is the codec configuration, because
getting it wrong produces a silently wrong decode: upstream's README requires
``--pca``/``--pca_chs``/``--group_size`` to match between compress and render,
and nothing in the bitstream forces the issue. :func:`bitstream_info` reads it
back off the headers instead -- ``codec/compress.py`` writes one header per
frame whose entry count and channel split *are* the PCA configuration, and
whose single-entry frames are exactly the key frames.

Runs anywhere: nothing here needs a GPU or Python 3.8 any more.
"""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from .. import paths
from streamer import bundle
from ..outputs import Kind, detect

name = "rerf"
#: The tree ReRF is vendored in, for `gs_tools.env`'s provenance record. It is
#: no longer executed from here -- see the module docstring -- but a manifest
#: should still say which commit of upstream the frames came from.
upstream = "rerf"

#: Where rendering lives now, named in the error `build_clips` raises.
RENDERER = "rerf_stream.export"


@dataclass
class RerfRenderOptions:
    """What bundling a ReRF output needs beyond the paths.

    Named for what it used to do. Kept as the name because `gs_tools.cli`
    builds one for every method and the shape is part of that contract; the
    render-only fields are gone, since nothing here renders.
    """

    #: Frames to bundle. None takes every frame present.
    frames: int | None = None
    #: Bundle ReRF's depth maps alongside the colour frames.
    depth: bool = True
    #: Which bitstream in a run to read the codec configuration from, by
    #: directory name. None reads whichever one is present.
    bitstream: str | None = None
    #: Override the inferred codec configuration. None means infer.
    pca: bool | None = None
    pca_chs: tuple[int, ...] | None = None
    group_size: int | None = None
    #: ReRF config; defaults to the `config.py` in the run directory.
    config: Path | None = None
    fps: int = 30
    dry_run: bool = False
    extra: dict[str, Any] = field(default_factory=dict)


def bitstream_info(rerf_dir: Path | str) -> dict[str, Any]:
    """Read the codec configuration back off a ReRF bitstream's headers.

    `codec/compress.py` writes, per frame, either one header covering every
    feature channel (a key frame, or PCA off) or two headers whose channel
    counts are the PCA split -- the first at the requested quality and the
    second one step below it. Both facts are recoverable, so neither has to be
    remembered from whatever command produced the directory.
    """
    rerf_dir = Path(rerf_dir).expanduser().resolve()
    kwargs_path = rerf_dir / "model_kwargs.json"
    if not kwargs_path.is_file():
        raise FileNotFoundError(f"{rerf_dir} has no model_kwargs.json; not a ReRF bitstream")
    model_kwargs = json.loads(kwargs_path.read_text())

    header_paths = sorted(
        (p for p in rerf_dir.glob("header_*.json") if re.fullmatch(r"header_\d+\.json", p.name)),
        key=lambda p: int(p.stem.split("_")[1]),
    )
    if not header_paths:
        raise FileNotFoundError(f"{rerf_dir} has no header_*.json; nothing was compressed")

    key_frames: list[int] = []
    pca_chs: tuple[int, ...] | None = None
    feature_dim = 0
    qualities: set[int] = set()
    for path in header_paths:
        index = int(path.stem.split("_")[1])
        entries = json.loads(path.read_text())["headers"]
        channels = [int(entry["origin_size"][0]) for entry in entries]
        qualities.update(int(entry["quality"]) for entry in entries)
        feature_dim = max(feature_dim, sum(channels))
        if len(entries) == 1:
            key_frames.append(index)
        elif pca_chs is None:
            cumulative: list[int] = []
            total = 0
            for count in channels:
                total += count
                cumulative.append(total)
            pca_chs = tuple(cumulative)

    frames = len(header_paths)
    # Key frames are `frame_id % group_size == 0`, so consecutive key frames are
    # one group apart; a single key frame means the group spans the sequence.
    group_size = key_frames[1] - key_frames[0] if len(key_frames) > 1 else frames
    grid = json.loads(header_paths[0].read_text())["headers"][0].get("origin_size", [])[1:]

    return {
        "root": rerf_dir,
        "frames": frames,
        "key_frames": key_frames,
        "group_size": group_size,
        "pca": pca_chs is not None,
        "pca_chs": pca_chs or (),
        "feature_dim": feature_dim,
        "quality": sorted(qualities, reverse=True),
        "grid": [int(n) for n in grid],
        "has_rgb_net": (rerf_dir / "rgb_net.tar").is_file(),
        "xyz_min": model_kwargs.get("xyz_min"),
        "xyz_max": model_kwargs.get("xyz_max"),
    }


def _config_datadir(config: Path) -> str | None:
    """``data.datadir`` out of a ReRF config, without importing mmcv."""
    match = re.search(r"^\s*datadir\s*=\s*['\"]([^'\"]*)['\"]", config.read_text(), re.M)
    return match.group(1) if match else None


def scene_name(run_name: str) -> str:
    """The subject a ReRF run reconstructs, from its run directory name.

    Runs are named `g_<object>` after the corpus they were prepared from, and
    the `g_` has to come off for the name to match what Vega and the captured
    views call the same subject -- which is what lets the viewer line them up.
    """
    return run_name[2:] if run_name.startswith("g_") else run_name


def collect(
    image_dir: Path,
    out_dir: Path,
    clip_name: str,
    options: RerfRenderOptions,
    *,
    scene: str | None = None,
    method: str | None = None,
    camera: int | None = None,
    notes: list[str] | None = None,
    detail: dict[str, Any] | None = None,
) -> list[bundle.Clip]:
    """Copy a rendered image sequence into a bundle, colour and depth separately.

    Copied rather than symlinked so the bundle survives being moved or archived;
    ReRF's 360 renders are tens of kilobytes a frame, so the duplication is not
    worth avoiding.
    """
    image_dir = Path(image_dir).expanduser().resolve()
    out_dir = Path(out_dir).expanduser().resolve()
    colour: list[Path] = []
    depth: list[Path] = []
    for path in sorted(image_dir.iterdir()):
        if path.suffix.lower() not in (".jpg", ".jpeg", ".png"):
            continue
        if re.fullmatch(r"\d+", path.stem):
            colour.append(path)
        elif re.fullmatch(r"\d+_depth", path.stem):
            depth.append(path)
    colour.sort(key=lambda p: int(p.stem))
    depth.sort(key=lambda p: int(p.stem.split("_")[0]))
    if not colour:
        raise FileNotFoundError(f"{image_dir} holds no numbered images")

    if options.frames is not None:
        colour = colour[: options.frames]
        depth = depth[: options.frames]

    clips: list[bundle.Clip] = []
    for suffix, sources in (("", colour), ("-depth", depth)):
        if not sources or (suffix and not options.depth):
            continue
        frames_at = bundle.frame_dir(out_dir, f"{clip_name}{suffix}")
        name = frames_at.name
        frames: list[str] = []
        for index, source in enumerate(sources):
            destination = frames_at / f"frame_{index:04d}{source.suffix.lower()}"
            shutil.copyfile(source, destination)
            frames.append(str(destination.relative_to(out_dir)))
        clips.append(
            bundle.Clip(
                name=name,
                representation="pixels",
                scene=scene,
                method=f"{method}-depth" if (suffix and method) else method,
                camera=camera,
                frames=frames,
                notes=list(notes or [])
                + (["ReRF's depth output, not colour"] if suffix else []),
                detail={"source": str(image_dir), **(detail or {})},
            )
        )
        print(f"      {name}: {len(frames)} frames from {image_dir.name}", flush=True)
    return clips


def build_clips(
    source: Path | str, out_dir: Path | str, options: RerfRenderOptions | None = None
) -> tuple[str, list[bundle.Clip], dict[str, Any]]:
    """Render (or reuse) one ReRF output into ``out_dir`` and describe the clips.

    Separate from :func:`export` so several runs can be combined into one
    bundle; see `gs_tools.methods.vega.build_clips`.

    ``source`` may be a run root (every bitstream in it), one bitstream
    directory, or a directory of already-rendered images.
    """
    options = options or RerfRenderOptions()
    source = Path(source).expanduser().resolve()
    out_dir = Path(out_dir).expanduser().resolve()
    found = detect(source)
    clips: list[bundle.Clip] = []
    detail: dict[str, Any] = {"baseline": "rerf"}

    if found.kind is Kind.IMAGE_SEQUENCE:
        clips = collect(
            source,
            out_dir,
            source.name,
            options,
            scene=scene_name(source.parent.name),
            method="rerf",
            notes=["pre-rendered by ReRF; the camera is the one that render swept"],
        )
        title = f"ReRF — {source.name}"
    elif found.kind in (Kind.RERF_BITSTREAM, Kind.RERF_RUN):
        if found.kind is Kind.RERF_BITSTREAM:
            run_root, bitstreams, renders = source.parent, [source], []
        else:
            run_root = source
            available = list(found.detail["bitstreams"])
            renders = list(found.detail["renders"])
            if options.bitstream:
                if options.bitstream not in available:
                    raise ValueError(
                        f"{source} has no bitstream {options.bitstream!r}; it has "
                        + (", ".join(available) or "none")
                    )
                bitstreams = [source / options.bitstream]
                renders = []
            elif renders:
                # Existing renders are named by whoever produced them, so they
                # say which condition they are; a bitstream name plus a render
                # command does not. Prefer the unambiguous evidence.
                bitstreams = []
            elif len(available) == 1:
                bitstreams = [source / available[0]]
            else:
                raise ValueError(
                    f"{source} holds {len(available)} bitstreams "
                    f"({', '.join(available)}) and no render; pass --bitstream to say "
                    "which one's codec configuration to record."
                )
        # Order matters: a bitstream with no render is the case that moved out,
        # and it deserves the instruction rather than "holds nothing".
        if bitstreams:
            # `bitstreams` is non-empty only when there is no render to prefer,
            # so this is exactly the case that moved to `rerf_stream`.
            # Rendering left this module; see the module docstring. Naming the
            # command matters more than usual here, because the alternative is
            # a person concluding ReRF cannot be bundled at all.
            names = ", ".join(path.name for path in bitstreams)
            raise RuntimeError(
                f"{run_root} holds a ReRF bitstream ({names}) and no render. "
                f"Rendering is {RENDERER}, which runs in the Python 3.8 "
                "environment ReRF's entropy coder needs:\n"
                f"  python -m {RENDERER} --config {run_root / 'config.py'} "
                f"--compression-path {bitstreams[0]} \\\n"
                f"      --out ~/rerf-clips --name {run_root.name} "
                f"--scene {scene_name(run_root.name)} --depth --captured\n"
                "  python -m streamer.adopt ~/rerf-clips --bundle <bundle>\n"
                "It renders at the corpus's own intrinsics, can write quality "
                "rungs, and emits the capture rig -- none of which the renderer "
                "this used to call did."
            )

        if not renders:
            raise ValueError(f"{source} holds no ReRF bitstream or render")

        for render_name in renders:
            clips += collect(
                run_root / render_name,
                out_dir,
                f"{run_root.name}-{render_name}",
                options,
                scene=scene_name(run_root.name),
                method="rerf-whitebg" if render_name.endswith("_whitebg") else "rerf",
                notes=[
                    "existing ReRF render, reused as-is — not a free camera: ReRF stores "
                    "a feature voxel grid, not Gaussians",
                    "which bitstream produced it is not recorded in the directory",
                ],
                detail={"render": str(run_root / render_name)},
            )
        title = f"ReRF — {run_root.name}"
    else:
        raise ValueError(
            f"{source} is {found.kind.value}, not a ReRF output; expected a run "
            "root, a bitstream directory, or a rendered image sequence"
        )

    return title, clips, detail


def export(source: Path | str, out_dir: Path | str, options: RerfRenderOptions | None = None) -> Path:
    """Export a ReRF output at ``source`` into a viewable bundle at ``out_dir``."""
    options = options or RerfRenderOptions()
    out_dir = Path(out_dir).expanduser().resolve()
    title, clips, detail = build_clips(source, out_dir, options)
    if options.dry_run and not clips:
        # Nothing was rendered, so there is nothing to index; writing an empty
        # bundle would leave a directory that `view` would then fail on.
        return out_dir
    bundle.write(out_dir, title=title, source=source, clips=clips, fps=options.fps, detail=detail)
    return out_dir
