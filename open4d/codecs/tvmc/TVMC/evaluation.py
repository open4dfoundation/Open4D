import argparse
import open3d as o3d
import numpy as np
import trimesh
import subprocess
import os
import time
from scipy.spatial import cKDTree
import re
import json


parser = argparse.ArgumentParser(description="Evaluation.")
parser.add_argument('--dataset', type=str, required=True, help="Dataset name (e.g., 'basketball_player')")
parser.add_argument('--num_frames', type=int, required=True, help="Number of frames to process")
parser.add_argument('--num_centers', type=int, required=True, help="Number of volume centers (pointCount)")
parser.add_argument('--firstIndex', type=int, required=True, help="first index")
parser.add_argument('--lastIndex', type=int, required=True, help="last index")
parser.add_argument('--fileNamePrefix', type=str, required=True, help="fileNamePrefix")
parser.add_argument('--encoderPath', type=str, required=True, help="encoderPath")
parser.add_argument('--decoderPath', type=str, required=True, help="decoderPath")
parser.add_argument('--qp', type=int, required=True, help="qp")
parser.add_argument('--outputPath', type=str, required=True, help="Path for reconstructed mesh")


args = parser.parse_args()

dataset = args.dataset
num_frames = args.num_frames
num_centers = args.num_centers
firstIndex = args.firstIndex
lastIndex = args.lastIndex
fileNamePrefix = args.fileNamePrefix
encoderPath = args.encoderPath
decoderPath = args.decoderPath
qp = args.qp
outputPath = args.outputPath

decoding_time = 0


def is_fresh(path, *inputs):
    if not os.path.exists(path):
        return False
    output_mtime = os.path.getmtime(path)
    return all(os.path.exists(item) and os.path.getmtime(item) <= output_mtime
               for item in inputs)


def subdivide_surface_fitting(decimated_mesh, target_mesh, iterations=1):
    subdivided_mesh = o3d.geometry.TriangleMesh.subdivide_midpoint(decimated_mesh, number_of_iterations=iterations)
    #print(subdivided_mesh)
    subdivided_mesh.compute_vertex_normals()

    subdivided_vertices = np.asarray(subdivided_mesh.vertices)
    target_vertices = np.asarray(target_mesh.vertices)
    _, indices = cKDTree(target_vertices).query(
        subdivided_vertices, k=1, workers=-1
    )
    fitting_vertices = target_vertices[indices]

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


def directed_metrics(source_mesh, target_mesh):
    source_vertices = np.asarray(source_mesh.vertices)
    target_vertices = np.asarray(target_mesh.vertices)
    target_normals = np.asarray(target_mesh.vertex_normals)
    distances, indices = cKDTree(target_vertices).query(
        source_vertices, k=1, workers=-1
    )
    mse = np.mean(distances * distances)
    diff = source_vertices - target_vertices[indices]
    point_to_plane_mse = np.mean(
        np.sum(diff * target_normals[indices], axis=1) ** 2
    )
    signal_peak = np.linalg.norm(
        source_mesh.get_max_bound() - source_mesh.get_min_bound()
    )
    d1 = 20 * np.log10(signal_peak) - 10 * np.log10(mse)
    d2 = 20 * np.log10(signal_peak) - 10 * np.log10(point_to_plane_mse)
    return d1, d2, np.log10(mse), np.log10(np.sqrt(mse))


reference_output_dir = (
    f'../tvm-editing/TVMEditor.Test/bin/Release/net5.0/'
    f'output/{dataset}_{num_centers}/reference'
)
reference_data_dir = (
    f'../tvm-editing/TVMEditor.Test/bin/Release/net5.0/'
    f'Data/{dataset}_{num_centers}/reference_mesh'
)

for i in range(firstIndex, lastIndex + 1):
    fitting_path = os.path.join(reference_output_dir, f'fitting_mesh_{i:03}.obj')
    dynamic_path = os.path.join(
        reference_output_dir, f'deformed_reference_mesh_{i:03}.obj'
    )
    original_path = (
        f'../arap-volume-tracking/data/{dataset}/{fileNamePrefix}{i:03}.obj'
    )
    if is_fresh(fitting_path, dynamic_path, original_path):
        continue
    dynamic_deformed = o3d.io.read_triangle_mesh(
        dynamic_path
    )
    original_i = o3d.io.read_triangle_mesh(original_path)
    dynamic_deformed.compute_vertex_normals()
    original_i.compute_vertex_normals()
    fitting_mesh_dancer_i = subdivide_surface_fitting(
        dynamic_deformed, original_i, 1
    )
    o3d.io.write_triangle_mesh(
        fitting_path, fitting_mesh_dancer_i, write_vertex_normals=False,
        write_vertex_colors=False, write_triangle_uvs=False
    )

loaded_decimated_reference_mesh = o3d.io.read_triangle_mesh(
    os.path.join(reference_data_dir, 'decimated_reference_mesh.obj'),
    enable_post_processing=False
)
subdivided_decimated_reference_mesh = o3d.geometry.TriangleMesh.subdivide_midpoint(loaded_decimated_reference_mesh, number_of_iterations=1)
subdivided_decimated_reference_mesh_vertices = np.array(subdivided_decimated_reference_mesh.vertices)
# o3d.visualization.draw_geometries([subdivided_decimated_reference_mesh])

displacements = []
for i in range(firstIndex, lastIndex + 1):
    fitting_path = os.path.join(reference_output_dir, f'fitting_mesh_{i:03}.obj')
    displacement_path = os.path.join(
        reference_output_dir, f'displacements_{dataset}_{i:03}.txt'
    )
    reference_mesh_path = os.path.join(
        reference_data_dir, 'decimated_reference_mesh.obj'
    )
    if is_fresh(displacement_path, fitting_path, reference_mesh_path):
        displacement_i = np.loadtxt(displacement_path)
    else:
        fitting_mesh_dancer_i = read_triangle_mesh_with_trimesh(
            fitting_path,
            enable_post_processing=False
        )
        fitting_mesh_vertices = np.asarray(fitting_mesh_dancer_i.vertices)
        displacement_i = (
            fitting_mesh_vertices - subdivided_decimated_reference_mesh_vertices
        )
        np.savetxt(displacement_path, displacement_i, fmt='%8f')
    displacements.append(displacement_i)


for i, displacement in zip(range(firstIndex, lastIndex + 1), displacements):
    pcd = o3d.geometry.PointCloud()
    points = displacement
    pcd.points = o3d.utility.Vector3dVector(points)
    points = np.asarray(pcd.points)
    dtype = o3d.core.float32
    p_tensor = o3d.core.Tensor(points, dtype=dtype)
    pc = o3d.t.geometry.PointCloud(p_tensor)
    o3d.t.io.write_point_cloud(
        os.path.join(reference_data_dir, f'dis_{dataset}_{i:03}.ply'),
        pc, write_ascii=True
    )

input_reference_mesh_path = fr'../tvm-editing/TVMEditor.Test/bin/Release/net5.0/Data/{dataset}_{num_centers}/reference_mesh/decimated_reference_mesh.obj'
input_decimated_reference_mesh = o3d.io.read_triangle_mesh(input_reference_mesh_path, enable_post_processing=False)
output_path = fr'../tvm-editing/TVMEditor.Test/bin/Release/net5.0/Data/{dataset}_{num_centers}/reference_mesh/encoded_decimated_reference_mesh.drc'
result = subprocess.run([
    encoderPath,
    '-i', input_reference_mesh_path,
    '-o', output_path,
    '-qp', str('14'),
    '-cl', '7'
], capture_output=True, text=True)
print(result.stdout)
print(result.stderr)

result = subprocess.run([
    decoderPath,
    '-i', output_path,
    '-o',
    fr'../tvm-editing/TVMEditor.Test/bin/Release/net5.0/Data/{dataset}_{num_centers}/reference_mesh/decode_decimated_reference_mesh.obj'
], capture_output=True, text=True)
print(result.stdout)
print(result.stderr)
time_pattern = re.compile(r"(\d+) ms to encode")
match = time_pattern.search(result.stdout)
if match:
    print(f"reference mesh decoding: {int(match.group(1))} ms")
    decoding_time += int(match.group(1))


times = []
input_encoder_path = fr"../tvm-editing/TVMEditor.Test/bin/Release/net5.0/Data/{dataset}_{num_centers}/reference_mesh"
output_encoder_path = fr"../tvm-editing/TVMEditor.Test/bin/Release/net5.0/Data/{dataset}_{num_centers}/reference_mesh/GoF{num_frames}"
print(input_encoder_path)
if not os.path.exists(output_encoder_path):
    os.makedirs(output_encoder_path)

for i in range(firstIndex, lastIndex + 1):
    # print(os.path.join(input_encoder_path, f"dis_{dataset}_{i:03}.ply"))
    result = subprocess.run([
        encoderPath,
        '-point_cloud',
        '-i', os.path.join(input_encoder_path, f"dis_{dataset}_{i:03}.ply"),
        '-o', os.path.join(output_encoder_path, f"dis_{dataset}_{i:03}.drc"),
        '-qp', str(qp),
        '-cl', '10'
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

times = []
for i in range(firstIndex, lastIndex + 1):
    result = subprocess.run([
        decoderPath,
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



def calculate_bitrate(file_size, duration):
    return file_size * 8 / duration


number_frames = num_frames
frame_rate = 30
total_size = 0
total_duration = number_frames / frame_rate
for i in range(firstIndex, lastIndex + 1):
    displacement_file_path = fr'../tvm-editing/TVMEditor.Test/bin/Release/net5.0/Data/{dataset}_{num_centers}/reference_mesh/GoF{num_frames}/dis_{dataset}_{i:03}.drc'
    displacement_file_size = os.path.getsize(displacement_file_path)
    total_size += displacement_file_size
    displacements_bitrate = calculate_bitrate(total_size, total_duration) / 1000000
reference_mesh_file_path = fr'../tvm-editing/TVMEditor.Test/bin/Release/net5.0/Data/{dataset}_{num_centers}/reference_mesh/encoded_decimated_reference_mesh.drc'
reference_mesh_file_size = os.path.getsize(reference_mesh_file_path)
total_size += reference_mesh_file_size

overall_bitrate = calculate_bitrate(total_size, total_duration)

reference_bitrate = calculate_bitrate(reference_mesh_file_size, total_duration) / 1000000

print(f"Total Size of {number_frames} DRC Files: {total_size} bytes")
print(f"Overall Bitrate: {overall_bitrate} bits per second")

bitrate_kbps = overall_bitrate / 1000
bitrate_mbps = overall_bitrate / 1000000

print(f"Overall Bitrate: {bitrate_kbps:.2f} Kbps")
print(f"Reference Bitrate: {reference_bitrate:.2f} Mbps")
print(f"Displacements Bitrate: {displacements_bitrate:.2f} Mbps")
print(f"Overall Bitrate: {bitrate_mbps:.2f} Mbps")

original_displacements = displacements
decoded_displacements = []
for i in range(firstIndex, lastIndex + 1):
    decoded_displacement = o3d.io.read_point_cloud(
        fr'../tvm-editing/TVMEditor.Test/bin/Release/net5.0/Data/{dataset}_{num_centers}/reference_mesh/GoF{num_frames}/decoded_{dataset}_{i:03}_displacements.ply')
    decoded_displacements.append(decoded_displacement)

d1s = []
d2s = []
mses = []
rmses = []
hausdorffs = []
if not os.path.exists(outputPath):
    os.makedirs(outputPath)

subdivision_times = 0
deform_times = 0
decode_decimated_reference_mesh = o3d.io.read_triangle_mesh(
    os.path.join(reference_data_dir, 'decode_decimated_reference_mesh.obj'),
    enable_post_processing=False
)
start = time.perf_counter()
subdivided_decoded_mesh = o3d.geometry.TriangleMesh.subdivide_midpoint(
    decode_decimated_reference_mesh, number_of_iterations=1
)
subdivision_times = time.perf_counter() - start
subdivided_decoded_mesh_vertices = np.asarray(
    subdivided_decoded_mesh.vertices
).copy()
input_decimated_reference_mesh = o3d.io.read_triangle_mesh(
    os.path.join(reference_data_dir, 'decimated_reference_mesh.obj'),
    enable_post_processing=False
)
subdivided_mesh = o3d.geometry.TriangleMesh.subdivide_midpoint(
    input_decimated_reference_mesh, number_of_iterations=1
)
_, reference_indices = cKDTree(np.asarray(subdivided_mesh.vertices)).query(
    subdivided_decoded_mesh_vertices, k=1, workers=-1
)

for m in range(0, num_frames):
    offset = firstIndex
    original_mesh = o3d.io.read_triangle_mesh(fr'../tvm-editing/TVMEditor.Test/bin/Release/net5.0/Data/{dataset}_{num_centers}/meshes/{fileNamePrefix}{m + offset:03}.obj')
    displacement = np.asarray(decoded_displacements[m].points)

    start = time.perf_counter()
    _, displacement_indices = cKDTree(displacement).query(
        original_displacements[m][reference_indices], k=1, workers=-1
    )
    reordered_vertices = (
        subdivided_decoded_mesh_vertices + displacement[displacement_indices]
    )
    deform_times += time.perf_counter() - start
    reconstruct_mesh = o3d.geometry.TriangleMesh()
    reconstruct_mesh.triangles = subdivided_decoded_mesh.triangles
    reconstruct_mesh.vertices = o3d.utility.Vector3dVector(reordered_vertices)
    reconstruct_mesh.compute_vertex_normals()
    #o3d.visualization.draw_geometries([reconstruct_mesh])
    o3d.io.write_triangle_mesh(os.path.join(outputPath, f"decoded_{dataset}_fr0{m+offset:03}.obj"), reconstruct_mesh, write_vertex_normals=False, write_vertex_colors=False, write_triangle_uvs=False)
    print(f"Mesh 0{m+offset:03} saved! Objective Evaluation:\n")
    original_mesh.compute_vertex_normals()
    forward = directed_metrics(original_mesh, reconstruct_mesh)
    reverse = directed_metrics(reconstruct_mesh, original_mesh)

    d1 = max(forward[0], reverse[0])
    print("D1:", d1)
    d1s.append(d1)

    d2 = max(forward[1], reverse[1])
    print("D2:", d2)
    d2s.append(d2)

    logmse = min(forward[2], reverse[2])
    logrmse = min(forward[3], reverse[3])
    print("log10 of mse:", logmse, ", log10 of rmse:", logrmse)
    mses.append(logmse)
    rmses.append(logrmse)

decoding_time += subdivision_times*1000/num_frames
decoding_time += deform_times*1000/num_frames

print(f"decoding time: {decoding_time} ms")
#o3d.visualization.draw_geometries([reconstruct_mesh])
print("average D1:", np.mean(d1s))
print("average D2:", np.mean(d2s))
print("average log10 of mse:", np.mean(mses))
print("average log10 of rmse:", np.mean(rmses))
metrics = {
    "bitrate_mbps": float(bitrate_mbps),
    "d1_mean": float(np.mean(d1s)),
    "d2_mean": float(np.mean(d2s)),
    "log10_mse_mean": float(np.mean(mses)),
    "log10_rmse_mean": float(np.mean(rmses)),
    "decoding_time_ms": float(decoding_time),
    "num_frames": int(num_frames),
    "qp": int(qp),
}
with open(os.path.join(outputPath, "metrics.json"), "w", encoding="utf-8") as metrics_file:
    json.dump(metrics, metrics_file, indent=2)
print(json.dumps(metrics))
