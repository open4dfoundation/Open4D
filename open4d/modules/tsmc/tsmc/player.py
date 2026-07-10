import argparse
import time
from pathlib import Path

import open3d as o3d


DEFAULT_MESH_DIR = Path(__file__).resolve().parents[1] / "data" / "synthetic" / "meshes"


def load_mesh(path: Path) -> o3d.geometry.TriangleMesh:
    mesh = o3d.io.read_triangle_mesh(str(path), enable_post_processing=False)
    if mesh.is_empty():
        raise ValueError(f"Failed to load mesh: {path}")

    mesh.compute_vertex_normals()
    return mesh


def get_scene_bounds(meshes: list[o3d.geometry.TriangleMesh]) -> o3d.geometry.AxisAlignedBoundingBox:
    bounds = meshes[0].get_axis_aligned_bounding_box()
    min_bound = bounds.get_min_bound()
    max_bound = bounds.get_max_bound()

    for mesh in meshes[1:]:
        mesh_bounds = mesh.get_axis_aligned_bounding_box()
        mesh_min = mesh_bounds.get_min_bound()
        mesh_max = mesh_bounds.get_max_bound()
        min_bound = [min(min_bound[i], mesh_min[i]) for i in range(3)]
        max_bound = [max(max_bound[i], mesh_max[i]) for i in range(3)]

    return o3d.geometry.AxisAlignedBoundingBox(min_bound, max_bound)


def fit_view_to_scene(vis: o3d.visualization.Visualizer, bounds: o3d.geometry.AxisAlignedBoundingBox) -> None:
    span = o3d.geometry.PointCloud()
    span.points = bounds.get_box_points()

    vis.add_geometry(span, reset_bounding_box=True)
    vis.remove_geometry(span, reset_bounding_box=False)


def play_meshes(meshes: list[o3d.geometry.TriangleMesh], fps: float, loop: bool) -> None:
    vis = o3d.visualization.Visualizer()
    vis.create_window(window_name="Mesh Player")

    frame_time = 1.0 / fps
    scene_bounds = get_scene_bounds(meshes)
    current_mesh = None

    try:
        fit_view_to_scene(vis, scene_bounds)

        running = True
        while running:
            for mesh in meshes:
                start = time.perf_counter()

                if current_mesh is not None:
                    vis.remove_geometry(current_mesh, reset_bounding_box=False)

                vis.add_geometry(mesh, reset_bounding_box=False)
                current_mesh = mesh

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
    parser = argparse.ArgumentParser(description="Play a sequence of OBJ meshes in one Open3D window.")
    parser.add_argument(
        "--mesh-dir",
        type=Path,
        default=DEFAULT_MESH_DIR,
        help="Directory containing OBJ meshes to play.",
    )
    parser.add_argument("--pattern", default="*.obj", help="Mesh filename glob pattern.")
    parser.add_argument("--fps", type=float, default=10.0, help="Playback speed.")
    parser.add_argument("--loop", action="store_true", help="Repeat playback until the window is closed.")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    mesh_paths = sorted(args.mesh_dir.glob(args.pattern))

    if not mesh_paths:
        raise FileNotFoundError(f"No meshes matched {args.mesh_dir / args.pattern}")
    if args.fps <= 0:
        raise ValueError("--fps must be greater than 0")

    print(f"Loading {len(mesh_paths)} meshes from {args.mesh_dir}")
    meshes = [load_mesh(path) for path in mesh_paths]

    print(f"Playing {len(meshes)} meshes")
    play_meshes(meshes, fps=args.fps, loop=args.loop)


if __name__ == "__main__":
    main()


'''
{
	"class_name" : "ViewTrajectory",
	"interval" : 29,
	"is_loop" : false,
	"trajectory" : 
	[
		{
			"boundingbox_max" : [ 0.81789398193359375, 1.0, 0.61250001192092896 ],
			"boundingbox_min" : [ -0.81789398193359375, -1.0, -0.61250001192092896 ],
			"field_of_view" : 60.0,
			"front" : [ -0.11928305449408404, 0.1187248099305494, -0.98573625905589801 ],
			"lookat" : [ 0.0, 0.0, 0.0 ],
			"up" : [ 0.019276167231214079, 0.99291453539989982, 0.11725678986086731 ],
			"zoom" : 0.84000000000000008
		}
	],
	"version_major" : 1,
	"version_minor" : 0
}
'''