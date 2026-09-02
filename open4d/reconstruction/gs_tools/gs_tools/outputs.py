"""Recognizing what kind of *output* a directory holds, without opening it fully.

``data/layouts.py`` does this for scenes going in; this does it for results
coming out, and it exists because the four things this module has to view are
four unrelated containers. QUEEN and 3DGStream leave a 3DGS run directory.
Vega leaves per-object ``frame_XXXX.pt`` chunks whose colour is a hash grid,
so nothing can read it but Vega. ReRF leaves a compressed feature voxel grid --
DCT-coded, not Gaussians at all -- which only its own decoder can open, and only
under Python 3.8. Telling them apart from the file names is what lets
``gs-tools view`` take a path and do the right thing with it.

Detection is deliberately cheap: file existence and header-sized reads only, no
torch and no CUDA, so it stays usable on a laptop with nothing installed.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import Any

IMAGE_SUFFIXES = (".jpg", ".jpeg", ".png")

#: The index file ``gs-tools export`` writes at the root of a viewable bundle.
BUNDLE_NAME = "view.json"


class Kind(str, Enum):
    """The output containers ``gs-tools view`` and ``gs-tools export`` accept."""

    #: A bundle this module's own exporter produced: `view.json` + frames.
    BUNDLE = "bundle"
    #: `orbitvega.prepare` output: catalog.json plus one directory per object.
    VEGA_CATALOG = "vega-catalog"
    #: One object's Vega bitstream: manifest.json + color_model.pt + frame_*.pt.
    VEGA_BITSTREAM = "vega-bitstream"
    #: `orbitvega.scene_export` output: the merged scene, colour already baked.
    VEGA_SCENE_EXPORT = "vega-scene-export"
    #: A ReRF training run root: config.py, with the bitstream in a subdirectory.
    RERF_RUN = "rerf-run"
    #: ReRF's compressed bitstream: model_kwargs.json + header_*.json + feature_*.
    RERF_BITSTREAM = "rerf-bitstream"
    #: A 3DGS run in any of the three layouts `gaussian_frames` resolves:
    #: a single frame's `point_cloud/iteration_*/`, 3DGStream's per-frame
    #: `frameNNNNNN/`, or QUEEN's `frames/NNNN/`.
    GAUSSIAN_RUN = "gaussian-run"
    #: The ORBIT Gaussian-training corpus: dataset.json listing every object.
    ORBIT_CORPUS = "orbit-corpus"
    #: One ORBIT object: frame_NNNNNN/ directories with transforms.json + images.
    ORBIT_SCENE = "orbit-scene"
    #: A directory of numbered images -- e.g. ReRF's own `render_360_rerf_N`.
    IMAGE_SEQUENCE = "image-sequence"
    UNKNOWN = "unknown"


@dataclass(frozen=True)
class Detected:
    """What was found at a path, and the few facts a caller needs to act on it."""

    root: Path
    kind: Kind
    detail: dict[str, Any] = field(default_factory=dict)

    @property
    def viewable(self) -> bool:
        """Whether `gs-tools export` has a path for this, not merely recognised it.

        These used to differ: anything but UNKNOWN called itself viewable, so a
        3DGS run passed `inspect` and then failed `export` with "no exporter for
        gaussian-run". A kind with no entry in :data:`EXPORTER_FOR` is detected
        but not yet exportable, and saying so here is the difference between a
        clear refusal and a puzzling one.
        """
        return self.kind is Kind.BUNDLE or self.kind in EXPORTER_FOR


#: Which exporter handles each kind. Names rather than modules because
#: `gs_tools.methods` imports this module, so the modules cannot be imported
#: from here; `gs_tools.cli` maps the names onto them and is checked against
#: this, so the two cannot drift.
EXPORTER_FOR: dict[Kind, str] = {
    Kind.VEGA_CATALOG: "vega",
    Kind.VEGA_BITSTREAM: "vega",
    Kind.VEGA_SCENE_EXPORT: "vega",
    Kind.RERF_RUN: "rerf",
    Kind.RERF_BITSTREAM: "rerf",
    Kind.IMAGE_SEQUENCE: "rerf",
    Kind.ORBIT_CORPUS: "captured",
    Kind.ORBIT_SCENE: "captured",
    Kind.GAUSSIAN_RUN: "gaussian",
}


def _best_iteration_ply(directory: Path) -> Path | None:
    """The trained result in a `point_cloud/iteration_*/` directory.

    The highest iteration, which is the finished model rather than a checkpoint
    on the way to it. `added/point_cloud.ply`, which 3DGStream writes beside the
    frame's own model, is skipped: it holds only that frame's *newly added*
    Gaussians, so treating it as a frame would show a fraction of the scene.
    """
    found = sorted(
        directory.glob("point_cloud/iteration_*/point_cloud.ply"),
        key=lambda path: int(path.parent.name.split("_")[-1]),
    )
    return found[-1] if found else None


def gaussian_frames(root: Path) -> list[tuple[int, Path]]:
    """Ordered ``(frame index, PLY)`` for a 3DGS run, in any of three layouts.

    The layouts are not variations on one convention, they are three unrelated
    ones, so each is matched rather than globbed for generically:

    * ``<run>/frames/NNNN/point_cloud.ply`` -- QUEEN, one directory per frame,
      no iteration level.
    * ``<run>/frameNNNNNN/point_cloud/iteration_N/point_cloud.ply`` --
      3DGStream, per-frame runs each with their own iterations.
    * ``<run>/point_cloud/iteration_N/point_cloud.ply`` -- a single frame, which
      is what a static 3DGS run or 3DGStream's init step produces.

    Frame numbers come from the directory names, so a run whose frames start at
    2 keeps its own numbering instead of being silently renumbered from zero.
    """
    queen = sorted(
        (int(path.parent.name), path)
        for path in root.glob("frames/*/point_cloud.ply")
        if path.parent.name.isdigit()
    )
    if queen:
        return queen

    gstream: list[tuple[int, Path]] = []
    for directory in sorted(root.glob("frame*")):
        if not directory.is_dir():
            continue
        digits = directory.name[len("frame"):]
        ply = _best_iteration_ply(directory) if digits.isdigit() else None
        if ply is not None:
            gstream.append((int(digits), ply))
    if gstream:
        return sorted(gstream)

    single = _best_iteration_ply(root)
    return [(0, single)] if single is not None else []


def _frame_pts(root: Path) -> list[Path]:
    return sorted(
        p for p in root.glob("frame_*.pt") if re.fullmatch(r"frame_\d+\.pt", p.name)
    )


def _images(root: Path) -> list[Path]:
    """Numbered images, excluding ReRF's `NNN_depth.jpg` companions."""
    found = [
        p
        for p in root.iterdir()
        if p.suffix.lower() in IMAGE_SUFFIXES and re.fullmatch(r"\d+", p.stem)
    ]
    return sorted(found, key=lambda p: int(p.stem))


def _rerf_headers(root: Path) -> list[Path]:
    headers = [
        p for p in root.glob("header_*.json") if re.fullmatch(r"header_\d+\.json", p.name)
    ]
    return sorted(headers, key=lambda p: int(p.stem.split("_")[1]))


def _config_value(config: Path, key: str) -> str | None:
    """One top-level string assignment out of a ReRF ``config.py``.

    Read with a regex rather than by importing: the file is an mmcv config, and
    importing it needs mmcv, which lives only in the Python 3.8 ``nevo``
    environment. Detection has to work without that.
    """
    try:
        text = config.read_text()
    except OSError:
        return None
    # Indented on purpose: `datadir` is nested inside `data = dict(...)`.
    match = re.search(rf"^\s*{re.escape(key)}\s*=\s*['\"]([^'\"]*)['\"]", text, re.M)
    return match.group(1) if match else None


def detect(path: Path | str) -> Detected:
    """Identify an output directory without modifying it."""
    root = Path(path).expanduser().resolve()
    if not root.is_dir():
        return Detected(root, Kind.UNKNOWN, {"reason": "not a directory"})

    if (root / BUNDLE_NAME).is_file():
        try:
            index = json.loads((root / BUNDLE_NAME).read_text())
        except (OSError, json.JSONDecodeError) as error:
            return Detected(root, Kind.UNKNOWN, {"reason": f"unreadable bundle: {error}"})
        clips = index.get("clips", [])
        return Detected(
            root,
            Kind.BUNDLE,
            {
                "title": index.get("title"),
                "clips": len(clips),
                "frames": sum(len(clip.get("frames", [])) for clip in clips),
            },
        )

    index = root / "dataset.json"
    if index.is_file():
        try:
            dataset = json.loads(index.read_text())
        except (OSError, json.JSONDecodeError):
            dataset = {}
        if str(dataset.get("format", "")).startswith("orbit-"):
            return Detected(root, Kind.ORBIT_CORPUS, {
                "format": dataset.get("format"),
                "objects": [entry["name"] for entry in dataset.get("objects", [])],
                "frames": dataset.get("frame_limit"),
                "views": dataset.get("views_per_frame"),
            })

    orbit_frames = sorted(
        p for p in root.glob("frame_*") if p.is_dir() and p.name[6:].isdigit()
    )
    if orbit_frames and (orbit_frames[0] / "transforms.json").is_file():
        return Detected(root, Kind.ORBIT_SCENE, {
            "name": root.name,
            "frames": len(orbit_frames),
            "views": len(json.loads((orbit_frames[0] / "transforms.json").read_text())
                         .get("frames", [])),
        })

    if (root / "catalog.json").is_file():
        try:
            catalog = json.loads((root / "catalog.json").read_text())
        except (OSError, json.JSONDecodeError):
            catalog = {}
        objects = catalog.get("objects", [])
        if objects:
            return Detected(
                root,
                Kind.VEGA_CATALOG,
                {
                    "baseline": catalog.get("baseline"),
                    "objects": [entry["name"] for entry in objects],
                    "frames": max((entry.get("frame_count", 0) for entry in objects), default=0),
                },
            )

    if (root / "scene_manifest.json").is_file() and _frame_pts(root):
        try:
            scene = json.loads((root / "scene_manifest.json").read_text())
        except (OSError, json.JSONDecodeError):
            scene = {}
        return Detected(
            root,
            Kind.VEGA_SCENE_EXPORT,
            {
                "frames": len(scene.get("frames") or _frame_pts(root)),
                "layout": scene.get("layout"),
                "objects": [entry["name"] for entry in scene.get("objects", [])],
            },
        )

    if (root / "color_model.pt").is_file() and (root / "manifest.json").is_file():
        try:
            manifest = json.loads((root / "manifest.json").read_text())
        except (OSError, json.JSONDecodeError):
            manifest = {}
        frames = manifest.get("frames") or _frame_pts(root)
        return Detected(root, Kind.VEGA_BITSTREAM, {"frames": len(frames), "name": root.name})

    headers = _rerf_headers(root)
    if headers and (root / "model_kwargs.json").is_file():
        return Detected(
            root,
            Kind.RERF_BITSTREAM,
            {"frames": len(headers), "has_rgb_net": (root / "rgb_net.tar").is_file()},
        )

    if (root / "config.py").is_file():
        bitstreams = sorted(
            child.name
            for child in root.iterdir()
            if child.is_dir() and detect(child).kind is Kind.RERF_BITSTREAM
        )
        renders = sorted(
            child.name
            for child in root.iterdir()
            if child.is_dir() and child.name.startswith("render_") and _images(child)
        )
        if bitstreams or renders:
            return Detected(
                root,
                Kind.RERF_RUN,
                {
                    "expname": _config_value(root / "config.py", "expname") or root.name,
                    "basedir": _config_value(root / "config.py", "basedir"),
                    "datadir": _config_value(root / "config.py", "datadir"),
                    "bitstreams": bitstreams,
                    "renders": renders,
                },
            )

    frames = gaussian_frames(root)
    if frames:
        return Detected(
            root,
            Kind.GAUSSIAN_RUN,
            {
                "frames": len(frames),
                "first_frame": frames[0][0],
                "last_frame": frames[-1][0],
                "iterations": sorted(
                    {
                        int(ply.parent.name.split("_")[-1])
                        for _, ply in frames
                        if ply.parent.name.startswith("iteration_")
                    }
                ),
                "method": (json.loads((root / "manifest.json").read_text()).get("method")
                           if (root / "manifest.json").is_file() else None),
            },
        )

    images = _images(root)
    if images:
        return Detected(root, Kind.IMAGE_SEQUENCE, {"frames": len(images)})

    return Detected(root, Kind.UNKNOWN, {"reason": "no recognized output files"})


def describe(found: Detected) -> str:
    """One-line summary for the CLI."""
    parts = [f"kind={found.kind.value}"]
    for key in ("baseline", "expname", "title", "name", "method", "layout"):
        if found.detail.get(key):
            parts.append(f"{key}={found.detail[key]}")
    if found.detail.get("frames"):
        parts.append(f"frames={found.detail['frames']}")
    if found.detail.get("clips"):
        parts.append(f"clips={found.detail['clips']}")
    for key in ("objects", "bitstreams", "renders", "iterations"):
        values = found.detail.get(key)
        if values:
            shown = ",".join(str(value) for value in values[:6])
            if len(values) > 6:
                shown += f",+{len(values) - 6}"
            parts.append(f"{key}=[{shown}]")
    if found.detail.get("reason"):
        parts.append(found.detail["reason"])
    return "  ".join(parts)
