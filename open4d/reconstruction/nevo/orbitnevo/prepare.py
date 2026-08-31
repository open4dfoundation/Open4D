"""Turn an ORBIT object into a ReRF-trainable NHR corpus.

Why this step exists: NeVo is a *streaming* system layered on ReRF, so it
needs ReRF feature-voxel sequences as input, and ReRF's own dataset is only
released after a signed licence agreement returned to ShanghaiTech -- see
``rerf/DATA.md``. The ORBIT corpus this repo already carries has to stand in,
which is what the NeVo paper itself did for two of its six datasets: "We
render the 8i and V-SENSE datasets' high-quality point clouds to images from
different viewports and use them to train NeRF videos" (section 5.1).

Two sources:

``--source gaussian`` (default)
    ``ORBIT_datasets_gaussian``: the repo's prepared multi-view RGB corpus, 30
    frames x 8 calibrated views per object, already rendered over black with
    exact OPENCV poses in ``transforms.json``. No rendering needed, and it is
    the same input `baselines/Vega` trains on, so a NeVo-vs-Vega comparison is
    over identical pixels.

``--source mesh``
    Rasterise ORBIT's textured OBJ sequences directly with DeltaStream's
    nvdiffrast renderer, on an arbitrary rig. The prepared corpus puts all 8
    of its views on one horizontal ring, which leaves a NeRF free to invent
    geometry above and below the subject; this path can put 48 views on four
    elevations instead. Better training data, but no longer the same pixels
    the other baselines see.

Either way the output is the layout ``rerf/lib/load_NHR.py`` reads -- what
upstream's ``data_util.py`` produces, minus the mp4 round-trip, since we hold
exact calibration already and have nothing to recover from ``CamPose.inf``:

    <out>/image/<frame>/img_%04d.png     RGB over black, one per camera
    <out>/mask/<frame>/img_%04d.png      coverage mask, 0 or 255
    <out>/cams_<frame>.json              per-view extrinsic (4x4) + intrinsic
    <out>/bbox.json                      xyz_min / xyz_max, read by run.py
    <out>/nevo_corpus.json               rig + world->normalised transform

Runs in the repo's main environment (Python 3.10+), *not* the ``nevo`` conda
environment: it uses this repo's 3.10-only type syntax and, for
``--source mesh``, DeltaStream's nvdiffrast rasteriser. Everything downstream
of it runs under ``nevo``. See ``baselines/NeVo/README.md``.
"""
from __future__ import annotations

import argparse
import dataclasses
import json
import os
import sys
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from PIL import Image

MODULE_ROOT = Path(__file__).resolve().parents[1]
if str(MODULE_ROOT) not in sys.path:
    sys.path.insert(0, str(MODULE_ROOT))

from nevo.cameras import NORMALISED_RADIUS, Camera, orbit_rig  # noqa: E402
from orbitnevo.objects import resolve_object_names  # noqa: E402

DEFAULT_GAUSSIAN_ROOT = "/media/frozzzen/DataDrive/ORBIT_datasets_gaussian"
DEFAULT_MESH_ROOT = "/media/frozzzen/DataDrive/ORBIT_datasets"

SILHOUETTE_THRESHOLD = 12
"""Per-channel 0-255 level above which a pixel counts as foreground.

The corpus renders over pure black, so the matte is exact rather than learned;
the threshold only rejects texture-filtering ringing at the silhouette. Same
value `baselines/Vega/vega/datasets/orbit_gaussian.py` carves with, so the two
baselines agree on where the subject ends."""

# dataset -> (file prefix, frame-number format, first frame on disk). Mirrors
# vstream.config.ALL_OBJECTS / scripts/orbit_datasets.sh, kept literal so this
# script does not have to import the server config to learn a filename.
MESH_SEQUENCES = {
    "basketball": ("basketball_player", "fr%04d", 1),
    "dancer": ("dancer", "fr%04d", 1),
    "mitch": ("mitch", "fr%04d", 1),
    "thomas": ("thomas", "fr%04d", 618),
    "UMA0": ("UMA0", "%06d", 900),
    "UMA1": ("UMA1", "%06d", 1800),
    "UMA2": ("UMA2", "%06d", 3900),
    "UMA3": ("UMA3", "%06d", 4900),
    "UMA4": ("UMA4", "%06d", 900),
}
# The Gaussian corpus renamed the UMA objects; everything else matches.
GAUSSIAN_ALIASES = {f"UMA{i}": f"UMA{i}" for i in range(5)}


@dataclass(frozen=True)
class _RendererCamera:
    """Duck-typed stand-in for DeltaStream's ``CameraCalibration``.

    ``gpu_renderer.CudaRgbdRenderer`` only reads ``camera_to_world`` and the
    four intrinsic scalars, so adapting is cheaper than round-tripping our rig
    through that dataclass's validation."""

    camera_to_world: tuple
    fx: float
    fy: float
    cx: float
    cy: float


# ------------------------------------------------------------------- shared
def _write_png(path: Path, array: np.ndarray) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(array).save(path, optimize=False, compress_level=1)


def _cams_json(out_dir: Path, frame: int, cameras: list[Camera],
               centre: np.ndarray, scale: float, holdout: int = -1) -> None:
    """Write the per-frame camera file ReRF trains from.

    ``holdout`` names a camera to omit. ReRF's NHR loader sets train = val =
    test = every entry in this file (``lib/load_NHR.py``'s ``i_split``), so a
    view listed here is a view the model fits; the only way to keep one back
    for evaluation is to leave it out. Its image is still written to disk --
    it is the reference the renders get scored against.
    """
    frames = [
        {
            "file": str(out_dir / "image" / str(frame) / ("img_%04d.png" % camera.camera_id)),
            "mask": str(out_dir / "mask" / str(frame) / ("img_%04d.png" % camera.camera_id)),
            "extrinsic": camera.scaled_translation(centre, scale).tolist(),
            "intrinsic": camera.intrinsic_matrix.tolist(),
        }
        for camera in cameras
        if camera.camera_id != holdout
    ]
    with open(out_dir / ("cams_%d.json" % frame), "w") as handle:
        json.dump({"frames": frames}, handle, indent=1)


def _write_bbox(out_dir: Path, lower: np.ndarray, upper: np.ndarray) -> None:
    with open(out_dir / "bbox.json", "w") as handle:
        json.dump({"xyz_min": lower.tolist(), "xyz_max": upper.tolist()}, handle, indent=1)


def _write_manifest(out_dir: Path, payload: dict) -> None:
    with open(out_dir / "nevo_corpus.json", "w") as handle:
        json.dump(payload, handle, indent=1)
    print(f"wrote {out_dir}/nevo_corpus.json", flush=True)


def bbox_corners(lower: np.ndarray, upper: np.ndarray) -> np.ndarray:
    return np.asarray(
        [
            (lower[0] if x else upper[0], lower[1] if y else upper[1], lower[2] if z else upper[2])
            for x in (0, 1)
            for y in (0, 1)
            for z in (0, 1)
        ]
    )


# ------------------------------------------------------- gaussian corpus path
def _load_transforms(root: Path, obj: str) -> dict:
    path = root / obj / "transforms.json"
    if not path.is_file():
        raise FileNotFoundError(
            f"{path} not found. The Gaussian corpus is one transforms.json per object; "
            f"available: {sorted(p.name for p in root.iterdir() if p.is_dir())}"
        )
    with open(path) as handle:
        return json.load(handle)


def _gaussian_rig(transforms: dict) -> tuple[list[np.ndarray], int]:
    """One c2w per view id, checked to be static across the sequence.

    The corpus rig is fixed (``dataset.json`` describes a single 8-camera set),
    but nothing in the file format enforces it -- and a rig that quietly moved
    would train a NeRF on inconsistent geometry, so verify rather than assume.
    """
    by_view: dict[int, np.ndarray] = {}
    for entry in transforms["frames"]:
        view = int(entry["view_id"])
        c2w = np.asarray(entry["camera_to_world_opencv"], dtype=np.float64)
        if view not in by_view:
            by_view[view] = c2w
        elif not np.allclose(by_view[view], c2w, atol=1e-9):
            raise ValueError(
                f"view {view} moves during the sequence; this corpus is not a static rig"
            )
    views = sorted(by_view)
    if views != list(range(len(views))):
        raise ValueError(f"view ids are not contiguous from 0: {views}")
    return [by_view[view] for view in views], len(views)


def _project(c2w: np.ndarray, intrinsic: np.ndarray, points: np.ndarray) -> np.ndarray:
    world_to_camera = np.linalg.inv(c2w)
    local = points @ world_to_camera[:3, :3].T + world_to_camera[:3, 3]
    if np.any(local[:, 2] <= 1e-6):
        raise ValueError("the bounding box crosses the camera plane")
    return (local / local[:, 2:3]) @ intrinsic.T


def _silhouette_bounds(path: Path) -> tuple[int, int, int, int] | None:
    """Pixel bbox of the non-black pixels in one render, or None if empty."""
    with Image.open(path) as handle:
        pixels = np.asarray(handle.convert("RGB"), dtype=np.uint8).max(axis=2)
    rows = np.flatnonzero(pixels.max(axis=1) > SILHOUETTE_THRESHOLD)
    columns = np.flatnonzero(pixels.max(axis=0) > SILHOUETTE_THRESHOLD)
    if rows.size == 0 or columns.size == 0:
        return None
    return int(columns[0]), int(rows[0]), int(columns[-1]), int(rows[-1])


def _crop_windows(
    root: Path,
    obj: str,
    by_frame: dict,
    frames: int,
    view_count: int,
    source_size: tuple[int, int],
    aspect: float,
    margin: float,
) -> list[tuple[int, int, int, int]]:
    """One static crop window per view, at ``aspect``, framing the subject.

    Measured from the silhouettes rather than by projecting the bounding box.
    The box is a loose cover -- its nearest corner projects to nearly the full
    frame height on a camera 3 m from a 1.9 m subject, which clamps the crop
    back to the whole image and defeats the point. The renders are over pure
    black, so the true silhouette is exact and free to read.

    The window is the union over all frames of the sequence, so it is static:
    the rig stays a fixed rig and the intrinsics stay constant across frames,
    which is what `_gaussian_rig` has already checked for the extrinsics.
    """
    width, height = source_size
    unions: list[list[int] | None] = [None] * view_count
    for frame in range(frames):
        for view in range(view_count):
            found = _silhouette_bounds(root / obj / by_frame[frame][view].lstrip("./"))
            if found is None:
                continue
            if unions[view] is None:
                unions[view] = list(found)
            else:
                current = unions[view]
                current[0] = min(current[0], found[0])
                current[1] = min(current[1], found[1])
                current[2] = max(current[2], found[2])
                current[3] = max(current[3], found[3])
    windows = []
    for view, bounds in enumerate(unions):
        if bounds is None:
            raise ValueError(f"{obj} view {view} is empty in every frame")
        left, top, right, bottom = bounds
        centre_x = (left + right + 1) * 0.5
        centre_y = (top + bottom + 1) * 0.5
        crop_height = max((bottom - top + 1), (right - left + 1) / aspect) * margin
        crop_height = min(crop_height, height, width / aspect)
        crop_width = crop_height * aspect
        origin_x = int(round(min(max(centre_x - crop_width * 0.5, 0.0), width - crop_width)))
        origin_y = int(round(min(max(centre_y - crop_height * 0.5, 0.0), height - crop_height)))
        windows.append((origin_x, origin_y, int(round(crop_width)), int(round(crop_height))))
    return windows


def prepare_gaussian(args, obj: str) -> dict:
    root = Path(args.dataset_root).expanduser().resolve()
    out_dir = Path(args.output_dir).expanduser().resolve() / obj
    transforms = _load_transforms(root, GAUSSIAN_ALIASES.get(obj, obj))

    source_width, source_height = int(transforms["w"]), int(transforms["h"])
    intrinsic = np.asarray(
        (
            (transforms["fl_x"], 0.0, transforms["cx"]),
            (0.0, transforms["fl_y"], transforms["cy"]),
            (0.0, 0.0, 1.0),
        )
    )
    rig, view_count = _gaussian_rig(transforms)
    available = int(transforms["source_frame_count"])
    frames = min(args.frames, available) if args.frames > 0 else available

    lower = np.asarray(transforms["bounds_min"], dtype=np.float64)
    upper = np.asarray(transforms["bounds_max"], dtype=np.float64)
    pad = (upper - lower) * args.bbox_pad
    lower, upper = lower - pad, upper + pad
    centre = (lower + upper) * 0.5
    radius = float(np.mean([np.linalg.norm(c2w[:3, 3] - centre) for c2w in rig]))
    scale = NORMALISED_RADIUS / radius

    by_frame: dict[int, dict[int, str]] = {}
    for entry in transforms["frames"]:
        by_frame.setdefault(int(entry["frame_index"]), {})[int(entry["view_id"])] = entry["file_path"]

    aspect = args.width / args.height
    windows = _crop_windows(
        root, GAUSSIAN_ALIASES.get(obj, obj), by_frame, frames, view_count,
        (source_width, source_height), aspect, args.crop_margin,
    )
    cameras: list[Camera] = []
    for view, c2w in enumerate(rig):
        left, top, crop_width, crop_height = windows[view]
        factor = args.width / crop_width
        cameras.append(
            Camera(
                camera_id=view,
                width=args.width,
                height=args.height,
                fx=float(intrinsic[0, 0]) * factor,
                fy=float(intrinsic[1, 1]) * factor,
                cx=(float(intrinsic[0, 2]) - left + 0.5) * factor - 0.5,
                cy=(float(intrinsic[1, 2]) - top + 0.5) * factor - 0.5,
                c2w=c2w,
            )
        )
    print(
        f"{obj}: {view_count} views, {frames} frames, crop "
        f"{windows[0][2]}x{windows[0][3]} of {source_width}x{source_height} "
        f"-> {args.width}x{args.height}, rig radius {radius:.4f} m",
        flush=True,
    )

    out_dir.mkdir(parents=True, exist_ok=True)
    _write_bbox(out_dir, (lower - centre) * scale, (upper - centre) * scale)

    coverage = []
    resample = Image.LANCZOS if args.width < windows[0][2] else Image.BICUBIC
    for frame in range(frames):
        image_dir = out_dir / "image" / str(frame)
        mask_dir = out_dir / "mask" / str(frame)
        done = image_dir / ("img_%04d.png" % (view_count - 1))
        if done.is_file() and not args.overwrite:
            print(f"frame {frame}: already prepared", flush=True)
            continue
        covered = 0
        for view, (left, top, crop_width, crop_height) in enumerate(windows):
            source = root / GAUSSIAN_ALIASES.get(obj, obj) / by_frame[frame][view].lstrip("./")
            with Image.open(source) as handle:
                cropped = handle.convert("RGB").resize(
                    (args.width, args.height),
                    resample,
                    box=(left, top, left + crop_width, top + crop_height),
                )
            rgb = np.asarray(cropped, dtype=np.uint8)
            mask = rgb.max(axis=2) > SILHOUETTE_THRESHOLD
            # Zero the sub-threshold pixels so the RGB we hand ReRF is exactly
            # the matte's inside: it composites rgb*alpha + (1-alpha) onto
            # white, and leftover dark ringing outside the mask would survive
            # as a grey halo.
            rgb = np.where(mask[..., None], rgb, 0)
            _write_png(image_dir / ("img_%04d.png" % view), rgb)
            _write_png(
                mask_dir / ("img_%04d.png" % view),
                np.repeat(mask[..., None].astype(np.uint8) * 255, 3, axis=2),
            )
            covered += int(mask.sum())
        _cams_json(out_dir, frame, cameras, centre, scale, args.holdout_view)
        fraction = covered / (view_count * args.width * args.height)
        coverage.append(fraction)
        print(f"frame {frame}: {view_count} views, foreground {fraction * 100:.1f}%", flush=True)

    manifest = _manifest(
        args, obj, cameras, centre, scale, lower, upper, frames, coverage,
        source="ORBIT_datasets_gaussian",
        extra={"crop_windows": [list(w) for w in windows],
               "source_size": [source_width, source_height]},
    )
    _write_manifest(out_dir, manifest)
    return manifest


# ----------------------------------------------------------- mesh render path
def obj_paths(mesh_root: Path, obj: str, start_frame: int, frames: int) -> list[Path]:
    if obj not in MESH_SEQUENCES:
        raise ValueError(f"unknown ORBIT object {obj!r}; known: {sorted(MESH_SEQUENCES)}")
    prefix, token, first = MESH_SEQUENCES[obj]
    start = start_frame if start_frame > 0 else first
    if start < first:
        raise ValueError(f"{obj} starts at source frame {first}, not {start}")
    paths = []
    for index in range(frames):
        path = mesh_root / obj / f"{prefix}_{token % (start + index)}.obj"
        if not path.is_file():
            raise FileNotFoundError(f"missing ORBIT frame: {path}")
        paths.append(path)
    return paths


def obj_bounds(path: Path) -> tuple[np.ndarray, np.ndarray]:
    """Bounding box of an OBJ's vertices, without building a mesh.

    Reading the `v ` lines directly is far cheaper than a trimesh load, and the
    sequence-wide bbox is needed before any rendering can start.
    """
    lower = np.full(3, np.inf)
    upper = np.full(3, -np.inf)
    with open(path, "r") as handle:
        for line in handle:
            if not line.startswith("v "):
                continue
            parts = line.split()
            point = np.asarray((float(parts[1]), float(parts[2]), float(parts[3])))
            np.minimum(lower, point, out=lower)
            np.maximum(upper, point, out=upper)
    if not np.all(np.isfinite(lower)):
        raise ValueError(f"no vertices in {path}")
    return lower, upper


def sequence_bounds(paths: list[Path]) -> tuple[np.ndarray, np.ndarray]:
    lower = np.full(3, np.inf)
    upper = np.full(3, -np.inf)
    for path in paths:
        frame_lower, frame_upper = obj_bounds(path)
        np.minimum(lower, frame_lower, out=lower)
        np.maximum(upper, frame_upper, out=upper)
    return lower, upper


def prepare_mesh(args, obj: str) -> dict:
    mesh_root = Path(args.dataset_root).expanduser().resolve()
    out_dir = Path(args.output_dir).expanduser().resolve() / obj
    frames = args.frames if args.frames > 0 else 30
    paths = obj_paths(mesh_root, obj, args.start_frame, frames)

    print(f"{obj}: scanning bounds over {len(paths)} frames", flush=True)
    lower, upper = sequence_bounds(paths)
    pad = (upper - lower) * args.bbox_pad
    lower, upper = lower - pad, upper + pad

    cameras, centre, scale = orbit_rig(
        lower, upper, args.width, args.height,
        azimuths=args.azimuths, elevations=tuple(args.elevations),
        hfov_degrees=args.hfov, margin=args.framing_margin,
    )
    print(f"{obj}: rig of {len(cameras)} cameras, radius scale {scale:.6f}", flush=True)

    out_dir.mkdir(parents=True, exist_ok=True)
    _write_bbox(out_dir, (lower - centre) * scale, (upper - centre) * scale)

    # Imported late: creating the CUDA context before the bounds scan would
    # hold GPU memory doing nothing for a minute.
    from baselines.DeltaStream.orbitstream.converter import load_textured_mesh
    from baselines.DeltaStream.orbitstream.gpu_renderer import CudaRgbdRenderer

    os.environ.setdefault("TORCH_CUDA_ARCH_LIST", "8.9")
    renderer = CudaRgbdRenderer(
        args.gpu, args.width, args.height, texture_filter="linear", supersample=args.supersample
    )
    # Rasterise in the *normalised* frame: DeltaStream's renderer exports depth
    # as uint16 millimetres and refuses a frame that overflows it, which a
    # millimetre-unit world does immediately. Normalised, the extrinsics we
    # rasterise with are exactly the ones written to cams_<frame>.json.
    renderer_cameras = tuple(
        _RendererCamera(
            tuple(tuple(float(v) for v in row) for row in camera.scaled_translation(centre, scale)),
            camera.fx, camera.fy, camera.cx, camera.cy,
        )
        for camera in cameras
    )

    coverage = []
    for frame, path in enumerate(paths):
        image_dir = out_dir / "image" / str(frame)
        mask_dir = out_dir / "mask" / str(frame)
        done = image_dir / ("img_%04d.png" % cameras[-1].camera_id)
        if done.is_file() and not args.overwrite:
            print(f"frame {frame}: already rendered", flush=True)
            continue
        mesh = load_textured_mesh(path)
        mesh = dataclasses.replace(
            mesh, vertices=(np.asarray(mesh.vertices, np.float64) - centre) * scale
        )
        # Chunked: all views at supersampled resolution at once would want many
        # GB of a card this box shares with other work.
        covered = 0
        total = 0
        for begin in range(0, len(cameras), args.view_chunk):
            chunk = cameras[begin : begin + args.view_chunk]
            result = renderer.render(mesh, renderer_cameras[begin : begin + args.view_chunk])
            mask = result.depth_mm > 0
            if result.rgb.shape[0] != len(chunk):
                raise RuntimeError("renderer returned the wrong number of views")
            for offset, camera in enumerate(chunk):
                _write_png(image_dir / ("img_%04d.png" % camera.camera_id), result.rgb[offset])
                _write_png(
                    mask_dir / ("img_%04d.png" % camera.camera_id),
                    np.repeat(mask[offset][..., None].astype(np.uint8) * 255, 3, axis=2),
                )
            covered += int(mask.sum())
            total += int(mask.size)
        _cams_json(out_dir, frame, cameras, centre, scale, args.holdout_view)
        coverage.append(covered / total)
        print(f"frame {frame}: {path.name} -> {len(cameras)} views, "
              f"foreground {coverage[-1] * 100:.1f}%", flush=True)

    manifest = _manifest(
        args, obj, cameras, centre, scale, lower, upper, len(paths), coverage,
        source="ORBIT_datasets",
        extra={"azimuths": args.azimuths, "elevations": list(args.elevations),
               "hfov_degrees": args.hfov, "supersample": args.supersample},
    )
    _write_manifest(out_dir, manifest)
    return manifest


def _manifest(args, obj, cameras, centre, scale, lower, upper, frames, coverage,
              *, source: str, extra: dict) -> dict:
    payload = {
        "format": "nevo-rerf-nhr-corpus",
        "version": 1,
        "source": source,
        "object": obj,
        "frames": frames,
        "width": args.width,
        "height": args.height,
        "world_centre": centre.tolist(),
        "world_scale": scale,
        "world_bounds_min": lower.tolist(),
        "world_bounds_max": upper.tolist(),
        "xyz_min": ((lower - centre) * scale).tolist(),
        "xyz_max": ((upper - centre) * scale).tolist(),
        "cameras": [
            {
                "camera_id": camera.camera_id,
                "fx": camera.fx, "fy": camera.fy, "cx": camera.cx, "cy": camera.cy,
                "c2w_world": camera.c2w.tolist(),
                "c2w_normalised": camera.scaled_translation(centre, scale).tolist(),
            }
            for camera in cameras
        ],
        "mean_foreground_fraction": float(np.mean(coverage)) if coverage else None,
        "holdout_view": args.holdout_view if args.holdout_view >= 0 else None,
        "training_views": [
            camera.camera_id for camera in cameras if camera.camera_id != args.holdout_view
        ],
    }
    payload.update(extra)
    return payload


# ---------------------------------------------------------------------- CLI
def prepare(args) -> list[dict]:
    objects = resolve_object_names(args.objects)
    worker = prepare_gaussian if args.source == "gaussian" else prepare_mesh
    return [worker(args, obj) for obj in objects]


def parse_args(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--source", default="gaussian", choices=("gaussian", "mesh"))
    parser.add_argument("--dataset-root", default=None,
                        help="defaults to the root for --source")
    parser.add_argument("--objects", nargs="*", default=(),
                        help="ORBIT objects; defaults to the scene in vstream/config.py")
    parser.add_argument("--output-dir", required=True,
                        help="one subdirectory is written per object")
    parser.add_argument("--frames", type=int, default=0, help="0 = every frame available")
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=960,
                        help="4:3 keeps ReRF's half_res crop a no-op (it crops h x 4h/3)")
    parser.add_argument("--bbox-pad", type=float, default=0.04)
    parser.add_argument("--overwrite", action="store_true")
    parser.add_argument("--holdout-view", type=int, default=-1,
                        help="camera id kept out of training and reserved as the evaluation "
                             "reference; -1 trains on every view")
    # gaussian only
    parser.add_argument("--crop-margin", type=float, default=1.15,
                        help="how much room to leave around the subject's projected bbox")
    # mesh only
    parser.add_argument("--start-frame", type=int, default=0, help="0 = the object's first")
    parser.add_argument("--azimuths", type=int, default=12)
    parser.add_argument("--elevations", type=float, nargs="+", default=[-15.0, 5.0, 25.0, 45.0])
    parser.add_argument("--hfov", type=float, default=60.0)
    parser.add_argument("--framing-margin", type=float, default=1.12)
    parser.add_argument("--supersample", type=int, default=2, choices=(1, 2, 4))
    parser.add_argument("--gpu", type=int, default=0)
    parser.add_argument("--view-chunk", type=int, default=6,
                        help="views rasterised per renderer call; bounds GPU memory")
    args = parser.parse_args(argv)
    if args.dataset_root is None:
        args.dataset_root = (
            DEFAULT_GAUSSIAN_ROOT if args.source == "gaussian" else DEFAULT_MESH_ROOT
        )
    return args


def main(argv=None) -> int:
    prepare(parse_args(argv))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
