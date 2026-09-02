"""ReRF, made viewable -- by running its own renderer, not by faking Gaussians.

ReRF stores a neural volume, so unlike every other method in this module its
output is not Gaussians and cannot be turned into a PLY. What a compressed
sequence holds is a DCT-coded, arithmetic-coded feature voxel grid
(`feature_<frame>_<quality>.rerf*`), an occupancy mask, per-frame motion
vectors, a PCA basis per P-frame, and the shared colour MLP. The only decoder
for that is ReRF's, and it only runs under Python 3.8 -- its entropy coder
`ac_dc/` ships as a CPython 3.8 binary with no sources. So this adapter renders
the bitstream with `rerf_render.py` and bundles the resulting images.

That makes a ReRF clip an image sequence rather than a free camera, which is the
honest shape of the thing: the camera is whichever orbit `--render_360` swept,
and re-aiming it means re-rendering. Turning occupied voxels into one Gaussian
each would give a free camera, but it would be a proxy whose appearance is not
what ReRF reconstructs, so it is deliberately not what this does.

What *is* inferred rather than asked for is the codec configuration, because
getting it wrong produces a silently wrong decode: upstream's README requires
``--pca``/``--pca_chs``/``--group_size`` to match between compress and render,
and nothing in the bitstream forces the issue. :func:`bitstream_info` reads it
back off the headers instead -- `codec/compress.py` writes one header per frame
whose entry count and channel split *are* the PCA configuration, and whose
single-entry frames are exactly the key frames.

The vendored ReRF tree lives under ``open4d/reconstruction/nevo/rerf`` because
that is the baseline that vendored it, and its environment shim and CLI wrapper
(`nevo.rerf_env`, `orbitnevo.rerf_cli`) live beside it. This adapter calls that
wrapper and is otherwise unconcerned with NeVo: what it renders is ReRF.

Runs the render under Python 3.8; everything else here runs anywhere.
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
#: The directory `paths.upstream` resolves -- the tree ReRF is vendored in.
upstream = "nevo"

#: Set this to the interpreter of the Python 3.8 environment that can import
#: ReRF's entropy coder, when it is not a sibling environment of this one.
PYTHON_ENV_VAR = "OPEN4D_RERF_PYTHON"

#: The conda environment `nevo/README.md` prescribes for anything that touches a
#: ReRF model. Named after the baseline that created it, not after ReRF.
DEFAULT_ENV_NAME = "nevo"


@dataclass
class RerfRenderOptions:
    """What the render needs beyond the bitstream and output paths."""

    #: Frames to render. None takes every frame the bitstream carries. Upstream
    #: pulls the decode stream sequentially, so asking for more than were
    #: compressed exhausts the iterator mid-render.
    frames: int | None = None
    #: Re-render even when a matching `render_360_rerf_N` directory already exists.
    force: bool = False
    #: Render at all. False reuses an existing render and fails if there is none,
    #: which is the safe default for a verb that would otherwise take minutes of
    #: GPU time without being asked.
    render: bool = False
    #: Bundle ReRF's depth maps alongside the colour frames.
    depth: bool = True
    #: Which bitstream in a run to render, by directory name. None bundles the
    #: renders already present, which is unambiguous; rendering is not, because
    #: `rerf_render.py` names its output `render_360_rerf_<n>` from the frame
    #: count alone, so a run holding two bitstreams has one name for both.
    bitstream: str | None = None
    #: Override the inferred codec configuration. None means infer.
    pca: bool | None = None
    pca_chs: tuple[int, ...] | None = None
    group_size: int | None = None
    #: The Python 3.8 interpreter to run the render with. None resolves it.
    python: Path | None = None
    #: ReRF config; defaults to the `config.py` in the run directory.
    config: Path | None = None
    #: Render at the capture rig's own cameras instead of upstream's synthetic
    #: orbit, so the result can be compared against another method -- and against
    #: the photograph -- at the same pose. Empty means upstream's orbit.
    rig_views: tuple[int, ...] = ()
    fps: int = 30
    dry_run: bool = False
    passthrough: tuple[str, ...] = ()
    extra: dict[str, Any] = field(default_factory=dict)


def _conda_env_roots() -> list[Path]:
    """Directories that may hold sibling conda environments.

    Two cases, and which one applies depends on what is active: inside an
    environment `sys.prefix` is `<conda>/envs/<name>`, so the siblings are its
    parent; in the base environment `sys.prefix` is the conda root itself, so
    they are under `envs/`. `CONDA_EXE` covers being run from neither.
    """
    roots = [Path(sys.prefix).parent, Path(sys.prefix) / "envs"]
    conda_exe = os.environ.get("CONDA_EXE")
    if conda_exe:
        roots.append(Path(conda_exe).resolve().parent.parent / "envs")
    seen: list[Path] = []
    for root in roots:
        if root not in seen:
            seen.append(root)
    return seen


def rerf_python(explicit: Path | None = None) -> Path:
    """The interpreter to run ReRF under.

    ReRF cannot run in this module's environment, so this is a subprocess
    boundary that has to be resolved by convention. In order: an explicit path,
    ``OPEN4D_RERF_PYTHON``, a sibling conda environment named ``nevo``, and
    finally this interpreter -- which is right when the caller already activated
    the environment, and produces a clear ImportError from ReRF when it is not.
    """
    if explicit:
        return Path(explicit).expanduser().resolve()
    override = os.environ.get(PYTHON_ENV_VAR)
    if override:
        return Path(override).expanduser().resolve()
    for root in _conda_env_roots():
        candidate = root / DEFAULT_ENV_NAME / "bin" / "python"
        if candidate.is_file():
            return candidate.resolve()
    return Path(sys.executable)


def _python_version(interpreter: Path) -> str | None:
    try:
        out = subprocess.run(
            [str(interpreter), "-c", "import sys; print('%d.%d' % sys.version_info[:2])"],
            capture_output=True,
            text=True,
            timeout=60,
            check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    return out.stdout.strip() or None


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


def render_command(
    config: Path,
    rerf_dir: Path,
    frames: int,
    info: dict[str, Any],
    options: RerfRenderOptions,
) -> list[str]:
    """The `rerf_render.py` invocation, run through `orbitnevo.rerf_cli`.

    Via `rerf_cli` rather than directly because ReRF cannot simply be executed:
    its entropy coder has to be ``dlopen``ed before import, the CWD has to be
    the ReRF root while `codec.quant` reads a relative path, and two upstream
    calls need patching against modern numpy and imageio. `nevo.rerf_env` does
    all of that, and `rerf_cli` is its command-line front.
    """
    pca = info["pca"] if options.pca is None else options.pca
    pca_chs = options.pca_chs or info["pca_chs"]
    group_size = options.group_size if options.group_size is not None else info["group_size"]

    command = [
        str(rerf_python(options.python)),
        "-m",
        "orbitnevo.rerf_cli",
        "rerf_render.py",
        "--config",
        str(config.resolve()),
        "--compression_path",
        str(rerf_dir.resolve()),
        "--render_360",
        str(frames),
        # Upstream defaults `--frame_num` to 20000 and derives group_size from
        # it, so leaving it out silently changes which frames decode as keys.
        "--frame_num",
        str(info["frames"]),
        "--group_size",
        str(group_size),
    ]
    if pca:
        command += ["--pca", "--pca_chs", ",".join(str(n) for n in pca_chs)]
    return command + list(options.passthrough)


def render_dir(run_root: Path, frames: int) -> Path:
    """Where `rerf_render.py` writes -- `<basedir>/<expname>/render_360_rerf_<n>`.

    Which is the run root, since that is what `<basedir>/<expname>` resolves to
    for the config that lives in it. Note what is *not* in that name: which
    bitstream was decoded. Two bitstreams in one run therefore render to the
    same directory, and the second overwrites the first.
    """
    return run_root / f"render_360_rerf_{frames}"


def render(run_root: Path, rerf_dir: Path, options: RerfRenderOptions) -> Path:
    """Render a ReRF bitstream to images, or reuse a matching existing render."""
    run_root = Path(run_root).expanduser().resolve()
    rerf_dir = Path(rerf_dir).expanduser().resolve()
    info = bitstream_info(rerf_dir)
    frames = options.frames or info["frames"]
    if frames > info["frames"]:
        raise ValueError(
            f"{rerf_dir} carries {info['frames']} compressed frames; cannot render {frames}. "
            "Upstream pulls the decode stream sequentially, so the iterator would run dry."
        )

    target = render_dir(run_root, frames)
    existing = detect(target)
    if existing.kind is Kind.IMAGE_SEQUENCE and not options.force:
        print(f"reusing {target} ({existing.detail['frames']} frames)")
        return target

    config = Path(options.config).expanduser().resolve() if options.config else run_root / "config.py"
    if not config.is_file():
        raise FileNotFoundError(f"no ReRF config at {config}; pass --config")
    datadir = _config_datadir(config)
    if datadir and not Path(datadir).expanduser().is_dir():
        raise FileNotFoundError(
            f"{config.name} points at corpus {datadir}, which is missing. "
            "rerf_render.py reads the rig's cameras and bbox.json from there, so "
            "the render cannot be reproduced without it."
        )

    command = render_command(config, rerf_dir, frames, info, options)
    tree = paths.upstream("nevo")
    if not tree.exists():
        raise FileNotFoundError(f"{tree} is missing")

    if not options.render:
        raise RuntimeError(
            f"no render at {target}, and --render was not given. To produce it:\n"
            f"  (cd {tree} && {' '.join(command)})"
        )

    if options.dry_run:
        print(f"(cd {tree} && {' '.join(command)})")
        return target

    interpreter = Path(command[0])
    version = _python_version(interpreter)
    if version and version != "3.8":
        print(
            f"warning: {interpreter} is Python {version}; ReRF's entropy coder "
            f"ships only as a 3.8 binary. Set {PYTHON_ENV_VAR} if this is wrong."
        )

    child_env = dict(os.environ, PYTHONPATH=str(tree))
    print(f"$ (cd {tree} && {' '.join(command)})", flush=True)
    started = time.monotonic()
    status = subprocess.run(command, cwd=tree, env=child_env, check=False).returncode
    elapsed = time.monotonic() - started
    if status != 0:
        raise RuntimeError(f"rerf_render.py exited {status} after {elapsed:.1f}s")
    print(f"rendered {frames} frames in {elapsed:.1f}s -> {target}")
    return target


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
                    f"{source} holds {len(available)} bitstreams ({', '.join(available)}) "
                    "and no render. rerf_render.py names its output from the frame count "
                    "alone, so pass --bitstream to say which one to render."
                )
        if not bitstreams and not renders:
            raise ValueError(f"{source} holds no ReRF bitstream to render")

        for rerf_dir in bitstreams:
            info = bitstream_info(rerf_dir)
            frames = options.frames or info["frames"]
            method = ("rerf" if rerf_dir.name == "rerf"
                      else f"rerf-{rerf_dir.name.replace('rerf_', '')}")
            if options.rig_views:
                # The rig cameras, so this can sit beside another method and the
                # photograph at the same pose. Upstream's orbit cannot.
                images, views, times = render_at_rig(run_root, rerf_dir, options)
                if options.dry_run:
                    continue
                clips += collect_rig(
                    images, out_dir, f"{run_root.name}-{rerf_dir.name}", views, times, options,
                    scene=scene_name(run_root.name),
                    method=method,
                    notes=[
                        "ReRF volume render at the scene's own capture camera — the same pose "
                        "the photograph and every other method use here",
                        "framing differs from the captured pane: ReRF renders at its training "
                        "images' size and intrinsics (1920x1080 here) while the corpus captured "
                        "4:3, so the pose matches but the crop does not — compare content, not "
                        "pixel positions",
                        f"codec: pca={info['pca']} pca_chs={','.join(str(n) for n in info['pca_chs']) or '-'} "
                        f"group_size={info['group_size']} quality={info['quality']}",
                    ],
                    detail={"bitstream": str(rerf_dir)},
                )
                detail.setdefault("bitstreams", {})[rerf_dir.name] = {
                    k: info[k] for k in ("frames", "group_size", "pca", "pca_chs", "quality", "grid")
                }
                continue
            if len(bitstreams) + len(renders) > 1 or (
                found.kind is Kind.RERF_RUN and len(found.detail["bitstreams"]) > 1
            ):
                print(
                    f"note: {run_root.name} holds several bitstreams; rerf_render.py writes "
                    f"all of them to {render_dir(run_root, frames).name}. The bundle keeps "
                    f"them apart, the run directory does not."
                )
            images = render(run_root, rerf_dir, options)
            if options.dry_run:
                continue
            clips += collect(
                images,
                out_dir,
                f"{run_root.name}-{rerf_dir.name}",
                options,
                scene=scene_name(run_root.name),
                method=method,
                notes=[
                    f"ReRF volume render, {frames}-frame 360° orbit — not a free camera: "
                    "ReRF stores a feature voxel grid, not Gaussians",
                    f"codec: pca={info['pca']} pca_chs={','.join(str(n) for n in info['pca_chs']) or '-'} "
                    f"group_size={info['group_size']} quality={info['quality']}",
                ],
                detail={"bitstream": str(rerf_dir), "codec": {
                    k: info[k] for k in ("frames", "group_size", "pca", "pca_chs", "feature_dim",
                                         "quality", "grid", "key_frames")
                }},
            )
            detail.setdefault("bitstreams", {})[rerf_dir.name] = {
                k: info[k] for k in ("frames", "group_size", "pca", "pca_chs", "quality", "grid")
            }

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
                    "which bitstream produced it is not recorded in the directory; "
                    "re-run with --bitstream --render to render one deliberately",
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




# ---------------------------------------------------------------- rig render ---
#: The runner that patches and executes `rerf_render.py`; see its docstring.
RIG_RUNNER = Path(__file__).resolve().parent / "_rerf_rig_render.py"


def rig_render_dir(run_root: Path, views: tuple[int, ...], frames: int) -> Path:
    """Where a rig render goes.

    Named for what it is, unlike upstream's `render_360_rerf_<n>`, which names
    only the frame count and so collides between bitstreams and between paths.
    """
    tag = "-".join(str(v) for v in views)
    return run_root / f"render_rig_v{tag}_f{frames}"


def render_at_rig(
    run_root: Path,
    rerf_dir: Path,
    options: RerfRenderOptions,
) -> tuple[Path, list[int], list[int]]:
    """Render the bitstream at the rig cameras. Returns (dir, views, times).

    Images land as `NNN.jpg` in timestep-major, view-minor order -- the order the
    plan asks for them in -- which is what :func:`collect_rig` unpacks.
    """
    run_root = Path(run_root).expanduser().resolve()
    rerf_dir = Path(rerf_dir).expanduser().resolve()
    info = bitstream_info(rerf_dir)
    times = list(range(options.frames or info["frames"]))
    if len(times) > info["frames"]:
        raise ValueError(
            f"{rerf_dir} carries {info['frames']} compressed frames; cannot render {len(times)}"
        )
    views = tuple(options.rig_views) or (0,)

    target = rig_render_dir(run_root, views, len(times))
    existing = detect(target)
    if existing.kind is Kind.IMAGE_SEQUENCE and not options.force:
        expected = len(views) * len(times)
        if existing.detail["frames"] == expected:
            print(f"reusing {target} ({expected} images)")
            return target, list(views), times
        print(f"{target} holds {existing.detail['frames']} images, expected {expected}; re-rendering")

    config = Path(options.config).expanduser().resolve() if options.config else run_root / "config.py"
    if not config.is_file():
        raise FileNotFoundError(f"no ReRF config at {config}; pass --config")
    datadir = _config_datadir(config)
    if datadir and not Path(datadir).expanduser().is_dir():
        raise FileNotFoundError(
            f"{config.name} points at corpus {datadir}, which is missing. The rig cameras "
            "are read from it, so there is nothing to render at without it."
        )

    tree = paths.upstream("nevo")
    plan = {
        "views": list(views),
        "times": times,
        "nevo_tree": str(tree),
    }
    target.mkdir(parents=True, exist_ok=True)
    plan_path = target / "plan.json"
    plan_path.write_text(json.dumps(plan, indent=2))

    pca = info["pca"] if options.pca is None else options.pca
    pca_chs = options.pca_chs or info["pca_chs"]
    group_size = options.group_size if options.group_size is not None else info["group_size"]
    command = [
        str(rerf_python(options.python)),
        str(RIG_RUNNER),
        "--config", str(config),
        "--compression_path", str(rerf_dir),
        # Upstream only renders when this is positive; the count it implies is
        # replaced by the plan, but the branch still has to be entered.
        "--render_360", str(len(times)),
        "--frame_num", str(info["frames"]),
        "--group_size", str(group_size),
    ]
    if pca:
        command += ["--pca", "--pca_chs", ",".join(str(n) for n in pca_chs)]
    command += list(options.passthrough)

    if not options.render:
        raise RuntimeError(
            f"no rig render at {target}, and --render was not given. To produce it:\n"
            f"  OPEN4D_RERF_PLAN={plan_path} OPEN4D_RERF_OUT={target} \\\n"
            f"    (cd {tree} && {' '.join(command)})"
        )
    if options.dry_run:
        print(f"OPEN4D_RERF_PLAN={plan_path} OPEN4D_RERF_OUT={target} \\")
        print(f"  (cd {tree} && {' '.join(command)})")
        return target, list(views), times

    child_env = dict(
        os.environ,
        PYTHONPATH=str(tree),
        OPEN4D_RERF_PLAN=str(plan_path),
        OPEN4D_RERF_OUT=str(target),
    )
    print(f"$ (cd {tree} && {' '.join(command)})", flush=True)
    started = time.monotonic()
    status = subprocess.run(command, cwd=tree, env=child_env, check=False).returncode
    elapsed = time.monotonic() - started
    if status != 0:
        raise RuntimeError(f"rig render exited {status} after {elapsed:.1f}s")
    print(f"rendered {len(views)}x{len(times)} images in {elapsed:.1f}s -> {target}")
    return target, list(views), times


def collect_rig(
    image_dir: Path,
    out_dir: Path,
    clip_name: str,
    views: list[int],
    times: list[int],
    options: RerfRenderOptions,
    *,
    scene: str | None = None,
    method: str | None = None,
    notes: list[str] | None = None,
    detail: dict[str, Any] | None = None,
) -> list[bundle.Clip]:
    """Split a rig render into one clip per camera.

    Upstream numbers its output by nothing but position in the render order, so
    the mapping back to (view, timestep) is the plan's ordering and only that:
    timestep-major, view-minor.
    """
    image_dir = Path(image_dir).expanduser().resolve()
    out_dir = Path(out_dir).expanduser().resolve()
    clips: list[bundle.Clip] = []
    for position, view in enumerate(views):
        for suffix, stem in (("", "{:03d}"), ("-depth", "{:03d}_depth")):
            if suffix and not options.depth:
                continue
            frames_at = bundle.frame_dir(out_dir, f"{clip_name}-cam{view:02d}{suffix}")
            written: list[str] = []
            for step in range(len(times)):
                index = step * len(views) + position
                source = image_dir / (stem.format(index) + ".jpg")
                if not source.is_file():
                    raise FileNotFoundError(
                        f"{source} is missing; the render did not produce "
                        f"{len(views) * len(times)} images"
                    )
                destination = frames_at / f"frame_{step:04d}.jpg"
                shutil.copyfile(source, destination)
                written.append(str(destination.relative_to(out_dir)))
            clips.append(
                bundle.Clip(
                    name=frames_at.name,
                    representation="pixels",
                    scene=scene,
                    method=f"{method}-depth" if suffix else method,
                    camera=view,
                    frames=written,
                    notes=list(notes or []),
                    detail={"source": str(image_dir), "view": view, **(detail or {})},
                )
            )
            print(f"      {frames_at.name}: {len(written)} frames", flush=True)
    return clips


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
