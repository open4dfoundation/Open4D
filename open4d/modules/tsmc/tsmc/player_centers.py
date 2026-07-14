from __future__ import annotations

import argparse
import time
from pathlib import Path

import numpy as np
import open3d as o3d
import matplotlib.cm as cm


DEFAULT_CENTER_DIR = Path(__file__).resolve().parents[1] / "data" / "synthetic" / "centers"


def load_xyz_points(path: Path) -> np.ndarray:
    points = np.loadtxt(path, dtype=np.float64)

    if points.ndim == 1:
        points = points.reshape(1, 3)

    if points.shape[1] != 3:
        raise ValueError(f"{path} should contain N x 3 xyz coordinates, got {points.shape}")

    return points


def get_points_bounds(points_list: list[np.ndarray]) -> o3d.geometry.AxisAlignedBoundingBox:
    all_points = np.concatenate(points_list, axis=0)
    min_bound = all_points.min(axis=0)
    max_bound = all_points.max(axis=0)
    return o3d.geometry.AxisAlignedBoundingBox(min_bound, max_bound)


def get_color_mapping(
    points_list: list[np.ndarray],
) -> tuple[np.ndarray, float, float]:
    all_points = np.concatenate(points_list, axis=0)
    centered_points = all_points - all_points.mean(axis=0)

    if len(all_points) < 2:
        axis = np.array([1.0, 0.0, 0.0])
    else:
        _, _, vh = np.linalg.svd(centered_points, full_matrices=False)
        axis = vh[0]

    values = all_points @ axis
    value_min = float(values.min())
    value_max = float(values.max())

    if np.isclose(value_min, value_max):
        axis = np.array([1.0, 0.0, 0.0])
        values = all_points @ axis
        value_min = float(values.min())
        value_max = float(values.max())

    return axis, value_min, value_max


def make_spatial_gradient_colors(
    points: np.ndarray,
    color_axis: np.ndarray,
    global_value_min: float,
    global_value_max: float,
    contrast: float,
    normalize_per_frame: bool,
    cmap_name: str = "turbo",
) -> np.ndarray:
    """
    Color centers by spatial position so nearby xyz points receive similar colors,
    while using a broad colormap range.
    """
    if len(points) == 0:
        return np.empty((0, 3))

    projections = points @ color_axis
    if normalize_per_frame:
        value_min = float(projections.min())
        value_max = float(projections.max())
    else:
        value_min = global_value_min
        value_max = global_value_max

    value_range = max(value_max - value_min, 1e-12)
    values = (projections - value_min) / value_range
    values = np.clip(0.5 + (values - 0.5) * contrast, 0.0, 1.0)
    cmap = cm.get_cmap(cmap_name)
    colors = cmap(values)[:, :3]
    return colors


def make_point_cloud(points: np.ndarray, colors: np.ndarray) -> o3d.geometry.PointCloud:
    pcd = o3d.geometry.PointCloud()
    pcd.points = o3d.utility.Vector3dVector(points)
    pcd.colors = o3d.utility.Vector3dVector(colors)
    return pcd


def make_sphere_cloud(
    points: np.ndarray,
    colors: np.ndarray,
    radius: float = 0.02,
    resolution: int = 8,
) -> o3d.geometry.TriangleMesh:
    """
    Render each center as a small sphere.
    This looks closer to your example figure, but is slower.
    """
    sphere_cloud = o3d.geometry.TriangleMesh()

    for p, c in zip(points, colors):
        sphere = o3d.geometry.TriangleMesh.create_sphere(radius=radius, resolution=resolution)
        sphere.translate(p)
        sphere.paint_uniform_color(c)
        sphere_cloud += sphere

    sphere_cloud.compute_vertex_normals()
    return sphere_cloud


def fit_view_to_scene(
    vis: o3d.visualization.Visualizer,
    bounds: o3d.geometry.AxisAlignedBoundingBox,
) -> None:
    span = o3d.geometry.PointCloud()
    span.points = bounds.get_box_points()

    vis.add_geometry(span, reset_bounding_box=True)
    vis.remove_geometry(span, reset_bounding_box=False)


def preload_center_geometries(
    points_list: list[np.ndarray],
    render_as_spheres: bool,
    sphere_radius: float,
    cmap_name: str,
    contrast: float,
    normalize_per_frame: bool,
) -> list[o3d.geometry.Geometry]:
    color_axis, global_value_min, global_value_max = get_color_mapping(points_list)

    geometries = []
    for points in points_list:
        colors = make_spatial_gradient_colors(
            points,
            color_axis=color_axis,
            global_value_min=global_value_min,
            global_value_max=global_value_max,
            contrast=contrast,
            normalize_per_frame=normalize_per_frame,
            cmap_name=cmap_name,
        )
        if render_as_spheres:
            geometry = make_sphere_cloud(
                points,
                colors,
                radius=sphere_radius,
            )
        else:
            geometry = make_point_cloud(points, colors)

        geometries.append(geometry)

    return geometries


def play_centers(
    center_geometries: list[o3d.geometry.Geometry],
    scene_bounds: o3d.geometry.AxisAlignedBoundingBox,
    fps: float,
    loop: bool,
    point_size: float,
) -> None:
    vis = o3d.visualization.Visualizer()
    vis.create_window(window_name="Volume Center Player")

    render_opt = vis.get_render_option()
    render_opt.background_color = np.array([1.0, 1.0, 1.0])
    render_opt.point_size = point_size

    frame_time = 1.0 / fps
    current_geom = None

    try:
        fit_view_to_scene(vis, scene_bounds)

        running = True
        while running:
            for geometry in center_geometries:
                start = time.perf_counter()

                if current_geom is not None:
                    vis.remove_geometry(current_geom, reset_bounding_box=False)

                current_geom = geometry
                vis.add_geometry(current_geom, reset_bounding_box=False)

                vis.poll_events()
                vis.update_renderer()

                elapsed = time.perf_counter() - start
                time.sleep(max(0.0, frame_time - elapsed))

                running = vis.poll_events()
                if not running:
                    break

            if not loop:
                break

        while running:
            running = vis.poll_events()
            vis.update_renderer()
            time.sleep(0.01)

    finally:
        vis.destroy_window()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Play preloaded xyz center files in Open3D with gradient colors.")

    parser.add_argument(
        "--center-dir",
        type=Path,
        required=True,
        help="Directory containing xyz center files.",
    )

    parser.add_argument(
        "--pattern",
        default="frame_0res_2000_*.xyz",
        help="Glob pattern for xyz files.",
    )

    parser.add_argument(
        "--fps",
        type=float,
        default=10.0,
        help="Playback FPS.",
    )

    parser.add_argument(
        "--loop",
        action="store_true",
        help="Loop playback.",
    )

    parser.add_argument(
        "--spheres",
        action="store_true",
        help="Render centers as spheres instead of point cloud.",
    )

    parser.add_argument(
        "--point-size",
        type=float,
        default=6.0,
        help="Point size if rendering as points.",
    )

    parser.add_argument(
        "--sphere-radius",
        type=float,
        default=0.02,
        help="Sphere radius if rendering as spheres.",
    )

    parser.add_argument(
        "--cmap",
        default="turbo",
        help="Matplotlib colormap name, e.g. turbo, rainbow, viridis, jet.",
    )

    parser.add_argument(
        "--color-contrast",
        type=float,
        default=1.8,
        help="Expand the spatial gradient range. Use 1.0 for no expansion.",
    )

    parser.add_argument(
        "--global-colors",
        action="store_true",
        help="Use one global color range across all frames instead of expanding each frame.",
    )

    return parser.parse_args()


def main() -> None:
    args = parse_args()

    center_paths = sorted(args.center_dir.glob(args.pattern))

    if not center_paths:
        raise FileNotFoundError(f"No xyz files matched: {args.center_dir / args.pattern}")

    if args.fps <= 0:
        raise ValueError("--fps must be greater than 0")
    if args.color_contrast <= 0:
        raise ValueError("--color-contrast must be greater than 0")

    print(f"Loading {len(center_paths)} xyz files from {args.center_dir}")

    points_list = []
    for path in center_paths:
        points = load_xyz_points(path)
        points_list.append(points)
        print(f"{path.name}: {points.shape[0]} centers")

    scene_bounds = get_points_bounds(points_list)

    print("Preloading center geometries with gradient colors...")
    center_geometries = preload_center_geometries(
        points_list=points_list,
        render_as_spheres=args.spheres,
        sphere_radius=args.sphere_radius,
        cmap_name=args.cmap,
        contrast=args.color_contrast,
        normalize_per_frame=not args.global_colors,
    )

    print("Playing preloaded centers...")
    play_centers(
        center_geometries=center_geometries,
        scene_bounds=scene_bounds,
        fps=args.fps,
        loop=args.loop,
        point_size=args.point_size,
    )


if __name__ == "__main__":
    main()
