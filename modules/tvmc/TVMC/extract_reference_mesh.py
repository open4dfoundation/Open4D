import argparse
import os.path
from copy import deepcopy
import numpy as np
import open3d as o3d
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

parser = argparse.ArgumentParser(description="Extract reference mesh.")
parser.add_argument('--dataset', type=str, required=True, help="Dataset name (e.g., 'basketball_player')")
parser.add_argument('--num_frames', type=int, required=True, help="Number of frames to process")
parser.add_argument('--num_centers', type=int, required=True, help="Number of volume centers (pointCount)")
parser.add_argument('--inputDir', type=str, required=True, help="Input path for the deformed meshes")
parser.add_argument('--outputDir', type=str, required=True, help="Output path for the reference mesh")
parser.add_argument('--firstIndex', type=int, required=True, help="first index")
parser.add_argument('--lastIndex', type=int, required=True, help="last index")
parser.add_argument('--key', type=int, required=True, help="key mesh")

args = parser.parse_args()

dataset = args.dataset
num_frames = args.num_frames
num_centers = args.num_centers
inputDir = args.inputDir
outputDir = args.outputDir
firstIndex = args.firstIndex
lastIndex = args.lastIndex
key = args.key



deformed_meshes = []
all_meshes = o3d.geometry.TriangleMesh()
for i in range(firstIndex, lastIndex + 1):
    mesh = o3d.io.read_triangle_mesh(os.path.join(inputDir, f"deformed_{i:03}.obj"))
    mesh.compute_vertex_normals()
    deformed_meshes.append(mesh)
    all_meshes += mesh
#o3d.visualization.draw_geometries([all_meshes])
reference_pcd = o3d.geometry.PointCloud()
reference_pcd.points = o3d.utility.Vector3dVector(all_meshes.vertices)
reference_pcd.normals = o3d.utility.Vector3dVector(all_meshes.vertex_normals)



GoF = 0
# key index of the key frame to fit (self-contact degree evaluation)
fitting_meshes = []
for i in range(0, num_frames):
    GoF += 1
    if i == key:
        continue
    deformed_number_of_triangles = round(np.array(deformed_meshes[key].triangles).__len__() / 4)
    decimated_mesh_i = o3d.geometry.TriangleMesh.simplify_quadric_decimation(deformed_meshes[i], deformed_number_of_triangles,
                                                                             boundary_weight=8000)
    #print(decimated_mesh_i)
    fitting_mesh_i = subdivide_surface_fitting(decimated_mesh_i, deformed_meshes[key], 1)
    #print(fitting_mesh_i)
    fitting_mesh_i.compute_vertex_normals()
    fitting_meshes.append(fitting_mesh_i)
fitting_meshes.append(deformed_meshes[key])
#o3d.visualization.draw_geometries(fitting_meshes)




all_meshes = o3d.geometry.TriangleMesh()
for i in range(0, GoF):
    all_meshes += fitting_meshes[i]
#print(all_meshes, all_meshes.has_vertex_normals())
reference_pcd = o3d.geometry.PointCloud()
reference_pcd.points = o3d.utility.Vector3dVector(all_meshes.vertices)
reference_pcd.normals = o3d.utility.Vector3dVector(all_meshes.vertex_normals)
#print(reference_pcd, reference_pcd.has_normals())
print('run Poisson surface reconstruction')
with o3d.utility.VerbosityContextManager(o3d.utility.VerbosityLevel.Debug) as cm:
    pre_reference_mesh, densities = o3d.geometry.TriangleMesh.create_from_point_cloud_poisson(reference_pcd, depth=9, linear_fit=True)
#print(pre_reference_mesh)
pre_reference_mesh.compute_vertex_normals()

pre_reference_mesh.paint_uniform_color([0.7, 0.7, 0.7])
# o3d.visualization.draw_geometries([pre_reference_mesh])

reference_mesh = o3d.geometry.TriangleMesh.simplify_quadric_decimation(pre_reference_mesh, round(np.array(deformed_meshes[key].triangles).__len__() * 0.25), boundary_weight=8000)
print(reference_mesh)
reference_mesh.compute_vertex_normals()

#o3d.visualization.draw_geometries([reference_mesh])




#print(np.array(reference_mesh.vertices).__len__())
#decimated_reference_mesh = o3d.geometry.TriangleMesh.simplify_quadric_decimation(reference_mesh, np.array(deformed_meshes[key].triangles).__len__(), boundary_weight=8000)
#print(decimated_reference_mesh)

o3d.io.write_triangle_mesh(os.path.join(outputDir, "decimated_reference_mesh.obj"), reference_mesh, write_vertex_normals=False, write_vertex_colors=False, write_triangle_uvs=False)
