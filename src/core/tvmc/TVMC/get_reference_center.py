import argparse
import os
import re

import numpy as np
import open3d as o3d
from sklearn.manifold import MDS

# Command-line argument parser
parser = argparse.ArgumentParser(description="Get the set of reference centers.")
parser.add_argument('--dataset', type=str, required=True, help="Dataset name (e.g., 'basketball_player')")
parser.add_argument('--num_frames', type=int, required=True, help="Number of frames to process")
parser.add_argument('--num_centers', type=int, required=True, help="Number of volume centers (pointCount)")
parser.add_argument('--centers_dir', type=str, required=True, help="Path for the volume centers")
parser.add_argument('--file_extension', type=str, default=".xyz", help="File extension for the input files")
parser.add_argument('--random_state', type=int, default=None, help="Seed for MDS (for reproducibility)")

args = parser.parse_args()

# Extract arguments
dataset = args.dataset
num_frames = args.num_frames
num_centers = args.num_centers
centers_dir = args.centers_dir
file_extension = args.file_extension
random_state = args.random_state

print("open3d version:", o3d.__version__)
print(f"Dataset: {dataset}, Frames: {num_frames}, Centers: {num_centers}")

output_file = f"{centers_dir}/{dataset}_distance_matrix_{num_frames}_{num_centers}.txt"

xyz_files = [f for f in os.listdir(centers_dir) if f.endswith('.xyz')]
re_pattern = re.compile('.+?(\d+)\.([a-zA-Z0-9+])')
xyz_files = sorted(xyz_files, key=lambda x: int(re_pattern.match(x).groups()[0]))

if not os.path.exists(output_file):
    max_distance_matrix = np.zeros((num_centers, num_centers))

    for xyz_file in xyz_files[0:num_frames]:
        print("Loading and processing: ", xyz_file)
        all_points = []
        filename = os.path.join(centers_dir, xyz_file)
        with open(filename, 'r') as file:
            for line in file:
                points = list(map(float, line.split()))
                all_points.append(points)
            all_points = np.array(all_points)
            #print(all_points[0])
            for i in range(num_centers):
                for j in range(i + 1, num_centers):
                    if np.max(np.linalg.norm(all_points[i] - all_points[j], axis=0)) > max_distance_matrix[i, j]:
                        max_distance_matrix[i, j] = np.max(np.linalg.norm(all_points[i] - all_points[j], axis=0))

                    max_distance_matrix[j, i] = max_distance_matrix[i, j]


    np.savetxt(output_file, max_distance_matrix)

    print("Distance Matrix generated and saved!")
    print("Find Distance Matrix here: ", output_file)
#print(max_distance_matrix, max_distance_matrix.shape)
else:
    print("Distance Matrix already generated! Feed it into MDS...")

max_distance_matrix = np.loadtxt(output_file)
if random_state is None:
    random_state = np.random.randint(0, 100000)
print(f"Feed Distance Matrix to multi-dimensional scaling to get reference centers, random_state = {random_state}")
mds = MDS(n_components=3, metric=True, dissimilarity='precomputed', n_jobs=-1, eps=1e-10, verbose=0, random_state=random_state,
          n_init=6, max_iter=300)
reference_centers = mds.fit_transform(max_distance_matrix)

center_datas = []
for xyz_file in xyz_files:
    center_filename = os.path.join(centers_dir, xyz_file)
    center_data = np.loadtxt(center_filename)
    center_datas.append(center_data)

centers = center_datas[0]

print("Singular Value Decomposition...")
centers_mean = np.mean(centers, axis=0)
reference_centers_mean = np.mean(reference_centers, axis=0)
centers_centered = centers - centers_mean
reference_centers_centered = reference_centers - reference_centers_mean

cov_matrix = np.dot(centers_centered.T, reference_centers_centered)
U, _, Vt = np.linalg.svd(cov_matrix)
R = np.dot(U, Vt)

reference_centers_aligned = np.dot(reference_centers_centered, R.T)
reference_centers_aligned = reference_centers_aligned + centers_mean

output_path = f"{centers_dir}/reference"
if not os.path.exists(output_path):
    os.makedirs(output_path)
output_filename = f"{centers_dir}/reference/reference_centers_aligned.xyz"
np.savetxt(output_filename, reference_centers_aligned, fmt='%f', delimiter=' ')

print("Reference centers saved!")
print("Find reference centers here: ", output_filename)
