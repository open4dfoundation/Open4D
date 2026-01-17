import argparse
import os
import open3d as o3d
import numpy as np
from copy import deepcopy
import trimesh


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
    # EDIT: next 4 lines replace to maintain order even in case of degenerate and non referenced
    # scene_patch = trimesh.load(avatar_name,process=enable_post_processing)
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
        # either texture or vertex colors if no uvs present.
        if scene_patch.visual.kind == 'vertex':
            mesh.vertex_colors = o3d.utility.Vector3dVector(
                scene_patch.visual.vertex_colors[:, :3] / 255)  # no alpha channel support
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

parser = argparse.ArgumentParser(description="Get displacements.")
parser.add_argument('--dataset', type=str, required=True, help="Dataset name (e.g., 'basketball_player')")
parser.add_argument('--num_frames', type=int, required=True, help="Number of frames to process")
parser.add_argument('--num_centers', type=int, required=True, help="Number of volume centers (pointCount)")
parser.add_argument('--target_mesh_path', type=str, required=True, help="Input path for the target meshes (original meshes)")
parser.add_argument('--firstIndex', type=int, required=True, help="first index")
parser.add_argument('--lastIndex', type=int, required=True, help="last index")

args = parser.parse_args()

dataset = args.dataset
num_frames = args.num_frames
num_centers = args.num_centers
target_mesh_path = args.target_mesh_path
firstIndex = args.firstIndex
lastIndex = args.lastIndex


obj_files = [f for f in os.listdir(target_mesh_path) if f.endswith('.obj')]
i = firstIndex
for obj_file in obj_files:
    dynamic_deformed = o3d.io.read_triangle_mesh(f'../tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/{dataset}_{num_centers}/reference/deformed_reference_mesh_{i:03}.obj')
    original_i = o3d.io.read_triangle_mesh(os.path.join(target_mesh_path, obj_file))

    dynamic_deformed.compute_vertex_normals()
    original_i.compute_vertex_normals()
    #o3d.visualization.draw_geometries([reconstruct_dancer_i])
    fitting_mesh_dancer_i = subdivide_surface_fitting(dynamic_deformed, original_i, 1)

    o3d.io.write_triangle_mesh(f'../tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/{dataset}_{num_centers}/reference/fitting_mesh_{i:03}.obj', fitting_mesh_dancer_i, write_vertex_normals=False, write_vertex_colors=False, write_triangle_uvs=False)
    #o3d.visualization.draw_geometries([fitting_mesh_dancer_i])
    i += 1

loaded_decimated_reference_mesh = o3d.io.read_triangle_mesh(f'../tvm-editing/TVMEditor.Test/bin/Release/net5.0/Data/{dataset}_{num_centers}/reference_mesh/decimated_reference_mesh.obj', enable_post_processing=False)
subdivided_decimated_reference_mesh = o3d.geometry.TriangleMesh.subdivide_midpoint(loaded_decimated_reference_mesh, number_of_iterations=1)
subdivided_decimated_reference_mesh_vertices = np.array(subdivided_decimated_reference_mesh.vertices)

displacements = []
for i in range(firstIndex, lastIndex + 1):
    fitting_mesh_dancer_i = read_triangle_mesh_with_trimesh(f'../tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/{dataset}_{num_centers}/reference/fitting_mesh_{i:03}.obj', enable_post_processing=False)

    fitting_mesh_vertices = np.array(fitting_mesh_dancer_i.vertices)
    displacement_i = fitting_mesh_vertices - subdivided_decimated_reference_mesh_vertices
    np.savetxt(f'../tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/{dataset}_{num_centers}/reference/displacements_{dataset}_{i:03}.txt', displacement_i, fmt='%8f')
    displacements.append(displacement_i)



for i in range(firstIndex, lastIndex + 1):
    displacement = np.loadtxt(f'../tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/{dataset}_{num_centers}/reference/displacements_{dataset}_{i:03}.txt')
    pcd = o3d.geometry.PointCloud()
    points = displacement
    pcd.points = o3d.utility.Vector3dVector(points)
    points=np.asarray(pcd.points)
    dtype = o3d.core.float32
    p_tensor = o3d.core.Tensor(points, dtype=dtype)
    pc = o3d.t.geometry.PointCloud(p_tensor)
    o3d.t.io.write_point_cloud(f'../tvm-editing/TVMEditor.Test/bin/Release/net5.0/Data/{dataset}_{num_centers}/reference_mesh/dis_{dataset}_{i:03}.ply', pc, write_ascii=True)