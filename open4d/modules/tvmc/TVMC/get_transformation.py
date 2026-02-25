import os
import re

from scipy.spatial.transform import Rotation as R
import numpy as np
import argparse
import shutil

def get_dual_quaternions(original_centers, transformed_centers):
    moved_indices = []
    for i in range(len(original_centers)):
        if not (np.array_equal(original_centers[i], transformed_centers[i])):
            moved_indices.append(i)
    dual_quaternions = np.zeros((len(original_centers), 8), dtype=np.float32)
    inverse_dual_quaternions = np.zeros((len(original_centers), 8), dtype=np.float32)
    for i in moved_indices:
        original = original_centers[i]
        transformed = transformed_centers[i]

        rotation_quaternion = R.from_quat([0, 0, 0, 1])

        translation = transformed - original

        rotation_quat = rotation_quaternion.as_quat()

        translation_quat = np.hstack((translation, [0]))
        dual_quat = np.hstack((rotation_quat, 0.5 * translation_quat))
        dual_quaternions[i] = dual_quat

        rotation_conjugate = np.hstack((-rotation_quat[:3], rotation_quat[3]))
        inv_translation_quat = np.hstack((translation, [0]))
        inverse_dual_quat = np.hstack((rotation_conjugate, -0.5 * inv_translation_quat))
        inverse_dual_quaternions[i] = inverse_dual_quat

    return moved_indices, dual_quaternions, inverse_dual_quaternions


parser = argparse.ArgumentParser(description="Get transformation matrix.")
parser.add_argument('--dataset', type=str, required=True, help="Dataset name (e.g., 'basketball_player')")
parser.add_argument('--num_frames', type=int, required=True, help="Number of frames to process")
parser.add_argument('--num_centers', type=int, required=True, help="Number of volume centers (pointCount)")
parser.add_argument('--centers_dir', type=str, required=True, help="Path for the volume centers")
parser.add_argument('--firstIndex', type=int, required=True, help="first index")
parser.add_argument('--lastIndex', type=int, required=True, help="last index")

args = parser.parse_args()

dataset = args.dataset
num_frames = args.num_frames
num_centers = args.num_centers
centers_dir = args.centers_dir
firstIndex = args.firstIndex
lastIndex = args.lastIndex

mesh_path = f"../arap-volume-tracking/data/{dataset}"

data_base_path = f"../tvm-editing/TVMEditor.Test/bin/Release/net5.0/Data/{dataset}_{num_centers}"
data_subdirectories = ["centers", "meshes", "reference_center", "reference_mesh"]

output_base_path = f"../tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/{dataset}_{num_centers}"
output_subdirectories = ["output", "reference"]

if not os.path.exists(data_base_path):
    os.makedirs(data_base_path)

for subdir in data_subdirectories:
    path = os.path.join(data_base_path, subdir)
    if not os.path.exists(path):
        os.makedirs(path)

if not os.path.exists(output_base_path):
    os.makedirs(output_base_path)

for subdir in output_subdirectories:
    path = os.path.join(output_base_path, subdir)
    if not os.path.exists(path):
        os.makedirs(path)

re_pattern = re.compile('.+?(\d+)\.([a-zA-Z0-9+])')

obj_files = [f for f in os.listdir(mesh_path) if f.endswith('.obj')]
obj_files = sorted(obj_files, key=lambda x: int(re_pattern.match(x).groups()[0]))

xyz_files = [f for f in os.listdir(centers_dir) if f.endswith('.xyz')]
xyz_files = sorted(xyz_files, key=lambda x: int(re_pattern.match(x).groups()[0]))

for obj_file in obj_files[:num_frames]:
    file_path = os.path.join(mesh_path, obj_file)
    target_path = os.path.join(data_base_path, "meshes", obj_file)
    shutil.copy(file_path, target_path)

for xyz_file in xyz_files[:num_frames]:
    file_path = os.path.join(centers_dir, xyz_file)
    target_path = os.path.join(data_base_path, "centers", xyz_file)
    shutil.copy(file_path, target_path)

reference_centers_path = os.path.join(centers_dir, "reference", "reference_centers_aligned.xyz")
shutil.copy(reference_centers_path, os.path.join(data_base_path, "reference_center"))

reference_centers_path = os.path.join(data_base_path, "reference_center/reference_centers_aligned.xyz")
loaded_reference_centers = np.loadtxt(reference_centers_path)

i = firstIndex
for xyz_file in xyz_files[:num_frames]:
    centers_path = os.path.join(data_base_path, "centers",xyz_file)
    loaded_centers = np.loadtxt(centers_path)
    print(centers_path)

    indices, dual_quaternions, inverse_dual_quaternions = get_dual_quaternions(loaded_centers, loaded_reference_centers)
    os.path.join(data_base_path, xyz_file)
    indices_path = os.path.join(data_base_path, f"indices_{i:03}.txt")
    np.savetxt(indices_path, indices, fmt='%d')

    dual_quaternions_path = os.path.join(data_base_path, f"transformations_{i:03}.txt")
    with open(dual_quaternions_path, 'w') as file:
        for dq in dual_quaternions:
            dq_str = f"{dq[0]};{dq[1]};{dq[2]};{dq[3]};{dq[4]};{dq[5]};{dq[6]};{dq[7]}"
            file.write(dq_str + '\n')

    inverse_dual_quaternions_path = os.path.join(data_base_path, f"inverse_transformations_{i:03}.txt")
    with open(inverse_dual_quaternions_path, 'w') as file:
        for inverse_dq in inverse_dual_quaternions:
            inverse_dq_str = f"{inverse_dq[0]};{inverse_dq[1]};{inverse_dq[2]};{inverse_dq[3]};{inverse_dq[4]};{inverse_dq[5]};{inverse_dq[6]};{inverse_dq[7]}"
            file.write(inverse_dq_str + '\n')

    i += 1