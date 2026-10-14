"""Offline data-prep step: encode an ORBIT corpus into a Vega bitstream,
one independently-encoded GOV sequence per scene object.

Two corpora are supported, told apart automatically by what is in
`--dataset-root` (override with `--dataset-format`):

- **gaussian** (`dataset.json`, format `orbit-rgb-gaussian-training`) — the
  multi-view RGB corpus built for Gaussian training: 8 calibrated views per
  frame, no depth, black background. Geometry is recovered by silhouette
  carving; see `vega.datasets.orbit_gaussian`. This is the default, and it is
  the corpus that actually matches what Vega is: a 3D Gaussian Splatting
  system.
- **rgbd** (`manifest.json`) — the older RGBD corpus, whose fused per-frame
  point clouds are converted straight to Gaussians by
  `vega.datasets.orbit`.

Vega's own internal "object-level selective computation" (paper §4.1 —
segmenting a scene into semantically meaningful Gaussian clusters, e.g. a
basketball player's limbs vs. the ball vs. the court) operates *within* each
of the scene objects this harness configures (dancer/basketball/mitch/...,
see `vstream.config.OBJECTS`). It is not exposed at this harness's
`object_id` granularity — from the wire protocol's point of view, Vega
serves one opaque chunk per configured scene object per frame, exactly like
every other baseline here.
"""
from __future__ import annotations

import argparse
import dataclasses
import json
from pathlib import Path

from orbitvega.eval_camera import eval_camera_for_bounds
from vega.bitstream import write_bitstream
from vega.color_encoding import DEFAULT_COLOR_CONFIG
from vega.datasets import orbit as orbit_rgbd
from vega.datasets import orbit_gaussian
from vega.encoder import VegaEncoderConfig, encode_sequence
from vega.segmentation import segment_sequence

DEFAULT_DATASET_ROOT = Path("/media/frozzzen/DataDrive/ORBIT_datasets_gaussian")
RGBD_DATASET_ROOT = Path("/media/frozzzen/DataDrive/ORBIT_datasets_rgbd/level_1")

DEFAULT_K_OBJECTS = 1
"""Sub-objects to segment each ORBIT scene object into (paper §5.1).

1, because in both ORBIT corpora a scene object *is* one object — a single
person, captured alone against an empty background. The paper gets its
sub-objects from Gaussian Grouping (SAM + video tracking), which is not
available here; `vega.segmentation` falls back to k-means over Gaussian
position, whose clusters are arbitrary spatial blobs whose boundaries drift as
the subject moves.

That interacts badly with dynamicity filtering (§5.3), which rebuilds a
residual frame as *key-frame* geometry for clusters judged static plus
*current-frame* geometry for clusters judged dynamic. When a limb moves, its
old location can sit in a cluster judged static (so the key frame's limb is
drawn) while its new location falls in a cluster judged dynamic (so the
current limb is drawn too) — both render, and the subject grows extra arms.
Measured on basketball at k=10, residual frames inherited 46% of their
Gaussians from the key frame; at k=1 reconstruction is exact for every frame.

Raise this only with real per-Gaussian instance labels, which
`vega.segmentation` already accepts as a pass-through."""


def detect_format(dataset_root: Path) -> str:
    """"gaussian" or "rgbd", from which index file the tree carries."""
    dataset_root = Path(dataset_root)
    if (dataset_root / "dataset.json").is_file():
        return "gaussian"
    if (dataset_root / "manifest.json").is_file():
        return "rgbd"
    raise FileNotFoundError(
        f"{dataset_root} has neither dataset.json (gaussian corpus) nor "
        f"manifest.json (rgbd corpus)")


@dataclasses.dataclass
class SceneObject:
    """The per-object metadata prepare needs, from either corpus."""
    name: str
    object_id: int
    bounds_min: list[float]
    bounds_max: list[float]
    n_available_frames: int
    source_start_frame: int


def _harness_object_id(name: str, fallback: int) -> int:
    """`vstream.config`'s object index, so Vega's catalog agrees with the rest
    of the harness; positional index if that config is not importable."""
    try:
        from vstream.config import OBJ_TO_IDX
        return int(OBJ_TO_IDX.get(name, fallback))
    except Exception:
        return fallback


def list_objects(dataset_root: Path, fmt: str) -> list[SceneObject]:
    if fmt == "gaussian":
        meta = orbit_gaussian.load_dataset_meta(dataset_root)
        objects = []
        for i, entry in enumerate(meta["objects"]):
            transforms = orbit_gaussian.load_object_transforms(dataset_root, entry["name"])
            objects.append(SceneObject(
                name=entry["name"],
                object_id=_harness_object_id(entry["name"], i),
                bounds_min=list(transforms["bounds_min"]),
                bounds_max=list(transforms["bounds_max"]),
                n_available_frames=len(orbit_gaussian.group_frames(transforms)),
                source_start_frame=int(entry.get("source_start_frame", 1)),
            ))
        return objects

    from baselines.DeltaStream.orbitstream.manifest import DatasetManifest
    manifest = DatasetManifest.load(Path(dataset_root) / "manifest.json")
    return [SceneObject(
        name=obj.name,
        object_id=obj.object_id,
        bounds_min=list(obj.bounds_min),
        bounds_max=list(obj.bounds_max),
        n_available_frames=obj.source_frame_count,
        source_start_frame=obj.source_start_frame,
    ) for obj in manifest.objects]


def pick_frames(obj: SceneObject, fmt: str, n_frames: int) -> list[int]:
    if fmt == "gaussian":
        # 0-based frame_index values (the on-disk frame_XXXXXX directories
        # carry source frame numbers, which differ per object).
        return orbit_gaussian.even_frame_indices(obj.n_available_frames, n_frames)
    return orbit_rgbd.even_frame_indices(obj.n_available_frames, n_frames,
                                         start=obj.source_start_frame)


def load_frames(dataset_root: Path, fmt: str, obj: SceneObject, frame_ids: list[int], *,
                max_points: int, image_scale: float, device: str, voxel_size: float,
                carve_scale: float, view_slack: int, refine_iters: int,
                prune_opacity: float, verbose: bool):
    if fmt == "gaussian":
        return orbit_gaussian.load_scene(
            dataset_root, obj.name, frame_ids, device=device,
            max_points_per_frame=max_points, image_scale=image_scale,
            carve_scale=carve_scale, voxel_size=voxel_size, view_slack=view_slack,
            refine_iters=refine_iters, prune_opacity=prune_opacity, verbose=verbose)
    return orbit_rgbd.load_scene(
        dataset_root, obj.name, frame_ids, device=device,
        max_points_per_frame=max_points, image_scale=image_scale)


def prepare_object(dataset_root: Path, fmt: str, obj: SceneObject, output_dir: Path, *,
                   n_frames: int, max_points: int, k_objects: int, key_iters: int,
                   residual_iters: int, dyn_iters: int, gov_max_group_len: int | None,
                   image_scale: float,
                   voxel_size: float, carve_scale: float, view_slack: int,
                   refine_iters: int, prune_opacity: float, tiny_hash_log2: int,
                   device: str, verbose: bool = True) -> dict:
    frame_ids = pick_frames(obj, fmt, n_frames)
    frames, _cams, _gts, bmin, bmax = load_frames(
        dataset_root, fmt, obj, frame_ids, max_points=max_points, image_scale=image_scale,
        device=device, voxel_size=voxel_size, carve_scale=carve_scale,
        view_slack=view_slack, refine_iters=refine_iters, prune_opacity=prune_opacity,
        verbose=verbose)
    frames_seg = segment_sequence(frames, k=min(k_objects, len(frames[0])), n_iters=20)

    color_config = DEFAULT_COLOR_CONFIG
    if tiny_hash_log2 != DEFAULT_COLOR_CONFIG.tiny_hash["log2_hashmap_size"]:
        color_config = dataclasses.replace(
            DEFAULT_COLOR_CONFIG,
            tiny_hash={**DEFAULT_COLOR_CONFIG.tiny_hash, "log2_hashmap_size": tiny_hash_log2})
    cfg = VegaEncoderConfig(color_config=color_config, key_iters=key_iters,
                            residual_iters=residual_iters, dyn_iters=dyn_iters,
                            gov_max_group_len=gov_max_group_len)
    # Explicit eval camera rather than the encoder's default: the default's
    # distance comes from scene extent alone and lands these human-sized
    # objects inside this rasterizer's partial-visibility band, which would
    # quietly corrupt the distortion/dynamicity/PSNR numbers. See
    # orbitvega.eval_camera.
    eval_cam = eval_camera_for_bounds(bmin, bmax, device)
    result = encode_sequence(frames_seg, bmin, bmax, eval_camera=eval_cam, config=cfg)

    write_bitstream(output_dir / obj.name, result.color_model, result.chunks)

    entry = {
        "object_id": obj.object_id,
        "name": obj.name,
        "frame_ids": list(frame_ids),
        "frame_count": len(frame_ids),
        "bounds_min": list(obj.bounds_min),
        "bounds_max": list(obj.bounds_max),
        "frame_types": [c.frame_type for c in result.chunks],
        "frame_bytes": [c.total_bytes for c in result.chunks],
        "gaussians_per_frame": [int(len(f.get_xyz)) for f in frames],
        "mean_psnr_db": sum(result.psnr_db) / len(result.psnr_db),
        "dir": obj.name,
    }
    if fmt == "gaussian":
        entry["frame_id_kind"] = "frame_index (0-based)"
        entry["source_frames"] = [obj.source_start_frame + t for t in frame_ids]
    else:
        entry["frame_id_kind"] = "on-disk frame number (1-based)"
    return entry


def prepare(
    dataset_root: Path,
    output_dir: Path,
    *,
    dataset_format: str | None = None,
    objects: set[str] | None = None,
    n_frames: int = 30,
    max_points: int = 120_000,
    k_objects: int = DEFAULT_K_OBJECTS,
    key_iters: int = 800,
    residual_iters: int = 600,
    dyn_iters: int = 20,
    gov_max_group_len: int | None = None,
    image_scale: float = 0.125,
    voxel_size: float = orbit_gaussian.DEFAULT_VOXEL_SIZE,
    carve_scale: float = orbit_gaussian.DEFAULT_CARVE_SCALE,
    view_slack: int = orbit_gaussian.DEFAULT_VIEW_SLACK,
    refine_iters: int = 0,
    prune_opacity: float = orbit_gaussian.DEFAULT_PRUNE_OPACITY,
    tiny_hash_log2: int = DEFAULT_COLOR_CONFIG.tiny_hash["log2_hashmap_size"],
    device: str = "cuda",
) -> dict:
    dataset_root = Path(dataset_root)
    fmt = dataset_format or detect_format(dataset_root)
    all_objects = list_objects(dataset_root, fmt)

    unknown = (objects or set()) - {obj.name for obj in all_objects}
    if unknown:
        raise ValueError(f"unknown objects: {', '.join(sorted(unknown))}")
    selected = tuple(obj for obj in all_objects if not objects or obj.name in objects)
    if not selected:
        raise ValueError("no objects selected")

    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    print(f"dataset {dataset_root} (format: {fmt}) -> {output_dir}", flush=True)
    if fmt == "gaussian" and refine_iters == 0:
        # Easy to leave off and hard to attribute later: the render just looks
        # grainy. Carving alone sits ~15 dB below what refinement reaches.
        print("      note: --refine-iters 0, so geometry is raw silhouette carving "
              "(~40 dB vs the real views; 2400 reaches ~55 dB and visibly "
              "de-speckles the result)", flush=True)

    object_entries = []
    for index, obj in enumerate(selected):
        print(f"[{index + 1}/{len(selected)}] encoding {obj.name} ...", flush=True)
        entry = prepare_object(
            dataset_root, fmt, obj, output_dir, n_frames=n_frames, max_points=max_points,
            k_objects=k_objects, key_iters=key_iters, residual_iters=residual_iters,
            dyn_iters=dyn_iters, gov_max_group_len=gov_max_group_len,
            image_scale=image_scale, voxel_size=voxel_size,
            carve_scale=carve_scale, view_slack=view_slack, refine_iters=refine_iters,
            prune_opacity=prune_opacity, tiny_hash_log2=tiny_hash_log2, device=device,
        )
        object_entries.append(entry)
        total_mb = sum(entry["frame_bytes"]) / 1e6
        print(f"    {entry['frame_count']} frames, {total_mb:.2f} MB, "
              f"{entry['frame_types'].count('key')} key frame(s), "
              f"mean PSNR {entry['mean_psnr_db']:.1f} dB", flush=True)

    encoder_config = {
        "n_frames": n_frames, "max_points_per_frame": max_points, "k_objects": k_objects,
        "key_iters": key_iters, "residual_iters": residual_iters, "dyn_iters": dyn_iters,
        "gov_max_group_len": gov_max_group_len,
        "image_scale": image_scale, "tiny_hash_log2": tiny_hash_log2,
    }
    if fmt == "gaussian":
        encoder_config.update({"voxel_size": voxel_size, "carve_scale": carve_scale,
                               "view_slack": view_slack, "refine_iters": refine_iters,
                               "prune_opacity": prune_opacity,
                               "geometry": "silhouette carving of the 8-view RGB rig"
                                           + (f" + {refine_iters} photometric refine iters"
                                              if refine_iters else "")})
    catalog = {
        "version": 2,
        "baseline": "Vega-ORBIT",
        "dataset_root": str(dataset_root),
        "dataset_format": fmt,
        "encoder_config": encoder_config,
        "objects": object_entries,
        "object_dir_pattern": "{object}/",
        "note": (
            "Each object's bitstream is produced independently by "
            "vega.encoder.encode_sequence and written via "
            "vega.bitstream.write_bitstream (manifest.json + "
            "color_model.pt + frame_XXXX.pt per object)."
        ),
    }
    (output_dir / "catalog.json").write_text(json.dumps(catalog, indent=2) + "\n", encoding="utf-8")
    return catalog


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Encode an ORBIT corpus into a Vega bitstream, one GOV per object")
    parser.add_argument("--dataset-root", type=Path, default=DEFAULT_DATASET_ROOT)
    parser.add_argument("--dataset-format", choices=("gaussian", "rgbd"), default=None,
                        help="default: auto-detected from --dataset-root")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--objects", nargs="+", help="object names; default is every object in the dataset")
    parser.add_argument("--frames", type=int, default=30, dest="n_frames")
    parser.add_argument("--max-points", type=int, default=120_000)
    parser.add_argument("--k-objects", type=int, default=DEFAULT_K_OBJECTS,
                        help="Gaussian-Grouping clusters per scene object. 1 (the default) "
                             "treats each ORBIT scene object as the single object it is; see "
                             "the README on why >1 ghosts with the k-means fallback")
    parser.add_argument("--key-iters", type=int, default=800)
    parser.add_argument("--residual-iters", type=int, default=600)
    parser.add_argument("--prune-opacity", type=float,
                        default=orbit_gaussian.DEFAULT_PRUNE_OPACITY,
                        help="drop Gaussians refinement faded below this opacity (0 disables)")
    parser.add_argument("--tiny-hash-log2", type=int,
                        default=DEFAULT_COLOR_CONFIG.tiny_hash["log2_hashmap_size"],
                        help="log2 entries per level in the per-residual-frame tiny hash table; "
                             "the main lever on residual-frame colour fidelity (see "
                             "vega.color_encoding)")
    parser.add_argument("--gov-max-group-len", type=int, default=None,
                        help="hard cap on frames per GOV. The RD rule alone can leave a "
                             "whole sequence in one group, so residual drift grows "
                             "unbounded; this forces a key-frame refresh. Omit for "
                             "pure-RD (previous) behaviour.")
    parser.add_argument("--dyn-iters", type=int, default=20)
    parser.add_argument("--image-scale", type=float, default=0.125)
    parser.add_argument("--voxel-size", type=float, default=orbit_gaussian.DEFAULT_VOXEL_SIZE,
                        help="gaussian corpus only: carving voxel size in metres")
    parser.add_argument("--carve-scale", type=float, default=orbit_gaussian.DEFAULT_CARVE_SCALE,
                        help="gaussian corpus only: image scale used for silhouettes")
    parser.add_argument("--view-slack", type=int, default=orbit_gaussian.DEFAULT_VIEW_SLACK,
                        help="gaussian corpus only: views allowed to disagree with the silhouette "
                             "(0 = strict visual hull)")
    parser.add_argument("--refine-iters", type=int, default=0,
                        help="gaussian corpus only: photometric 3DGS refinement iterations per "
                             "frame after carving (0 = off; attributes only, no densification)")
    parser.add_argument("--device", default="cuda")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    prepare(
        args.dataset_root, args.output_dir,
        dataset_format=args.dataset_format,
        objects=set(args.objects) if args.objects else None,
        n_frames=args.n_frames, max_points=args.max_points, k_objects=args.k_objects,
        key_iters=args.key_iters, residual_iters=args.residual_iters, dyn_iters=args.dyn_iters,
        gov_max_group_len=args.gov_max_group_len,
        image_scale=args.image_scale, voxel_size=args.voxel_size, carve_scale=args.carve_scale,
        view_slack=args.view_slack, refine_iters=args.refine_iters,
        prune_opacity=args.prune_opacity, tiny_hash_log2=args.tiny_hash_log2,
        device=args.device,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
