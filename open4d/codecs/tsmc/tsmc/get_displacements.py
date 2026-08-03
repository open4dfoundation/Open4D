import argparse
import os
import open3d as o3d
import numpy as np
from copy import deepcopy
import trimesh
import re
import subprocess
from scipy.spatial import cKDTree
from util import  subdivide_surface_fitting, read_triangle_mesh_with_trimesh



parser = argparse.ArgumentParser(description="Get displacements.")
parser.add_argument('--dataset', type=str, required=True, help="Dataset name (e.g., 'basketball_player')")
parser.add_argument('--num_frames', type=int, required=True, help="Number of frames to process")
parser.add_argument('--num_centers', type=int, required=True, help="Number of volume centers (pointCount)")
parser.add_argument('--target_mesh_path', type=str, required=True, help="Input path for the target meshes (original meshes)")
parser.add_argument('--firstIndex', type=int, required=True, help="first index")
parser.add_argument('--lastIndex', type=int, required=True, help="last index")
parser.add_argument('--group_idx', type=int, default=1, help="Group index (e.g., group=1 frame[0:num_frames])")

args = parser.parse_args()

dataset = args.dataset
num_frames = args.num_frames
num_centers = args.num_centers
target_mesh_path = args.target_mesh_path
firstIndex = args.firstIndex
lastIndex = args.lastIndex
group_idx = args.group_idx

re_pattern = re.compile('.+?(\d+)\.([a-zA-Z0-9+])')
obj_files = [f for f in os.listdir(target_mesh_path) if f.endswith('.obj')]
obj_files = sorted(obj_files, key=lambda x: int(re_pattern.match(x).groups()[0]))
#print(obj_files.__len__())
selected_obj_files = obj_files[(group_idx-1) * num_frames:(group_idx) *num_frames]
print(selected_obj_files.__len__(), selected_obj_files)


i = firstIndex
for obj_file in selected_obj_files:
    dynamic_deformed = o3d.io.read_triangle_mesh(f'../tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/{dataset}_{num_centers}/reference/deformed_reference_mesh_{i:03}.obj')
    original_i = o3d.io.read_triangle_mesh(os.path.join(target_mesh_path, obj_file))

    dynamic_deformed.compute_vertex_normals()
    original_i.compute_vertex_normals()
    fitting_mesh_dancer_i = subdivide_surface_fitting(dynamic_deformed, original_i, 1)

    o3d.io.write_triangle_mesh(f'../tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/{dataset}_{num_centers}/reference/fitting_mesh_{i:03}.obj', fitting_mesh_dancer_i, write_vertex_normals=False, write_vertex_colors=False, write_triangle_uvs=False)
    #o3d.visualization.draw_geometries([fitting_mesh_dancer_i])
    i += 1

loaded_decimated_reference_mesh = o3d.io.read_triangle_mesh(f'../tvm-editing/TVMEditor.Test/bin/Release/net5.0/Data/{dataset}_{num_centers}/reference_mesh/decimated_reference_mesh.obj', enable_post_processing=False)
print(loaded_decimated_reference_mesh)
result = subprocess.run([
    '../draco/build/draco_encoder',
    '-i', f'../tvm-editing/TVMEditor.Test/bin/Release/net5.0/Data/{dataset}_{num_centers}/reference_mesh/decimated_reference_mesh.obj',
    '-o', f'../tvm-editing/TVMEditor.Test/bin/Release/net5.0/Data/{dataset}_{num_centers}/reference_mesh/decimated_reference_mesh.drc',
    '-qp', str('14'),
    '-cl', '7'
], capture_output=True, text=True)
print(result.stdout)
print(result.stderr)


result = subprocess.run([
    '../draco/build/draco_decoder',
    '-i', f'../tvm-editing/TVMEditor.Test/bin/Release/net5.0/Data/{dataset}_{num_centers}/reference_mesh/decimated_reference_mesh.drc',
    '-o', fr'../tvm-editing/TVMEditor.Test/bin/Release/net5.0/Data/{dataset}_{num_centers}/reference_mesh/others/decoded_decimated_reference_mesh.obj'
], capture_output=True, text=True)
print(result.stdout)
print(result.stderr)

loaded_decoded_decimated_reference_mesh = o3d.io.read_triangle_mesh(f'../tvm-editing/TVMEditor.Test/bin/Release/net5.0/Data/{dataset}_{num_centers}/reference_mesh/others/decoded_decimated_reference_mesh.obj', enable_post_processing=False)
print(loaded_decoded_decimated_reference_mesh)


subdivided_decimated_reference_mesh = o3d.geometry.TriangleMesh.subdivide_midpoint(loaded_decimated_reference_mesh, number_of_iterations=1)
subdivided_decimated_reference_mesh_vertices = np.array(subdivided_decimated_reference_mesh.vertices)
subdivided_decoded_decimated_reference_mesh = o3d.geometry.TriangleMesh.subdivide_midpoint(loaded_decoded_decimated_reference_mesh, number_of_iterations=1)
subdivided_decoded_decimated_reference_mesh_vertices = np.array(subdivided_decoded_decimated_reference_mesh.vertices)
print(subdivided_decimated_reference_mesh_vertices.shape, subdivided_decoded_decimated_reference_mesh_vertices.shape)


displacements = []
for i in range(firstIndex, lastIndex + 1):
    fitting_mesh_dancer_i = read_triangle_mesh_with_trimesh(f'../tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/{dataset}_{num_centers}/reference/fitting_mesh_{i:03}.obj', enable_post_processing=False)

    fitting_mesh_vertices = np.array(fitting_mesh_dancer_i.vertices)
    displacement_i = fitting_mesh_vertices - subdivided_decimated_reference_mesh_vertices
    np.savetxt(f'../tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/{dataset}_{num_centers}/reference/displacements_{dataset}_{i:03}.txt', displacement_i, fmt='%8f')
    displacements.append(displacement_i)


# Reorder displacements to align with the DECODED reference mesh, because Draco changes the order of vertices
tree = cKDTree(subdivided_decoded_decimated_reference_mesh_vertices)
_, vertex_mapping = tree.query(subdivided_decimated_reference_mesh_vertices, k=1)

inverse_mapping = np.argsort(vertex_mapping)

for i in range(firstIndex, lastIndex + 1):
    displacement_path = f'../tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/{dataset}_{num_centers}/reference/displacements_{dataset}_{i:03}.txt'
    displacement = np.loadtxt(displacement_path)
    
    # Apply the inverse mapping
    reordered_displacement = displacement[inverse_mapping]
    
    # Overwrite or save to a new file
    np.savetxt(displacement_path, reordered_displacement, fmt='%8f')



'''
mesh = loaded_decoded_decimated_reference_mesh
print(mesh)
subdivided_mesh = o3d.geometry.TriangleMesh.subdivide_midpoint(mesh, number_of_iterations=1)
print(subdivided_mesh)
subdivided_decoded_mesh_vertices = np.array(subdivided_mesh.vertices)
for k in range(firstIndex, lastIndex+1):
    vertices = deepcopy(subdivided_decoded_mesh_vertices)
    #reconstructed_displacement = np.loadtxt(os.path.join(output_dir, f'displacements_{dataset}_{k:03d}.txt'))
    reconstructed_displacement = np.loadtxt(f'../tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/{dataset}_{num_centers}/reference/displacements_{dataset}_{k:03}.txt')
    for i in range(0, len(subdivided_decoded_mesh_vertices)):
        vertices[i] += reconstructed_displacement[i]
    reconstruct_mesh = o3d.geometry.TriangleMesh()
    reconstruct_mesh.triangles = subdivided_mesh.triangles
    reconstruct_mesh.vertices = o3d.utility.Vector3dVector(vertices)
    reconstruct_mesh.compute_vertex_normals()
    o3d.visualization.draw_geometries([reconstruct_mesh])
    #o3d.io.write_triangle_mesh(os.path.join("/home/frozzzen/Documents/Github_SINRG/TSMC/tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/answering_2000/reference/test", f'{dataset}_{k:03d}.obj'), reconstruct_mesh)
'''





# Compress displacements with Draco, convert .txt files into .ply files, for comparison
for i in range(firstIndex, lastIndex + 1):
    displacement_path = f'../tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/{dataset}_{num_centers}/reference/displacements_{dataset}_{i:03}.txt'
    displacement = np.loadtxt(displacement_path)
    pcd = o3d.geometry.PointCloud()
    points = displacement
    pcd.points = o3d.utility.Vector3dVector(points)
    points=np.asarray(pcd.points)
    dtype = o3d.core.float32
    p_tensor = o3d.core.Tensor(points, dtype=dtype)
    pc = o3d.t.geometry.PointCloud(p_tensor)
    o3d.t.io.write_point_cloud(f'../tvm-editing/TVMEditor.Test/bin/Release/net5.0/Data/{dataset}_{num_centers}/reference_mesh/dis_{dataset}_{i:03}.ply', pc, write_ascii=True)

times = []
input_encoder_path = fr"../tvm-editing/TVMEditor.Test/bin/Release/net5.0/Data/{dataset}_{num_centers}/reference_mesh"
output_encoder_path = fr"../tvm-editing/TVMEditor.Test/bin/Release/net5.0/Data/{dataset}_{num_centers}/reference_mesh/GoF{num_frames}"
print(input_encoder_path)
if not os.path.exists(output_encoder_path):
    os.makedirs(output_encoder_path)
qp = '11'
for i in range(firstIndex, lastIndex + 1):
    # print(os.path.join(input_encoder_path, f"dis_{dataset}_{i:03}.ply"))
    result = subprocess.run([
        '../draco/build/draco_encoder',
        '-point_cloud',
        '-i', os.path.join(input_encoder_path, f"dis_{dataset}_{i:03}.ply"),
        '-o', os.path.join(output_encoder_path, f"dis_{dataset}_{i:03}.drc"),
        '-qp', str(qp),
        '-cl', '0'
    ], capture_output=True, text=True)
    print(result.stdout)
    time_pattern = re.compile(r"(\d+) ms to encode")
    match = time_pattern.search(result.stdout)
    if match:
        times.append(int(match.group(1)))

if times:
    mean_time = sum(times) / len(times)
    print(f"Mean encoding time: {mean_time:.6f} ms")
#print(f"Average encoding time for qp {qp}: {mean_time:.2f} ms/n/n")
decoding_time = 0
times = []
for i in range(firstIndex, lastIndex + 1):
    result = subprocess.run([
        '../draco/build/draco_decoder',
        '-i',
        fr'../tvm-editing/TVMEditor.Test/bin/Release/net5.0/Data/{dataset}_{num_centers}/reference_mesh/GoF{num_frames}/dis_{dataset}_{i:03}.drc',
        '-o',
        fr'../tvm-editing/TVMEditor.Test/bin/Release/net5.0/Data/{dataset}_{num_centers}/reference_mesh/GoF{num_frames}/decoded_{dataset}_{i:03}_displacements.ply'
    ], capture_output=True, text=True)
    print(result.stdout)

    time_pattern = re.compile(r"(\d+) ms to decode")
    match = time_pattern.search(result.stdout)
    if match:
        times.append(int(match.group(1)))
        decoding_time += int(match.group(1))

if times:
    mean_time = sum(times) / len(times)
    print(f"Mean decoding time: {mean_time:.6f} ms")
#print(f"Average encoding time for qp {qp}: {mean_time:.2f} ms/n/n")
