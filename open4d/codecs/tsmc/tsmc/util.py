import os
import re
import cv2
import open3d as o3d
import numpy as np
from collections import deque
from copy import deepcopy
import trimesh
import time
import math
import matplotlib.pyplot as plt
from collections import defaultdict
import cupy as cp
from scipy.spatial.distance import directed_hausdorff
from tqdm import tqdm
from scipy.sparse import coo_matrix, lil_matrix, save_npz, vstack, diags, identity
import cupyx
import cupyx.scipy.sparse
from cupyx.scipy.sparse.linalg import cg
import point_cloud_utils as pcu
import matplotlib.cm as cm
from skimage.metrics import structural_similarity as ssim, peak_signal_noise_ratio as psnr

def flip_uv_v_inplace(mesh: o3d.geometry.TriangleMesh):
    if not mesh.has_triangle_uvs():
        return
    uvs = np.asarray(mesh.triangle_uvs)
    uvs[:, 1] = 1.0 - uvs[:, 1]
    mesh.triangle_uvs = o3d.utility.Vector2dVector(uvs)

def select_viewpoints(mesh, gt_mesh, num_views=4, width=1080, height=1920):
    """
    Interactively select viewpoints for rendering.

    Args:
        mesh (o3d.geometry.TriangleMesh): Mesh to visualize.
        gt_mesh (o3d.geometry.TriangleMesh): Ground truth mesh.
        num_views (int): Number of viewpoints to select.

    Returns:
        list: List of PinholeCameraParameters for selected viewpoints.
    """
    viewpoints = []
    print(f"Select {num_views} viewpoints. Adjust the view, press 'Q' to close the window, then choose to save via console.")

    for i in range(num_views * 2):  # Allow extra attempts
        vis = o3d.visualization.Visualizer()
        vis.create_window(width=width, height=height)
        vis.add_geometry(mesh)
        vis.add_geometry(gt_mesh)
        vis.run()

        # Get current view parameters
        param = vis.get_view_control().convert_to_pinhole_camera_parameters()
        vis.destroy_window()

        # Prompt user to save viewpoint
        #response = input(f"Save viewpoint {len(viewpoints)+1}? (y/n): ").strip().lower()
        #if response == 'y':
            # Check if viewpoint is unique
        is_unique = True
        for existing_param in viewpoints:
            if np.allclose(param.extrinsic, existing_param.extrinsic, atol=1e-3):
                print("Viewpoint is too similar to a previous one. Please select a different view.")
                is_unique = False
                break
        if is_unique:
            viewpoints.append(param)
            print(f"Viewpoint {len(viewpoints)} saved. Extrinsic: {param.extrinsic}")
        #else:
        #    print(f"Viewpoint skipped. Select another.")

        if len(viewpoints) >= num_views:
            break

    if len(viewpoints) < num_views:
        print(f"Warning: Only {len(viewpoints)} viewpoints saved. Proceeding with available views.")

    return viewpoints[:num_views]

def render_mesh_texture(mesh, width=512, height=512, camera_params=None, enable_lighting=False):
    """
    Render textured RGB + depth using OffscreenRenderer (Open3D 0.19.x).
    Returns: (rgb_uint8 HxWx3, depth_uint8 HxW, (dmin, dmax))
    """
    mesh_copy = mesh

    if enable_lighting and not mesh_copy.has_vertex_normals():
        mesh_copy.compute_vertex_normals()

    # ---- Make a "held" texture image if present ----
    tex_img = None
    if hasattr(mesh_copy, "textures") and len(mesh_copy.textures) > 0:
        # Convert to numpy and re-wrap -> ensures a held instance owned by Python
        tex_np = np.asarray(mesh_copy.textures[0])
        if tex_np is not None and tex_np.size > 0:
            tex_img = o3d.geometry.Image(np.ascontiguousarray(tex_np))

    use_texture = (tex_img is not None) and mesh_copy.has_triangle_uvs()

    renderer = o3d.visualization.rendering.OffscreenRenderer(width, height)
    renderer.scene.set_background([1.0, 1.0, 1.0, 1.0])

    mat = o3d.visualization.rendering.MaterialRecord()
    mat.shader = "defaultLit" if enable_lighting else "defaultUnlit"
    mat.base_color = [1.0, 1.0, 1.0, 1.0]

    if use_texture:
        mat.albedo_img = tex_img
        if enable_lighting:
            mat.roughness = 1.0
            mat.metallic = 0.0

    renderer.scene.add_geometry("mesh", mesh_copy, mat)

    # Camera
    if camera_params is not None:
        intrinsic = camera_params.intrinsic.intrinsic_matrix
        extrinsic = camera_params.extrinsic
        renderer.setup_camera(intrinsic, extrinsic, width, height)
    else:
        center = mesh_copy.get_center()
        eye = center + np.array([0, 0, 2.0])
        renderer.scene.camera.look_at(center, eye, [0, 1, 0])

    rgb_img = np.asarray(renderer.render_to_image(), dtype=np.uint8)
    depth_f = np.asarray(renderer.render_to_depth_image())

    dmin, dmax = float(depth_f.min()), float(depth_f.max())
    if dmax > dmin:
        depth_img = ((depth_f - dmin) / (dmax - dmin) * 255.0).astype(np.uint8)
    else:
        depth_img = np.zeros_like(depth_f, dtype=np.uint8)

    renderer.scene.clear_geometry()
    del renderer

    return rgb_img, depth_img, (dmin, dmax)


def render_mesh_geometry(
    mesh,
    width=512,
    height=512,
    camera_params=None,
    background_color=(1.0, 1.0, 1.0, 1.0),
    base_color=(0.7, 0.7, 0.7, 1.0),
):
    """
    Render a non-textured mesh with plain shaded geometry, similar to draw_geometries([mesh]).
    Returns: (rgb_uint8 HxWx3, depth_uint8 HxW, (dmin, dmax))
    """
    mesh_copy = o3d.geometry.TriangleMesh(mesh)

    if not mesh_copy.has_vertex_normals():
        mesh_copy.compute_vertex_normals()

    renderer = o3d.visualization.rendering.OffscreenRenderer(width, height)
    renderer.scene.set_background(list(background_color))

    mat = o3d.visualization.rendering.MaterialRecord()
    mat.shader = "defaultLit"
    mat.base_color = list(base_color)
    mat.base_roughness = 1.0
    mat.base_reflectance = 0.0
    mat.base_metallic = 0.0

    if mesh_copy.has_vertex_colors():
        mat.shader = "defaultLit"

    renderer.scene.add_geometry("mesh", mesh_copy, mat)

    if camera_params is not None:
        intrinsic = camera_params.intrinsic.intrinsic_matrix
        extrinsic = camera_params.extrinsic
        renderer.setup_camera(intrinsic, extrinsic, width, height)
    else:
        bbox = mesh_copy.get_axis_aligned_bounding_box()
        center = bbox.get_center()
        extent = max(float(np.max(bbox.get_extent())), 1e-3)
        eye = center + np.array([0.0, 0.0, 2.5 * extent])
        renderer.scene.camera.look_at(center, eye, [0.0, 1.0, 0.0])

    rgb_img = np.asarray(renderer.render_to_image(), dtype=np.uint8)
    depth_f = np.asarray(renderer.render_to_depth_image())

    finite = np.isfinite(depth_f)
    if np.any(finite):
        dmin = float(depth_f[finite].min())
        dmax = float(depth_f[finite].max())
    else:
        dmin = 0.0
        dmax = 0.0

    if dmax > dmin:
        depth_norm = np.zeros_like(depth_f, dtype=np.float32)
        depth_norm[finite] = (depth_f[finite] - dmin) / (dmax - dmin)
        depth_img = (depth_norm * 255.0).astype(np.uint8)
    else:
        depth_img = np.zeros((height, width), dtype=np.uint8)

    renderer.scene.clear_geometry()
    del renderer

    return rgb_img, depth_img, (dmin, dmax)


def render_mesh_auto(mesh, width=512, height=512, camera_params=None, enable_lighting=True):
    """
    Render a mesh using textures when available, otherwise fall back to plain shaded geometry.
    """
    has_textures = hasattr(mesh, "textures") and len(mesh.textures) > 0
    has_uvs = mesh.has_triangle_uvs()
    if has_textures and has_uvs:
        return render_mesh_texture(
            mesh,
            width=width,
            height=height,
            camera_params=camera_params,
            enable_lighting=enable_lighting,
        )
    return render_mesh_geometry(
        mesh,
        width=width,
        height=height,
        camera_params=camera_params,
    )


def _make_single_material_submesh(mesh, triangle_indices, material_id):
    triangles = np.asarray(mesh.triangles)
    vertices = np.asarray(mesh.vertices)

    submesh = o3d.geometry.TriangleMesh()
    submesh.vertices = o3d.utility.Vector3dVector(vertices.copy())
    submesh.triangles = o3d.utility.Vector3iVector(triangles[triangle_indices].copy())

    if mesh.has_vertex_normals():
        submesh.vertex_normals = o3d.utility.Vector3dVector(np.asarray(mesh.vertex_normals).copy())
    else:
        submesh.compute_vertex_normals()

    if mesh.has_vertex_colors():
        submesh.vertex_colors = o3d.utility.Vector3dVector(np.asarray(mesh.vertex_colors).copy())

    if mesh.has_triangle_uvs():
        triangle_uvs = np.asarray(mesh.triangle_uvs).reshape(-1, 3, 2)
        submesh.triangle_uvs = o3d.utility.Vector2dVector(
            triangle_uvs[triangle_indices].reshape(-1, 2).copy()
        )

    if hasattr(mesh, "textures") and len(mesh.textures) > material_id:
        tex_np = np.asarray(mesh.textures[material_id])
        if tex_np is not None and tex_np.size > 0:
            submesh.textures = [o3d.geometry.Image(np.ascontiguousarray(tex_np))]

    return submesh


def render_mesh_materials(
    mesh,
    width=512,
    height=512,
    camera_params=None,
    background_color=(1.0, 1.0, 1.0, 1.0),
    enable_lighting=True,
):
    """
    Render a legacy TriangleMesh with multiple textures by splitting it per material id.
    Returns: (rgb_uint8 HxWx3, depth_uint8 HxW, (dmin, dmax))
    """
    mesh_copy = o3d.geometry.TriangleMesh(mesh)
    if not mesh_copy.has_vertex_normals():
        mesh_copy.compute_vertex_normals()

    if not mesh_copy.has_triangle_material_ids() or not mesh_copy.has_textures():
        return render_mesh_auto(
            mesh_copy,
            width=width,
            height=height,
            camera_params=camera_params,
            enable_lighting=enable_lighting,
        )

    triangle_material_ids = np.asarray(mesh_copy.triangle_material_ids).reshape(-1)
    unique_material_ids = sorted(int(mid) for mid in np.unique(triangle_material_ids) if mid >= 0)
    if not unique_material_ids:
        return render_mesh_auto(
            mesh_copy,
            width=width,
            height=height,
            camera_params=camera_params,
            enable_lighting=enable_lighting,
        )

    renderer = o3d.visualization.rendering.OffscreenRenderer(width, height)
    renderer.scene.set_background(list(background_color))

    for material_id in unique_material_ids:
        triangle_indices = np.flatnonzero(triangle_material_ids == material_id)
        if triangle_indices.size == 0:
            continue

        submesh = _make_single_material_submesh(mesh_copy, triangle_indices, material_id)

        mat = o3d.visualization.rendering.MaterialRecord()
        mat.shader = "defaultLit" if enable_lighting else "defaultUnlit"
        mat.base_color = [1.0, 1.0, 1.0, 1.0]

        if hasattr(submesh, "textures") and len(submesh.textures) > 0:
            tex_np = np.asarray(submesh.textures[0])
            if tex_np is not None and tex_np.size > 0:
                mat.albedo_img = o3d.geometry.Image(np.ascontiguousarray(tex_np))
        elif submesh.has_vertex_colors():
            mat.shader = "defaultLit"
        else:
            mat.base_color = [0.7, 0.7, 0.7, 1.0]

        renderer.scene.add_geometry(f"mesh_mat_{material_id}", submesh, mat)

    if camera_params is not None:
        intrinsic = camera_params.intrinsic.intrinsic_matrix
        extrinsic = camera_params.extrinsic
        renderer.setup_camera(intrinsic, extrinsic, width, height)
    else:
        bbox = mesh_copy.get_axis_aligned_bounding_box()
        center = bbox.get_center()
        extent = max(float(np.max(bbox.get_extent())), 1e-3)
        eye = center + np.array([0.0, 0.0, 2.5 * extent])
        renderer.scene.camera.look_at(center, eye, [0.0, 1.0, 0.0])

    rgb_img = np.asarray(renderer.render_to_image(), dtype=np.uint8)
    depth_f = np.asarray(renderer.render_to_depth_image())

    finite = np.isfinite(depth_f)
    if np.any(finite):
        dmin = float(depth_f[finite].min())
        dmax = float(depth_f[finite].max())
    else:
        dmin = 0.0
        dmax = 0.0

    if dmax > dmin:
        depth_norm = np.zeros_like(depth_f, dtype=np.float32)
        depth_norm[finite] = (depth_f[finite] - dmin) / (dmax - dmin)
        depth_img = (depth_norm * 255.0).astype(np.uint8)
    else:
        depth_img = np.zeros((height, width), dtype=np.uint8)

    renderer.scene.clear_geometry()
    del renderer

    return rgb_img, depth_img, (dmin, dmax)


def render_triangle_model(
    model_or_path,
    width=512,
    height=512,
    camera_params=None,
    background_color=(1.0, 1.0, 1.0, 1.0),
):
    """
    Render a multi-material OBJ/FBX/GLTF using Open3D's TriangleMeshModel path.

    This is the correct path for meshes with multiple materials / texture maps,
    since ``read_triangle_mesh()`` + a single MaterialRecord only uses one material.
    Returns: (rgb_uint8 HxWx3, depth_uint8 HxW, (dmin, dmax))
    """
    if isinstance(model_or_path, (str, os.PathLike)):
        model = o3d.io.read_triangle_model(str(model_or_path), print_progress=False)
    else:
        model = model_or_path

    if len(model.meshes) == 0:
        raise ValueError("TriangleMeshModel is empty")

    renderer = o3d.visualization.rendering.OffscreenRenderer(width, height)
    renderer.scene.set_background(list(background_color))
    renderer.scene.add_model("model", model)

    if camera_params is not None:
        intrinsic = camera_params.intrinsic.intrinsic_matrix
        extrinsic = camera_params.extrinsic
        renderer.setup_camera(intrinsic, extrinsic, width, height)
    else:
        all_vertices = []
        for mesh_info in model.meshes:
            vertices = np.asarray(mesh_info.mesh.vertices)
            if vertices.size:
                all_vertices.append(vertices)
        if not all_vertices:
            raise ValueError("TriangleMeshModel contains no vertices")
        vertices = np.concatenate(all_vertices, axis=0)
        center = vertices.mean(axis=0)
        extent = max(float(np.max(vertices.max(axis=0) - vertices.min(axis=0))), 1e-3)
        eye = center + np.array([0.0, 0.0, 2.5 * extent])
        renderer.scene.camera.look_at(center, eye, [0.0, 1.0, 0.0])

    rgb_img = np.asarray(renderer.render_to_image(), dtype=np.uint8)
    depth_f = np.asarray(renderer.render_to_depth_image())

    finite = np.isfinite(depth_f)
    if np.any(finite):
        dmin = float(depth_f[finite].min())
        dmax = float(depth_f[finite].max())
    else:
        dmin = 0.0
        dmax = 0.0

    if dmax > dmin:
        depth_norm = np.zeros_like(depth_f, dtype=np.float32)
        depth_norm[finite] = (depth_f[finite] - dmin) / (dmax - dmin)
        depth_img = (depth_norm * 255.0).astype(np.uint8)
    else:
        depth_img = np.zeros((height, width), dtype=np.uint8)

    renderer.scene.clear_geometry()
    del renderer

    return rgb_img, depth_img, (dmin, dmax)


_RENDER_FRAME_PATTERN = re.compile(r"(?P<frame>\d+)_v_(?P<view>\d+)\.(?:jpe?g|png)$", re.IGNORECASE)


def index_render_sequence(render_dir):
    """Index a flattened render folder named like ``00_v_03.jpg``."""
    records = []
    for name in os.listdir(render_dir):
        match = _RENDER_FRAME_PATTERN.fullmatch(name)
        if match is None:
            continue
        frame_id = int(match.group("frame"))
        view_id = int(match.group("view"))
        records.append((frame_id, view_id, os.path.join(render_dir, name)))

    if not records:
        raise ValueError(f"No frame-view renders found in {render_dir}")

    frame_ids = sorted({frame_id for frame_id, _, _ in records})
    view_ids = sorted({view_id for _, view_id, _ in records})
    frame_to_pos = {frame_id: idx for idx, frame_id in enumerate(frame_ids)}
    view_to_pos = {view_id: idx for idx, view_id in enumerate(view_ids)}

    paths_by_view = {view_id: [None] * len(frame_ids) for view_id in view_ids}
    flat_paths = [None] * (len(frame_ids) * len(view_ids))

    for frame_id, view_id, path in records:
        frame_pos = frame_to_pos[frame_id]
        view_pos = view_to_pos[view_id]
        paths_by_view[view_id][frame_pos] = path
        flat_paths[frame_pos * len(view_ids) + view_pos] = path

    missing = [
        (frame_ids[frame_pos], view_id)
        for view_id, view_paths in paths_by_view.items()
        for frame_pos, path in enumerate(view_paths)
        if path is None
    ]
    if missing:
        raise ValueError(f"Missing rendered images for frame/view pairs: {missing[:5]}")

    return {
        "frame_ids": frame_ids,
        "view_ids": view_ids,
        "frame_to_pos": frame_to_pos,
        "view_to_pos": view_to_pos,
        "paths_by_view": paths_by_view,
        "flat_paths": flat_paths,
        "num_frames": len(frame_ids),
        "num_views": len(view_ids),
    }


def _load_rgb_image(path, downscale):
    image_bgr = cv2.imread(path, cv2.IMREAD_COLOR)
    if image_bgr is None:
        raise ValueError(f"Failed to read image: {path}")
    image_rgb = cv2.cvtColor(image_bgr, cv2.COLOR_BGR2RGB)
    if downscale != 1.0:
        image_rgb = cv2.resize(
            image_rgb,
            dsize=None,
            fx=downscale,
            fy=downscale,
            interpolation=cv2.INTER_AREA,
        )
    return image_rgb


def _extract_largest_component(mask, min_area):
    mask_u8 = mask.astype(np.uint8)
    num_labels, labels, stats, _ = cv2.connectedComponentsWithStats(mask_u8, connectivity=8)
    if num_labels <= 1:
        return np.zeros_like(mask, dtype=bool)

    best_label = 0
    best_area = 0
    for label_idx in range(1, num_labels):
        area = int(stats[label_idx, cv2.CC_STAT_AREA])
        if area >= min_area and area > best_area:
            best_label = label_idx
            best_area = area

    if best_label == 0:
        return np.zeros_like(mask, dtype=bool)
    return labels == best_label


def _pick_positive_points(mask, count):
    if count <= 0:
        return []

    dist = cv2.distanceTransform(mask.astype(np.uint8), cv2.DIST_L2, 5)
    work = dist.copy()
    if not np.any(work > 0):
        return []

    radius = max(8, int(np.sqrt(mask.sum() / max(count, 1)) * 0.35))
    points = []
    for _ in range(count):
        y, x = np.unravel_index(np.argmax(work), work.shape)
        if work[y, x] <= 0:
            break
        points.append([int(x), int(y)])
        cv2.circle(work, (int(x), int(y)), radius, 0, thickness=-1)

    return points


def _pick_negative_points(mask, count):
    if count <= 0 or not np.any(mask):
        return []

    ys, xs = np.where(mask)
    y0, y1 = int(ys.min()), int(ys.max())
    x0, x1 = int(xs.min()), int(xs.max())
    height, width = mask.shape
    pad = max(12, int(0.15 * max(y1 - y0 + 1, x1 - x0 + 1)))

    candidates = [
        [max(0, x0 - pad), max(0, y0 - pad)],
        [min(width - 1, x1 + pad), max(0, y0 - pad)],
        [max(0, x0 - pad), min(height - 1, y1 + pad)],
        [min(width - 1, x1 + pad), min(height - 1, y1 + pad)],
        [max(0, x0 - pad), (y0 + y1) // 2],
        [min(width - 1, x1 + pad), (y0 + y1) // 2],
    ]

    negatives = []
    for x, y in candidates:
        if not mask[y, x]:
            negatives.append([int(x), int(y)])
        if len(negatives) >= count:
            break
    return negatives


def _analyze_view_motion(
    frame_paths,
    downscale=0.5,
    white_threshold=245,
    motion_percentile=96,
    min_component_area=96,
):
    if len(frame_paths) < 2:
        raise ValueError("Need at least two frames per view to estimate motion")

    kernel = np.ones((3, 3), dtype=np.uint8)
    first_rgb = _load_rgb_image(frame_paths[0], downscale)
    prev_gray = cv2.cvtColor(first_rgb, cv2.COLOR_RGB2GRAY)
    prev_valid = np.any(first_rgb < white_threshold, axis=2)

    heatmap = np.zeros_like(prev_gray, dtype=np.float32)
    best_mask = np.zeros_like(prev_gray, dtype=bool)
    best_score = 0.0
    best_frame_pos = 0

    for frame_pos, path in enumerate(frame_paths[1:], start=1):
        curr_rgb = _load_rgb_image(path, downscale)
        curr_gray = cv2.cvtColor(curr_rgb, cv2.COLOR_RGB2GRAY)
        curr_valid = np.any(curr_rgb < white_threshold, axis=2)

        diff = cv2.absdiff(curr_gray, prev_gray).astype(np.float32)
        valid = prev_valid | curr_valid
        diff[~valid] = 0.0
        diff = cv2.GaussianBlur(diff, (0, 0), 1.2)
        heatmap += diff

        valid_diff = diff[valid]
        if valid_diff.size == 0:
            prev_gray = curr_gray
            prev_valid = curr_valid
            continue

        threshold = max(8.0, float(np.percentile(valid_diff, motion_percentile)))
        motion_mask = diff >= threshold
        motion_mask = cv2.morphologyEx(motion_mask.astype(np.uint8), cv2.MORPH_OPEN, kernel)
        motion_mask = cv2.morphologyEx(motion_mask, cv2.MORPH_CLOSE, kernel).astype(bool)
        component_mask = _extract_largest_component(motion_mask, min_area=min_component_area)

        if np.any(component_mask):
            component_score = float(diff[component_mask].sum())
            if component_score > best_score:
                best_score = component_score
                best_mask = component_mask
                best_frame_pos = frame_pos

        prev_gray = curr_gray
        prev_valid = curr_valid

    return {
        "best_mask": best_mask,
        "best_score": best_score,
        "best_frame_pos": best_frame_pos,
        "heatmap": heatmap,
        "image_shape": best_mask.shape,
    }


def auto_select_sam3_prompt(
    render_dir,
    prompt_frame_pos=0,
    num_positive_points=3,
    num_negative_points=2,
    downscale=0.5,
    white_threshold=245,
    motion_percentile=96,
    min_component_area=96,
):
    """
    Automatically propose SAM3 point prompts from temporal motion in one reference view.
    """
    render_index = index_render_sequence(render_dir)
    view_analyses = {}
    best_view_id = None
    best_view_score = -1.0

    for view_id, frame_paths in render_index["paths_by_view"].items():
        analysis = _analyze_view_motion(
            frame_paths,
            downscale=downscale,
            white_threshold=white_threshold,
            motion_percentile=motion_percentile,
            min_component_area=min_component_area,
        )
        view_analyses[view_id] = analysis
        if analysis["best_score"] > best_view_score:
            best_view_id = view_id
            best_view_score = analysis["best_score"]

    if best_view_id is None or best_view_score <= 0:
        raise ValueError("Failed to find a dynamic region from temporal motion")

    best_analysis = view_analyses[best_view_id]
    prompt_frame_pos = int(np.clip(prompt_frame_pos, 0, render_index["num_frames"] - 1))
    prompt_mask = best_analysis["best_mask"].copy()
    if not np.any(prompt_mask):
        raise ValueError(f"No usable motion proposal found for reference view {best_view_id}")

    reference_frame_path = render_index["paths_by_view"][best_view_id][prompt_frame_pos]
    reference_rgb = _load_rgb_image(reference_frame_path, downscale=1.0)
    reference_height, reference_width = reference_rgb.shape[:2]
    prompt_rgb = _load_rgb_image(reference_frame_path, downscale=downscale)
    prompt_foreground = np.any(prompt_rgb < white_threshold, axis=2)
    prompt_mask = prompt_mask & prompt_foreground
    if not np.any(prompt_mask):
        prompt_mask = best_analysis["best_mask"]

    prompt_height, prompt_width = prompt_mask.shape
    scale_x = reference_width / prompt_width
    scale_y = reference_height / prompt_height

    positive_points = _pick_positive_points(prompt_mask, num_positive_points)
    negative_points = _pick_negative_points(prompt_mask, num_negative_points)
    all_points = positive_points + negative_points
    all_labels = [1] * len(positive_points) + [0] * len(negative_points)

    if not all_points:
        raise ValueError("Automatic prompt generation produced no points")

    points_abs = np.array(
        [
            [
                int(np.clip(round(x * scale_x), 0, reference_width - 1)),
                int(np.clip(round(y * scale_y), 0, reference_height - 1)),
            ]
            for x, y in all_points
        ],
        dtype=np.int32,
    )
    labels = np.array(all_labels, dtype=np.int32)
    flat_frame_index = prompt_frame_pos * render_index["num_views"] + render_index["view_to_pos"][best_view_id]

    return {
        "reference_view_id": best_view_id,
        "analysis_frame_pos": best_analysis["best_frame_pos"],
        "prompt_frame_pos": prompt_frame_pos,
        "frame_id": render_index["frame_ids"][prompt_frame_pos],
        "flat_frame_index": flat_frame_index,
        "points_abs": points_abs,
        "labels": labels,
        "prompt_mask": prompt_mask,
        "heatmap": best_analysis["heatmap"],
        "reference_frame_path": reference_frame_path,
        "view_scores": {view_id: analysis["best_score"] for view_id, analysis in view_analyses.items()},
        "render_index": render_index,
    }

def get_prim_id_image(mesh_legacy: o3d.geometry.TriangleMesh,
                      camera_params: o3d.camera.PinholeCameraParameters,
                      width: int, height: int):
    tmesh = o3d.t.geometry.TriangleMesh.from_legacy(mesh_legacy)

    scene = o3d.t.geometry.RaycastingScene()
    _ = scene.add_triangles(tmesh)

    K = camera_params.intrinsic.intrinsic_matrix.astype(np.float64)
    E = camera_params.extrinsic.astype(np.float64)

    Kt = o3d.core.Tensor(K, dtype=o3d.core.Dtype.Float64)
    Et = o3d.core.Tensor(E, dtype=o3d.core.Dtype.Float64)

    rays = o3d.t.geometry.RaycastingScene.create_rays_pinhole(
        intrinsic_matrix=Kt,
        extrinsic_matrix=Et,
        width_px=width,
        height_px=height
    )

    ans = scene.cast_rays(rays)
    prim_id = ans["primitive_ids"].numpy().astype(np.int32)
    t_hit = ans["t_hit"].numpy().astype(np.float32)

    return prim_id, t_hit



def vote_faces_from_mask(prim_id: np.ndarray, mask: np.ndarray, num_faces: int):
    face_ids = prim_id[mask]
    face_ids = face_ids[face_ids >= 0]
    votes = np.bincount(face_ids, minlength=num_faces).astype(np.int32)

    visible_ids = prim_id[prim_id >= 0]
    visible = np.bincount(visible_ids, minlength=num_faces).astype(np.int32)

    return votes, visible


def fuse_views_to_face_mask(mesh, camera_params_list, sam_masks_list, width, height, tau=0.6):
    num_faces = np.asarray(mesh.triangles).shape[0]
    total_votes = np.zeros(num_faces, dtype=np.int64)
    total_vis = np.zeros(num_faces, dtype=np.int64)

    for cam, mask in zip(camera_params_list, sam_masks_list):
        prim_id, _ = get_prim_id_image(mesh, cam, width, height)
        votes, vis = vote_faces_from_mask(prim_id, mask.astype(bool), num_faces)
        total_votes += votes
        total_vis += vis

    ratio = total_votes / np.maximum(total_vis, 1)

    face_is_dynamic = ratio >= tau
    face_is_unseen = total_vis == 0
    face_is_low_vis = total_vis < 4   # tune this

    return face_is_dynamic, ratio, total_vis, face_is_unseen, face_is_low_vis


def load_masks_for_mesh_frame(mask_root, mesh_frame_id, num_views=16):
    frame_dir = os.path.join(mask_root, f"frame_{mesh_frame_id:04d}")
    masks = []
    for view_id in range(num_views):
        masks.append(np.load(os.path.join(frame_dir, f"view_{view_id:02d}.npy")).astype(bool))
    return masks



def split_mesh_by_face_mask(mesh: o3d.geometry.TriangleMesh, face_mask: np.ndarray):
    tris = np.asarray(mesh.triangles)
    verts = np.asarray(mesh.vertices)

    dyn_tris = tris[face_mask]
    stat_tris = tris[~face_mask]

    dyn = o3d.geometry.TriangleMesh()
    dyn.vertices = o3d.utility.Vector3dVector(verts)
    dyn.triangles = o3d.utility.Vector3iVector(dyn_tris)
    dyn.remove_unreferenced_vertices()
    dyn.remove_degenerate_triangles()
    dyn.remove_duplicated_triangles()
    dyn.remove_duplicated_vertices()
    dyn.compute_vertex_normals()

    stat = o3d.geometry.TriangleMesh()
    stat.vertices = o3d.utility.Vector3dVector(verts)
    stat.triangles = o3d.utility.Vector3iVector(stat_tris)
    stat.remove_unreferenced_vertices()
    stat.remove_degenerate_triangles()
    stat.remove_duplicated_triangles()
    stat.remove_duplicated_vertices()
    stat.compute_vertex_normals()

    return dyn, stat

def build_face_adjacency(mesh):
    tris = np.asarray(mesh.triangles)
    edge_to_faces = {}

    for fi, tri in enumerate(tris):
        edges = [
            tuple(sorted((tri[0], tri[1]))),
            tuple(sorted((tri[1], tri[2]))),
            tuple(sorted((tri[2], tri[0]))),
        ]

        for e in edges:
            edge_to_faces.setdefault(e, []).append(fi)

    neighbors = [[] for _ in range(len(tris))]

    for faces in edge_to_faces.values():
        if len(faces) == 2:
            a, b = faces
            neighbors[a].append(b)
            neighbors[b].append(a)

    return neighbors


def dilate_dynamic_on_mesh(
    face_is_dynamic,
    neighbors,
    expandable_mask=None,
    num_iters=2
):
    """
    Expand dynamic labels over the mesh graph.

    Args:
        face_is_dynamic: bool array, initial dynamic face mask.
        neighbors: face adjacency list.
        expandable_mask: bool array. If provided, only these faces can be newly added.
                         Useful for only expanding into unseen or low-visible faces.
        num_iters: number of adjacency expansion steps.

    Returns:
        refined dynamic mask.
    """
    refined = face_is_dynamic.copy()

    for _ in range(num_iters):
        new_refined = refined.copy()

        dynamic_ids = np.where(refined)[0]

        for f in dynamic_ids:
            for nb in neighbors[f]:
                if refined[nb]:
                    continue

                if expandable_mask is not None and not expandable_mask[nb]:
                    continue

                new_refined[nb] = True

        refined = new_refined

    return refined

def connected_components_faces(face_ids, neighbors):
    face_set = set(face_ids)
    visited = set()
    components = []

    for f in face_ids:
        if f in visited:
            continue

        comp = []
        queue = deque([f])
        visited.add(f)

        while queue:
            cur = queue.popleft()
            comp.append(cur)

            for nb in neighbors[cur]:
                if nb in face_set and nb not in visited:
                    visited.add(nb)
                    queue.append(nb)

        components.append(comp)

    return components

def remove_small_static_islands(face_is_dynamic, neighbors, max_static_component_size=100):
    """
    Move small disconnected static components into the dynamic region.
    """
    refined = face_is_dynamic.copy()

    static_ids = np.where(~refined)[0]
    comps = connected_components_faces(static_ids, neighbors)

    for comp in comps:
        if len(comp) <= max_static_component_size:
            refined[comp] = True

    return refined


def build_local_band_around_dynamic(face_is_dynamic, neighbors, band_iters=2):
    """
    Return a bool mask of faces that are close to the initial dynamic region.

    band_iters=1 means direct neighbors of dynamic faces.
    band_iters=2 means neighbors of neighbors.
    """
    band = face_is_dynamic.copy()
    frontier = set(np.where(face_is_dynamic)[0].tolist())

    for _ in range(band_iters):
        new_frontier = set()

        for f in frontier:
            for nb in neighbors[f]:
                if not band[nb]:
                    band[nb] = True
                    new_frontier.add(nb)

        frontier = new_frontier

        if not frontier:
            break

    # only candidate faces, excluding already dynamic ones
    candidate_band = band & (~face_is_dynamic)

    return candidate_band

def local_expand_dynamic(
    face_is_dynamic,
    neighbors,
    candidate_band,
    expandable_mask,
    min_dynamic_neighbors=2,
    num_iters=1
):
    """
    Conservative local expansion.
    Only faces inside candidate_band can be changed.
    """
    refined = face_is_dynamic.copy()

    for _ in range(num_iters):
        new_refined = refined.copy()

        candidates = np.where((~refined) & candidate_band & expandable_mask)[0]

        for f in candidates:
            dyn_count = sum(refined[nb] for nb in neighbors[f])

            if dyn_count >= min_dynamic_neighbors:
                new_refined[f] = True

        refined = new_refined

    return refined


def keep_only_components_touching_initial_dynamic(
    refined_dynamic,
    initial_dynamic,
    neighbors,
    min_touch_faces=1
):
    """
    Keep dynamic components only if they touch the initial dynamic region.
    This removes unconnected floor/background pieces added during refinement.
    """
    visited = np.zeros(len(refined_dynamic), dtype=bool)
    final_dynamic = np.zeros_like(refined_dynamic, dtype=bool)

    dynamic_ids = np.where(refined_dynamic)[0]

    for start in dynamic_ids:
        if visited[start]:
            continue

        queue = deque([start])
        visited[start] = True
        comp = []

        while queue:
            f = queue.popleft()
            comp.append(f)

            for nb in neighbors[f]:
                if refined_dynamic[nb] and not visited[nb]:
                    visited[nb] = True
                    queue.append(nb)

        comp = np.array(comp, dtype=np.int64)

        # Keep this dynamic component only if it contains/touches initial dynamic faces.
        touch_count = np.sum(initial_dynamic[comp])

        if touch_count >= min_touch_faces:
            final_dynamic[comp] = True

    return final_dynamic

def save_sam_masks_multi_frames(outputs_all, save_root, num_frames=None, num_views=16,
                                H=1080, W=1920, instance_id=0):


    os.makedirs(save_root, exist_ok=True)

    total = len(outputs_all)
    if total % num_views != 0:
        raise ValueError(f"len(outputs_all)={total} not divisible by num_views={num_views}")

    inferred_frames = total // num_views
    if num_frames is None:
        num_frames = inferred_frames
    else:
        if num_frames != inferred_frames:
            raise ValueError(f"num_frames={num_frames} but inferred {inferred_frames} from total/num_views")

    for frame_id in range(num_frames):
        frame_dir = os.path.join(save_root, f"frame_{frame_id:04d}")
        os.makedirs(frame_dir, exist_ok=True)

        for view_id in range(num_views):
            k = frame_id * num_views + view_id
            frame_dict = outputs_all[k]

            if not frame_dict:
                mask = np.zeros((H, W), dtype=bool)
            else:
                if instance_id in frame_dict:
                    mask = frame_dict[instance_id].astype(bool)
                else:

                    mask = frame_dict[next(iter(frame_dict))].astype(bool)

            np.save(os.path.join(frame_dir, f"view_{view_id:02d}.npy"), mask)

    print(f"Saved masks to: {save_root}")
    print(f"Frames: {num_frames}, Views/frame: {num_views}, Total saved: {num_frames * num_views}")

def remove_small_dynamic_components(face_is_dynamic, neighbors, min_component_faces=200):
    """
    Move small disconnected dynamic components back to static.

    Args:
        face_is_dynamic: bool array, True = dynamic face.
        neighbors: face adjacency list from build_face_adjacency(mesh).
        min_component_faces: dynamic components with fewer faces than this
                             will be moved back to static.

    Returns:
        refined bool mask.
    """
    refined = face_is_dynamic.copy()
    visited = np.zeros(len(face_is_dynamic), dtype=bool)

    dynamic_ids = np.where(face_is_dynamic)[0]

    for start in dynamic_ids:
        if visited[start]:
            continue

        queue = deque([start])
        visited[start] = True
        comp = []

        while queue:
            f = queue.popleft()
            comp.append(f)

            for nb in neighbors[f]:
                if face_is_dynamic[nb] and not visited[nb]:
                    visited[nb] = True
                    queue.append(nb)

        if len(comp) < min_component_faces:
            refined[comp] = False

    return refined

def subdivide_surface_fitting(decimated_mesh, target_mesh, iterations=1):
    subdivided_mesh = o3d.geometry.TriangleMesh.subdivide_midpoint(decimated_mesh, number_of_iterations=iterations)
    #print(subdivided_mesh)
    subdivided_mesh.compute_vertex_normals()

    pcd_target = o3d.geometry.PointCloud()
    pcd_target.points = o3d.utility.Vector3dVector(target_mesh.vertices)
    pcd_tree = o3d.geometry.KDTreeFlann(pcd_target)
    subdivided_vertices = np.array(subdivided_mesh.vertices)
    target_vertices = np.array(target_mesh.vertices)
    fitting_vertices = deepcopy(subdivided_vertices)

    for i in range(0, len(subdivided_vertices)):
        [k, index, _] = pcd_tree.search_knn_vector_3d(subdivided_vertices[i], 1)
        fitting_vertices[i] = target_vertices[np.asarray(index)]

    subdivided_mesh.vertices = o3d.utility.Vector3dVector(fitting_vertices)
    return subdivided_mesh

def read_triangle_mesh_with_trimesh(avatar_name, enable_post_processing=False):
    if enable_post_processing:
        scene_patch = trimesh.load(avatar_name, process=True)
    else:
        scene_patch = trimesh.load(avatar_name, process=False, maintain_order=True)
    mesh = o3d.geometry.TriangleMesh(
        o3d.utility.Vector3dVector(scene_patch.vertices),
        o3d.utility.Vector3iVector(scene_patch.faces)
    )
    if scene_patch.vertex_normals.size:
        mesh.vertex_normals = o3d.utility.Vector3dVector(scene_patch.vertex_normals.copy())
    if scene_patch.visual.defined:
        if scene_patch.visual.kind == 'vertex':
            mesh.vertex_colors = o3d.utility.Vector3dVector(
                scene_patch.visual.vertex_colors[:, :3] / 255)
        elif scene_patch.visual.kind == 'texture':
            uv = scene_patch.visual.uv
            if uv.shape[0] == scene_patch.vertices.shape[0]:
                mesh.triangle_uvs = o3d.utility.Vector2dVector(uv[scene_patch.faces.flatten()])
            elif uv.shape[0] != scene_patch.faces.shape[0] * 3:
                assert False
            else:
                mesh.triangle_uvs = o3d.utility.Vector2dVector(uv)
                if scene_patch.visual.material is not None and scene_patch.visual.material.image is not None:
                    if scene_patch.visual.material.image.mode == 'RGB':
                        mesh.textures = [o3d.geometry.Image(np.asarray(scene_patch.visual.material.image))]
                    else:
                        assert False
        else:
            assert False
    return mesh

def solve_sparse_least_squares_cg(L_star_gpu, D_hat_gpu, maxiter=500, tol=1e-6):
    A_T = L_star_gpu.transpose()
    AtA = A_T @ L_star_gpu
    AtB = A_T @ D_hat_gpu

    num_cols = AtB.shape[1]
    n = AtA.shape[0]
    S_recon_gpu = cp.zeros((n, num_cols), dtype=cp.float32)

    for i in range(num_cols):
        b = AtB[:, i]
        x, info = cg(AtA, b, tol=tol, maxiter=maxiter)
        if info != 0:
            print(f"CG did not converge on column {i}, info: {info}")
        S_recon_gpu[:, i] = x

    return S_recon_gpu

def build_mv_laplacian_gpu_fast(mesh, anchor_indices=[]):
    vertices = np.asarray(mesh.vertices)
    adjacency_list = mesh.adjacency_list
    n = len(vertices)

    start = time.time()
    W = compute_mv_weights_gpu(vertices, adjacency_list)
    print("Computing mv weights:", time.time() - start)

    start_time = time.time()

    # Normalize W by row sums (L = I - D⁻¹W)
    W = W.tocsr()
    row_sums = np.array(W.sum(axis=1)).flatten()
    row_inv = np.reciprocal(row_sums, where=row_sums > 1e-8)

    D_inv = diags(row_inv)
    L = identity(n, format='csr') - D_inv @ W
    print("Laplacian build time (vectorized):", time.time() - start_time)

    start = time.time()
    if len(anchor_indices) > 0:
        anchor_rows = lil_matrix((len(anchor_indices), n))
        for row_offset, ki in enumerate(anchor_indices):
            anchor_rows[row_offset, ki] = 1
        L_ext = vstack([L, anchor_rows]).tocsr()
    else:
        L_ext = L
    print("vstack method:", time.time() - start)
    return L_ext



def compute_delta_trajectories(L_ext, S):
    """
    Compute delta trajectory matrix D = L* @ S
    L_ext: (n+l) x n sparse matrix
    S: n x m matrix
    """
    return L_ext @ S  # Result is (n+l) x m

def compute_mv_weights_gpu(vertices, adjacency_list):
    n = len(vertices)
    row, col = [], []

    for i in range(n):
        neighbors = adjacency_list[i]
        row.extend([i] * len(neighbors))
        col.extend(neighbors)

    row = np.array(row, dtype=np.int32)
    col = np.array(col, dtype=np.int32)

    vertices_gpu = cp.asarray(vertices)
    vi_gpu = vertices_gpu[row]
    vj_gpu = vertices_gpu[col]

    diff_gpu = vj_gpu - vi_gpu
    dist_gpu = cp.linalg.norm(diff_gpu, axis=1) + 1e-8
    weights_gpu = 1.0 / dist_gpu

    # Transfer to CPU just once
    data = cp.asnumpy(weights_gpu)

    W = coo_matrix((data, (row, col)), shape=(n, n)).tocsr()
    return W

def compute_D1_psnr(original_mesh, decoded_mesh):
    original_vertices = np.array(original_mesh.vertices)
    decoded_vertices = np.array(decoded_mesh.vertices)

    pcd_original = o3d.geometry.PointCloud()
    pcd_original.points = o3d.utility.Vector3dVector(original_vertices)

    pcd_decoded = o3d.geometry.PointCloud()
    pcd_decoded.points = o3d.utility.Vector3dVector(decoded_vertices)
    pcd_tree = o3d.geometry.KDTreeFlann(pcd_decoded)

    MSE = 0
    for i in range(0, len(original_vertices)):
        [k, index, _] = pcd_tree.search_knn_vector_3d(original_vertices[i], 1)
        MSE += np.square(np.linalg.norm(original_vertices[i] - decoded_vertices[index]))
    MSE = MSE / len(original_vertices)
    aabb = pcd_original.get_axis_aligned_bounding_box()
    min_bound = aabb.get_min_bound()

    max_bound = aabb.get_max_bound()

    signal_peak = np.linalg.norm(max_bound - min_bound)
    psnr = 20 * np.log10(signal_peak) - 10 * np.log10(MSE)
    return psnr

def compute_MSE_RMSE(original_mesh, decoded_mesh):
    original_vertices = np.array(original_mesh.vertices)

    decoded_vertices = np.array(decoded_mesh.vertices)

    pcd_original = o3d.geometry.PointCloud()
    pcd_original.points = o3d.utility.Vector3dVector(original_vertices)

    pcd_decoded = o3d.geometry.PointCloud()
    pcd_decoded.points = o3d.utility.Vector3dVector(decoded_vertices)
    pcd_tree = o3d.geometry.KDTreeFlann(pcd_decoded)

    MSE = 0
    for i in range(0, len(original_vertices)):
        [k, index, _] = pcd_tree.search_knn_vector_3d(original_vertices[i], 1)
        MSE += np.square(np.linalg.norm(original_vertices[i] - decoded_vertices[index]))
    MSE = MSE / len(original_vertices)
    RMSE = np.sqrt(MSE)

    return np.log10(MSE), np.log10(RMSE)

def calculate_bitrate(file_size, duration):
    return file_size * 8 / duration

def compute_D2_psnr_test(original_mesh, decoded_mesh, show_plot=True):
    # Extract vertices and triangles for signed distance computation
    original_vertices = np.asarray(original_mesh.vertices)
    decoded_vertices = np.asarray(decoded_mesh.vertices)
    decoded_faces = np.asarray(decoded_mesh.triangles)

    # Compute signed distances from original vertices to decoded mesh surface
    sdf, _, _ = pcu.signed_distance_to_mesh(original_vertices, decoded_vertices, decoded_faces)

    # Use absolute distance as unsigned distance
    dists = np.abs(sdf)

    MSE = np.mean(dists ** 2)

    min_bound, max_bound = original_mesh.get_min_bound(), original_mesh.get_max_bound()
    signal_peak = np.linalg.norm(max_bound - min_bound)

    psnr = 20 * np.log10(signal_peak) - 10 * np.log10(MSE)

    # Colorize original mesh with per-vertex errors
    colors = plt.get_cmap("jet")((dists - dists.min()) / (dists.ptp() + 1e-8))[:, :3]
    colored_mesh = o3d.geometry.TriangleMesh()
    colored_mesh.vertices = o3d.utility.Vector3dVector(original_vertices)
    colored_mesh.triangles = original_mesh.triangles
    colored_mesh.vertex_colors = o3d.utility.Vector3dVector(colors)

    # Show the mesh and error histogram if requested
    if show_plot:
        o3d.visualization.draw_geometries([colored_mesh], window_name="Per-vertex Error")

        plt.figure(figsize=(6, 4))
        plt.hist(dists, bins=50, color='blue', edgecolor='black')
        plt.title("Per-vertex Distance Histogram")
        plt.xlabel("Distance (error)")
        plt.ylabel("Count")
        plt.grid(True)
        plt.tight_layout()
        plt.show()
    return psnr

def render_mesh(mesh, width=512, height=512, camera_params=None):
    """
    Headless mesh rendering compatible with Open3D 0.19.0.
    Produces a normal (RGB) and a depth image.

    Args:
        mesh (o3d.geometry.TriangleMesh): Input mesh.
        width (int): Render width.
        height (int): Render height.
        camera_params (o3d.camera.PinholeCameraParameters): Optional camera parameters.

    Returns:
        tuple: (normal_img, depth_img, depth_range)
    """
    # Copy mesh to avoid modifying the input
    mesh_copy = o3d.geometry.TriangleMesh(mesh)
    if not mesh_copy.has_vertex_normals():
        mesh_copy.compute_vertex_normals()

    # Convert normals to RGB colors in [0,1]
    normals = np.asarray(mesh_copy.vertex_normals)
    mesh_copy.vertex_colors = o3d.utility.Vector3dVector((normals + 1.0) / 2.0)

    # Create offscreen renderer
    renderer = o3d.visualization.rendering.OffscreenRenderer(width, height)

    # Unlit material → render vertex colors directly
    mat = o3d.visualization.rendering.MaterialRecord()
    mat.shader = "defaultUnlit"
    renderer.scene.add_geometry("mesh", mesh_copy, mat)

    # Camera setup
    if camera_params is not None:
        intrinsic = camera_params.intrinsic.intrinsic_matrix
        extrinsic = camera_params.extrinsic
        renderer.setup_camera(intrinsic, extrinsic, width, height)
    else:
        center = mesh_copy.get_center()
        eye = center + np.array([0, 0, 2.0])
        up = [0, 1, 0]
        renderer.scene.camera.look_at(center, eye, up)

    # ---- Rendering ----
    normal_img = np.asarray(renderer.render_to_image(), dtype=np.uint8)

    depth_f = np.asarray(renderer.render_to_depth_image())
    dmin, dmax = float(depth_f.min()), float(depth_f.max())
    if dmax > dmin:
        depth_img = ((depth_f - dmin) / (dmax - dmin) * 255).astype(np.uint8)
    else:
        depth_img = np.zeros_like(depth_f, dtype=np.uint8)

    # Clean up (no .release() in 0.19)
    renderer.scene.clear_geometry()
    del renderer

    return normal_img, depth_img, (dmin, dmax)

def compute_ssim(img1, img2, multichannel=False):
    """
    Compute SSIM between two images.

    Args:
        img1 (np.ndarray): First image.
        img2 (np.ndarray): Second image.
        multichannel (bool): True for RGB images, False for grayscale.

    Returns:
        float: SSIM score.
    """
    if multichannel:
        score = ssim(img1, img2, channel_axis=2, data_range=255)
    else:
        score = ssim(img1, img2, data_range=255)
    return score

def compute_psnr(img1, img2):
    """
    Compute PSNR between two images.

    Args:
        img1 (np.ndarray): First image.
        img2 (np.ndarray): Second image.

    Returns:
        float: PSNR score.
    """
    mse = np.mean((img1.astype(float) - img2.astype(float)) ** 2)
    if mse == 0:
        return float('inf')
    max_pixel = 255.0
    psnr = 20 * np.log10(max_pixel / np.sqrt(mse))
    return psnr

def evaluate_meshes(gt_mesh, recon_mesh, viewpoints, output_dir="renderings", width=1080, height=1920):
    """
    Evaluate two meshes by rendering normal maps and depth images from multiple viewpoints
    and computing SSIM and PSNR scores.

    Args:
        gt_mesh (o3d.geometry.TriangleMesh): Ground truth mesh.
        recon_mesh (o3d.geometry.TriangleMesh): Reconstructed mesh.
        viewpoints (list): List of PinholeCameraParameters.
        output_dir (str): Directory to save rendered images.

    Returns:
        tuple: (avg_ssim_depth, avg_ssim_normal) - Average SSIM for depth and normal map images.
    """
    # Ensure meshes have vertex normals
    if not gt_mesh.has_vertex_normals():
        gt_mesh.compute_vertex_normals()
    if not recon_mesh.has_vertex_normals():
        recon_mesh.compute_vertex_normals()

    # Create output directory
    os.makedirs(output_dir, exist_ok=True)

    ssim_scores_depth = []
    ssim_scores_normal = []
    psnr_scores_depth = []
    psnr_scores_normal = []

    # Render and compare for each viewpoint
    for i, view in enumerate(viewpoints):
        # Render both meshes (normal map and depth)
        gt_normal, gt_depth, gt_depth_range = render_mesh(gt_mesh, width=width, height=height, camera_params=view)
        recon_normal, recon_depth, recon_depth_range = render_mesh(recon_mesh, width=width, height=height, camera_params=view)

        # Debug: Print image shapes and ranges
        #print(f"View {i+1} - GT Normal Shape: {gt_normal.shape}, GT Depth Shape: {gt_depth.shape}, GT Depth Range: {gt_depth_range}")
        #print(f"View {i+1} - Recon Normal Shape: {recon_normal.shape}, Recon Depth Shape: {recon_depth.shape}, Recon Depth Range: {recon_depth_range}")
        #print(f"View {i+1} - GT Normal Mean (R,G,B): {np.mean(gt_normal, axis=(0,1))}")
        #print(f"View {i+1} - Recon Normal Mean (R,G,B): {np.mean(recon_normal, axis=(0,1))}")

        # Save renderings
        cv2.imwrite(os.path.join(output_dir, f"gt_view_{i}_normal.png"), cv2.cvtColor(gt_normal, cv2.COLOR_RGB2BGR))
        cv2.imwrite(os.path.join(output_dir, f"recon_view_{i}_normal.png"), cv2.cvtColor(recon_normal, cv2.COLOR_RGB2BGR))
        cv2.imwrite(os.path.join(output_dir, f"gt_view_{i}_depth.png"), gt_depth)
        cv2.imwrite(os.path.join(output_dir, f"recon_view_{i}_depth.png"), recon_depth)

        # Save debug difference image (normal map)
        normal_diff = np.abs(gt_normal.astype(float) - recon_normal.astype(float))
        normal_diff = (normal_diff / normal_diff.max() * 255).astype(np.uint8) if normal_diff.max() > 0 else normal_diff.astype(np.uint8)
        cv2.imwrite(os.path.join(output_dir, f"view_{i}_normal_diff.png"), cv2.cvtColor(normal_diff, cv2.COLOR_RGB2BGR))

        # Compute SSIM for depth and normal maps
        score_depth = compute_ssim(gt_depth, recon_depth, multichannel=False)
        score_normal = compute_ssim(gt_normal, recon_normal, multichannel=True)
        ssim_scores_depth.append(score_depth)
        ssim_scores_normal.append(score_normal)
        #print(f"View {i+1} - Depth SSIM: {score_depth:.4f}, Normal SSIM: {score_normal:.4f}")

        # Compute PSNR for completeness
        psnr_depth = compute_psnr(gt_depth, recon_depth)
        psnr_normal = compute_psnr(gt_normal, recon_normal)
        psnr_scores_depth.append(psnr_depth)
        psnr_scores_normal.append(psnr_normal)
        #print(f"View {i+1} - Depth PSNR: {psnr_depth:.4f}, Normal PSNR: {psnr_normal:.4f}")

    # Compute average SSIM
    avg_ssim_depth = np.mean(ssim_scores_depth) if ssim_scores_depth else 0
    avg_ssim_normal = np.mean(ssim_scores_normal) if ssim_scores_normal else 0
    #print(f"Average Depth SSIM: {avg_ssim_depth:.4f}")
    #print(f"Average Normal SSIM: {avg_ssim_normal:.4f}")

    avg_psnr_depth = np.mean(psnr_scores_depth) if psnr_scores_depth else 0
    avg_psnr_normal = np.mean(psnr_scores_normal) if psnr_scores_normal else 0
    # print(f"Average Depth PSNR: {avg_psnr_depth:.4f}")
    # print(f"Average Normal PSNR: {avg_psnr_normal:.4f}")

    return avg_ssim_depth, avg_ssim_normal, avg_psnr_depth, avg_psnr_normal