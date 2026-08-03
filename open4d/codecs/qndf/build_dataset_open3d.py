"""Build a component-preserving QNDF training pair with Open3D.

The original SSP binary simplifies a disconnected mesh as one surface.  On the
basketball sequence that can discard small components or project them onto a
nearby, unrelated component.  This builder keeps each connected component
independent through simplification, subdivision, and closest-point projection.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
import open3d as o3d
import trimesh


def clean_mesh(mesh: o3d.geometry.TriangleMesh) -> o3d.geometry.TriangleMesh:
    mesh = o3d.geometry.TriangleMesh(mesh)
    # Do not merge coincident vertices here. Disconnected components in scanned
    # meshes can touch at the same coordinates; welding those vertices turns the
    # entire model into one component before the split.
    mesh.remove_duplicated_triangles()
    mesh.remove_degenerate_triangles()
    mesh.remove_unreferenced_vertices()

    vertices = np.asarray(mesh.vertices)
    triangles = np.asarray(mesh.triangles)
    if len(triangles):
        a, b, c = vertices[triangles[:, 0]], vertices[triangles[:, 1]], vertices[triangles[:, 2]]
        twice_area = np.linalg.norm(np.cross(b - a, c - a), axis=1)
        scale = max(float(np.ptp(vertices, axis=0).max()), 1.0)
        keep = twice_area > np.finfo(np.float64).eps * scale * scale * 64
        mesh.triangles = o3d.utility.Vector3iVector(triangles[keep])
        mesh.remove_unreferenced_vertices()
    return mesh


def split_components(mesh: o3d.geometry.TriangleMesh) -> list[o3d.geometry.TriangleMesh]:
    labels, counts, _ = mesh.cluster_connected_triangles()
    labels = np.asarray(labels)
    components: list[o3d.geometry.TriangleMesh] = []
    for label in np.argsort(-np.asarray(counts)):
        component = o3d.geometry.TriangleMesh(mesh)
        component.remove_triangles_by_mask(labels != label)
        component.remove_unreferenced_vertices()
        if len(component.triangles):
            components.append(component)
    return components


def allocate_face_budget(face_counts: np.ndarray, total: int) -> np.ndarray:
    """Allocate a bounded proportional budget while retaining every component."""
    if total < len(face_counts):
        raise ValueError(f"coarse face budget {total} is smaller than {len(face_counts)} components")
    capacity = face_counts.astype(np.int64)
    target = min(int(total), int(capacity.sum()))
    allocation = np.ones(len(capacity), dtype=np.int64)
    remaining = target - len(allocation)

    while remaining:
        room = capacity - allocation
        active = room > 0
        if not np.any(active):
            break
        weights = capacity.astype(np.float64)
        ideal = np.zeros_like(weights)
        ideal[active] = remaining * weights[active] / weights[active].sum()
        add = np.minimum(room, np.floor(ideal).astype(np.int64))
        if not np.any(add):
            order = np.argsort(-(ideal - np.floor(ideal)))
            for index in order:
                if room[index] > 0 and remaining:
                    add[index] += 1
                    remaining -= 1
            allocation += add
            continue
        allocation += add
        remaining -= int(add.sum())
    return allocation


def closest_points(
    vertices: np.ndarray, original: o3d.geometry.TriangleMesh
) -> np.ndarray:
    scene = o3d.t.geometry.RaycastingScene()
    tensor_mesh = o3d.t.geometry.TriangleMesh.from_legacy(original)
    scene.add_triangles(tensor_mesh)
    query = o3d.core.Tensor(vertices.astype(np.float32), dtype=o3d.core.Dtype.Float32)
    return scene.compute_closest_points(query)["points"].numpy().astype(np.float64)


def combine(meshes: list[o3d.geometry.TriangleMesh]) -> o3d.geometry.TriangleMesh:
    vertices: list[np.ndarray] = []
    triangles: list[np.ndarray] = []
    offset = 0
    for mesh in meshes:
        component_vertices = np.asarray(mesh.vertices)
        vertices.append(component_vertices)
        triangles.append(np.asarray(mesh.triangles) + offset)
        offset += len(component_vertices)
    result = o3d.geometry.TriangleMesh()
    result.vertices = o3d.utility.Vector3dVector(np.vstack(vertices))
    result.triangles = o3d.utility.Vector3iVector(np.vstack(triangles))
    return result


def build(mesh_name: str, coarse_size: int, num_subdiv: int, root: Path) -> dict:
    source_path = root / "objs_original" / f"{mesh_name}.obj"
    # Open3D's OBJ reader welds coincident positions. Load without processing in
    # trimesh and then convert so component boundaries encoded by distinct OBJ
    # vertex indices survive.
    loaded = trimesh.load(source_path, force="mesh", process=False)
    source = o3d.geometry.TriangleMesh()
    source.vertices = o3d.utility.Vector3dVector(np.asarray(loaded.vertices))
    source.triangles = o3d.utility.Vector3iVector(np.asarray(loaded.faces))
    source = clean_mesh(source)
    if not len(source.triangles):
        raise ValueError(f"{source_path} contains no usable triangles")

    vertices = np.asarray(source.vertices)
    bbox_min = vertices.min(axis=0)
    scale = float((vertices - bbox_min).max())
    if not np.isfinite(scale) or scale <= 0:
        raise ValueError(f"{source_path} has invalid bounds")
    source.vertices = o3d.utility.Vector3dVector((vertices - bbox_min) / scale)

    components = split_components(source)
    face_counts = np.asarray([len(component.triangles) for component in components])
    budgets = allocate_face_budget(face_counts, coarse_size)
    inputs: list[o3d.geometry.TriangleMesh] = []
    targets: list[o3d.geometry.TriangleMesh] = []
    component_metadata: list[dict] = []

    for index, (original, budget) in enumerate(zip(components, budgets)):
        if budget < len(original.triangles):
            coarse = original.simplify_quadric_decimation(int(budget))
            coarse = clean_mesh(coarse)
        else:
            coarse = o3d.geometry.TriangleMesh(original)
        if not len(coarse.triangles):
            raise RuntimeError(f"component {index} vanished during simplification")

        subdivided = o3d.geometry.TriangleMesh(coarse)
        for _ in range(num_subdiv):
            subdivided = subdivided.subdivide_midpoint(number_of_iterations=1)
        projected = o3d.geometry.TriangleMesh(subdivided)
        projected.vertices = o3d.utility.Vector3dVector(
            closest_points(np.asarray(subdivided.vertices), original)
        )
        inputs.append(subdivided)
        targets.append(projected)
        component_metadata.append(
            {
                "index": index,
                "original_vertices": len(original.vertices),
                "original_faces": len(original.triangles),
                "allocated_coarse_faces": int(budget),
                "actual_coarse_vertices": len(coarse.vertices),
                "actual_coarse_faces": len(coarse.triangles),
                "training_vertices": len(subdivided.vertices),
                "training_faces": len(subdivided.triangles),
            }
        )

    input_mesh, target_mesh = combine(inputs), combine(targets)
    experiment = root / "experiments" / mesh_name
    experiment.mkdir(parents=True, exist_ok=True)
    stem = f"f{coarse_size}_s{num_subdiv}"
    input_path = experiment / f"input_{stem}.obj"
    output_path = experiment / f"output_{stem}.obj"
    transform_path = experiment / f"transform_{stem}.json"
    if not o3d.io.write_triangle_mesh(str(input_path), input_mesh, write_vertex_normals=False):
        raise OSError(f"failed to write {input_path}")
    if not o3d.io.write_triangle_mesh(str(output_path), target_mesh, write_vertex_normals=False):
        raise OSError(f"failed to write {output_path}")

    metadata = {
        "source": str(source_path),
        "bbox_min": bbox_min.tolist(),
        "scale": scale,
        "component_count": len(components),
        "coarse_face_budget": coarse_size,
        "num_subdiv": num_subdiv,
        "training_vertices": len(input_mesh.vertices),
        "training_faces": len(input_mesh.triangles),
        "components": component_metadata,
    }
    transform_path.write_text(json.dumps(metadata, indent=2) + "\n")
    return metadata


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mesh_name")
    parser.add_argument("--coarse-size", "-cs", type=int, default=5000)
    parser.add_argument("--num-subdiv", "-ns", type=int, default=3)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args()
    metadata = build(args.mesh_name, args.coarse_size, args.num_subdiv, args.root.resolve())
    print(json.dumps(metadata, indent=2))


if __name__ == "__main__":
    main()
