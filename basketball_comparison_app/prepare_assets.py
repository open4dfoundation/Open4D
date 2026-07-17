"""Build videos and comparable metrics for the basketball codec dashboard."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess

import matplotlib
matplotlib.use("Agg")
from matplotlib import colors
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
import matplotlib.pyplot as plt
import numpy as np
import open3d as o3d
from scipy.spatial import cKDTree
import trimesh


ROOT = Path(__file__).resolve().parents[1]
APP = Path(__file__).resolve().parent
ASSETS = APP / "assets"
REFERENCE = ROOT / "open4d/modules/tvmc/arap-volume-tracking/data/basketball_player"
METHODS = {
    "N4MC": {
        "color": "#F6C453",
        "glob": ROOT / "open4d/modules/N4MC/outputs/basketball_sequence_n4mc/original_scale",
        "pattern": "*_reconstructed.ply",
        "variant": "basketball-trained TSDF autoencoder · 300 epochs",
    },
    "QNDF": {
        "color": "#FF746C",
        "glob": ROOT / "open4d/modules/Quantized-Neural-Displacement-Fields/outputs/basketball_sequence_qndf",
        "pattern": "basketball_player_fr*/reconstruction_original_scale.obj",
        "variant": "component-preserving Open3D SSP · 300 epochs",
    },
    "TVMC": {
        "color": "#31C6A5",
        "glob": ROOT / "open4d/modules/tvmc/TVMC/basketball_player_outputs",
        "pattern": "decoded_basketball_player_fr*.obj",
        "variant": "QP 10 · 2,000 tracked centers",
    },
    "TSMC": {
        "color": "#9A7DFF",
        "glob": ROOT / "open4d/modules/tsmc/outputs/basketball_sequence_tsmc/decoded",
        "pattern": "decoded_basketball_player_fr*.obj",
        "variant": "all-dynamic adapter · 5 eigenvectors",
    },
}


def mesh(path: Path) -> trimesh.Trimesh:
    loaded = trimesh.load(path, force="mesh", process=False)
    if not isinstance(loaded, trimesh.Trimesh) or not len(loaded.faces):
        raise ValueError(f"not a triangle mesh: {path}")
    return loaded


def sample(path: Path, count: int, seed: int) -> tuple[np.ndarray, np.ndarray]:
    value = mesh(path)
    points, face_ids = trimesh.sample.sample_surface(value, count=count, seed=seed)
    normals = np.asarray(value.face_normals)[face_ids]
    return np.asarray(points), normals


def compare(reference: Path, output: Path, samples: int) -> dict[str, float | int]:
    ref_mesh, out_mesh = mesh(reference), mesh(output)
    ref_points, ref_normals = sample(reference, samples, 20260716)
    out_points, out_normals = sample(output, samples, 20260717)
    out_tree, ref_tree = cKDTree(out_points), cKDTree(ref_points)
    ref_dist, ref_nearest = out_tree.query(ref_points, workers=-1)
    out_dist, out_nearest = ref_tree.query(out_points, workers=-1)
    distances = np.concatenate([ref_dist, out_dist])
    diagonal = float(np.linalg.norm(np.ptp(np.asarray(ref_mesh.vertices), axis=0)))
    normal_dots = np.concatenate([
        np.abs(np.einsum("ij,ij->i", ref_normals, out_normals[ref_nearest])),
        np.abs(np.einsum("ij,ij->i", out_normals, ref_normals[out_nearest])),
    ])
    return {
        "chamfer_nrmse_pct": float(np.sqrt(np.mean(distances**2)) / diagonal * 100),
        "p95_distance_pct": float(np.percentile(distances, 95) / diagonal * 100),
        "sampled_hausdorff_pct": float(np.max(distances) / diagonal * 100),
        "normal_consistency": float(np.mean(normal_dots)),
        "vertices": int(len(out_mesh.vertices)),
        "faces": int(len(out_mesh.faces)),
        "decoded_bytes": int(output.stat().st_size),
    }


def simplify_for_render(value: trimesh.Trimesh, max_faces: int) -> trimesh.Trimesh:
    """Reduce render cost without creating holes or discarding small components."""
    if len(value.faces) <= max_faces:
        return value
    components = value.split(only_watertight=False)
    total_faces = sum(len(component.faces) for component in components)
    simplified = []
    for component in components:
        budget = max(64, round(max_faces * len(component.faces) / total_faces))
        if len(component.faces) <= budget:
            simplified.append(component)
            continue
        source = o3d.geometry.TriangleMesh(
            o3d.utility.Vector3dVector(np.asarray(component.vertices)),
            o3d.utility.Vector3iVector(np.asarray(component.faces)),
        )
        reduced = source.simplify_quadric_decimation(target_number_of_triangles=budget)
        reduced.remove_degenerate_triangles()
        reduced.remove_duplicated_triangles()
        simplified.append(trimesh.Trimesh(
            vertices=np.asarray(reduced.vertices), faces=np.asarray(reduced.triangles), process=False,
        ))
    return trimesh.util.concatenate(simplified)


def surface_error_pct(reference: Path, value: trimesh.Trimesh) -> np.ndarray:
    """Return decoded-vertex distance to the source surface as bbox-diagonal percent."""
    reference_mesh = mesh(reference)
    legacy = o3d.geometry.TriangleMesh(
        o3d.utility.Vector3dVector(np.asarray(reference_mesh.vertices)),
        o3d.utility.Vector3iVector(np.asarray(reference_mesh.faces)),
    )
    scene = o3d.t.geometry.RaycastingScene()
    scene.add_triangles(o3d.t.geometry.TriangleMesh.from_legacy(legacy))
    queries = o3d.core.Tensor(np.asarray(value.vertices, dtype=np.float32))
    distances = scene.compute_distance(queries).numpy()
    diagonal = float(np.linalg.norm(np.ptp(np.asarray(reference_mesh.vertices), axis=0)))
    return distances / diagonal * 100


def render(value: trimesh.Trimesh, output: Path, bounds: np.ndarray, color: str,
           vertex_error_pct: np.ndarray | None = None, heatmap_max_pct: float = 1.5) -> None:
    vertices = np.asarray(value.vertices)
    faces = np.asarray(value.faces)
    triangles = vertices[faces][:, :, [0, 2, 1]]
    edges1, edges2 = triangles[:, 1] - triangles[:, 0], triangles[:, 2] - triangles[:, 0]
    normals = np.cross(edges1, edges2)
    normals /= np.maximum(np.linalg.norm(normals, axis=1, keepdims=True), 1e-12)
    light = np.array([0.45, -0.7, 0.75]); light /= np.linalg.norm(light)
    intensity = np.clip(0.28 + 0.72 * np.abs(normals @ light), 0, 1)
    if vertex_error_pct is None:
        base_colors = np.broadcast_to(np.asarray(colors.to_rgb(color)), (len(faces), 3))
    else:
        face_error = np.mean(vertex_error_pct[faces], axis=1)
        normalized = np.clip(face_error / heatmap_max_pct, 0, 1)
        base_colors = matplotlib.colormaps["turbo"](normalized)[:, :3]
    face_colors = np.c_[base_colors * intensity[:, None], np.ones(len(faces))]

    figure = plt.figure(figsize=(6.4, 6.4), dpi=100, facecolor="#08101d")
    axis = figure.add_subplot(111, projection="3d", facecolor="#08101d")
    collection = Poly3DCollection(triangles, facecolors=face_colors, edgecolors="none", linewidths=0)
    axis.add_collection3d(collection)
    remapped = bounds[:, [0, 2, 1]]
    center = remapped.mean(axis=0)
    radius = float(np.ptp(remapped, axis=0).max()) * 0.57
    axis.set_xlim(center[0] - radius, center[0] + radius)
    axis.set_ylim(center[1] - radius, center[1] + radius)
    axis.set_zlim(center[2] - radius, center[2] + radius)
    axis.set_box_aspect((1, 1, 1))
    axis.set_proj_type("ortho")
    axis.view_init(elev=8, azim=-72)
    axis.set_axis_off()
    figure.subplots_adjust(0, 0, 1, 1)
    figure.savefig(output, facecolor=figure.get_facecolor(), dpi=100)
    plt.close(figure)


def video(frames: Path, output: Path, fps: int) -> None:
    subprocess.run([
        "ffmpeg", "-y", "-loglevel", "error", "-framerate", str(fps),
        "-i", str(frames / "%04d.png"), "-vf", "format=yuv420p", "-c:v", "libx264",
        "-crf", "18", "-movflags", "+faststart", str(output),
    ], check=True)


def native_context() -> dict:
    context = {
        "N4MC": {"label": "Mean entropy-model rate", "value": "9,657 bits / volume"},
        "QNDF": {"label": "Estimated representation", "value": "126.42 KiB / frame"},
        "TVMC": {"label": "Native stream bitrate", "value": "7.039 Mbps"},
        "TSMC": {"label": "Native stream bitrate", "value": "not reported"},
    }
    tvmc = ROOT / "open4d/modules/tvmc/TVMC/basketball_player_outputs/metrics.json"
    if tvmc.exists():
        value = json.loads(tvmc.read_text())
        context["TVMC"].update(raw=value, value=f"{value['bitrate_mbps']:.3f} Mbps")
    tsmc = ROOT / "open4d/modules/tsmc/outputs/basketball_sequence_tsmc/native_metrics.json"
    if tsmc.exists():
        value = json.loads(tsmc.read_text())
        if value.get("bitrate_mbps") is not None:
            context["TSMC"].update(raw=value, value=f"{value['bitrate_mbps']:.3f} Mbps")
    return context


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--samples", type=int, default=12000)
    parser.add_argument("--max-render-faces", type=int, default=80000)
    parser.add_argument("--heatmap-max-pct", type=float, default=1.5)
    parser.add_argument("--fps", type=int, default=3)
    args = parser.parse_args()
    ASSETS.mkdir(parents=True, exist_ok=True)
    references = sorted(REFERENCE.glob("basketball_player_fr*.obj"))
    if len(references) != 10:
        raise RuntimeError(f"expected 10 references, found {len(references)}")
    method_paths = {}
    for name, spec in METHODS.items():
        paths = sorted(spec["glob"].glob(spec["pattern"]))
        if len(paths) != 10:
            raise RuntimeError(f"{name}: expected 10 outputs, found {len(paths)}")
        method_paths[name] = paths
    all_vertices = np.vstack([np.asarray(mesh(path).vertices) for path in references])
    bounds = np.vstack([all_vertices.min(axis=0), all_vertices.max(axis=0)])

    result = {"dataset": "basketball_player_fr0011–fr0020", "samples": args.samples,
              "heatmap": {"metric": "decoded-to-reference point-to-surface distance",
                          "units": "% of source bounding-box diagonal",
                          "max_pct": args.heatmap_max_pct, "colormap": "turbo"},
              "native_context": native_context(), "methods": {}, "frames": []}
    for name, spec in METHODS.items():
        frame_dir = ASSETS / f"{name.lower()}_frames"
        heatmap_dir = ASSETS / f"{name.lower()}_heatmap_frames"
        frame_dir.mkdir(exist_ok=True)
        heatmap_dir.mkdir(exist_ok=True)
        for index, path in enumerate(method_paths[name]):
            value = simplify_for_render(mesh(path), args.max_render_faces)
            render(value, frame_dir / f"{index:04d}.png", bounds, spec["color"])
            error_pct = surface_error_pct(references[index], value)
            render(value, heatmap_dir / f"{index:04d}.png", bounds, spec["color"],
                   vertex_error_pct=error_pct, heatmap_max_pct=args.heatmap_max_pct)
        video_path = ASSETS / f"{name.lower()}.mp4"
        heatmap_video_path = ASSETS / f"{name.lower()}_heatmap.mp4"
        video(frame_dir, video_path, args.fps)
        video(heatmap_dir, heatmap_video_path, args.fps)
        result["methods"][name] = {"color": spec["color"], "variant": spec["variant"],
                                    "video": str(video_path.relative_to(APP)),
                                    "heatmap_video": str(heatmap_video_path.relative_to(APP))}

    for index, reference in enumerate(references):
        row = {"frame": reference.stem, "methods": {}}
        for name in METHODS:
            row["methods"][name] = compare(reference, method_paths[name][index], args.samples)
        result["frames"].append(row)
    for name in METHODS:
        values = [frame["methods"][name] for frame in result["frames"]]
        result["methods"][name]["aggregate"] = {
            key: float(np.mean([value[key] for value in values]))
            for key in ("chamfer_nrmse_pct", "p95_distance_pct", "sampled_hausdorff_pct", "normal_consistency")
        }
    (APP / "comparison.json").write_text(json.dumps(result, indent=2) + "\n")
    print(f"wrote {APP / 'comparison.json'}")


if __name__ == "__main__":
    main()
