import os

import matplotlib.pyplot as plt
import open3d as o3d
import numpy as np


path = "./figures"
if not os.path.exists(path):
    os.makedirs(path)
## Figure 4 (a) RD performance - Dancer
Bitrates = [3.31, 3.99, 4.88, 6.13, 7.73, 9.44, 11.23, 12.81, 13.10, 13.13, 13.23, 13.43, 13.89, 20.08]
D2_PSNR = [48.23, 54.34, 60.37, 66.54, 72.52, 78.56, 84.58, 90.57, 96.58, 102.63, 108.64, 114.65, 120.71, 126.24]


ours_Bitrates = [1.76, 3.03, 4.7, 6.12, 8.29, 10.10, 11.90, 13.69, 15.49, 17.29]
ours_D2_PSNR = [68.52, 74.57, 80.51, 86.27, 91.42, 95.18, 97.15, 97.97, 98.19, 98.25]

KDDI_Bitrates = [2.18, 3.90, 9.12, 13.97, 21.25]
KDDI_D2_PSNR = [70.79, 71.15, 77.26, 80.01, 80.75]

VDMC_Bitrates = [3.26, 5.50, 10.83, 15.32, 23.26]
VDMC_D2_PSNR = [71.49, 71.91, 78.12, 80.55, 81.38]


plt.figure(figsize=(12, 12), num= "RD performance - Dancer")
plt.plot(ours_Bitrates, ours_D2_PSNR, marker='s', label='TVMC, GoF = 10', markersize=14, color = 'C1')
plt.plot(Bitrates, D2_PSNR, marker='o', label='Draco', markersize=14, color = 'C2')
plt.plot(KDDI_Bitrates, KDDI_D2_PSNR, marker='^', label='KDDI', markersize=14, color = 'C0')
plt.plot(VDMC_Bitrates, VDMC_D2_PSNR, marker='D', label='V-DMC 4.0', markersize=14, color = 'C3')
plt.xlabel('Bitrate (Mbps)', fontsize=40)
plt.ylabel('D2-PSNR (dB)', fontsize=40)
plt.grid(True)
plt.legend(fontsize=34, loc = 'lower right')
plt.xlim(0, 25)
plt.ylim(45, 130)
plt.xticks(fontsize=34)
plt.yticks(fontsize=34)
plt.tight_layout()
plt.savefig("./figures/comparison_methods_Dancer.png", dpi=300, bbox_inches='tight')
plt.show()


## Figure 4 (b) RD performance - Basketball player
Bitrates = [4.02, 4.93, 6.19, 7.78, 9.50, 11.29, 12.89, 13.32, 13.38, 13.41, 13.63, 14.08, 23.91]
D2_PSNR = [53.84, 59.83, 66.01, 71.99, 78.01, 84.03, 90.04, 96.06, 102.06, 109.43, 114.16, 120.17, 126.14]


ours_Bitrates = [2.8, 4.41, 6.21, 8.01, 9.81, 11.61, 13.41, 15.21, 17.01, 18.8]
ours_D2_PSNR = [72.23, 78.26, 84.13, 89.62, 93.97, 96.42, 97.34, 97.59, 97.66, 97.68]

KDDI_Bitrates = [1.67, 3.05, 7.63, 12.21, 19.76]
KDDI_D2_PSNR = [70.45, 71.61, 77.17, 80.04, 80.40]

VDMC_Bitrates = [3.28, 5.42, 10.74, 15.52, 23.21]
VDMC_D2_PSNR = [71.30, 72.60, 78.11, 80.92, 81.36]


plt.figure(figsize=(12, 12), num= "RD performance - Basketball player")
plt.plot(ours_Bitrates, ours_D2_PSNR, marker='s', label='TVMC, GoF = 10', markersize=14, color = 'C1')
plt.plot(Bitrates, D2_PSNR, marker='o', label='Draco', markersize=14, color = 'C2')
plt.plot(KDDI_Bitrates, KDDI_D2_PSNR, marker='^', label='KDDI', markersize=14, color = 'C0')
plt.plot(VDMC_Bitrates, VDMC_D2_PSNR, marker='D', label='V-DMC 4.0', markersize=14, color = 'C3')
plt.xlabel('Bitrate (Mbps)', fontsize=40)
plt.ylabel('D2-PSNR (dB)', fontsize=40)
plt.grid(True)
plt.legend(fontsize=34, loc = 'lower right')
plt.xlim(0, 25)
plt.ylim(45, 130)
plt.xticks(fontsize=34)
plt.yticks(fontsize=34)
plt.tight_layout()
plt.savefig("./figures/comparison_methods_Basketball.png", dpi=300, bbox_inches='tight')
plt.show()


## Figure 4 (c) RD performance - Mitch
Bitrates = [3.32, 4.11, 5.20, 6.47, 7.81, 9.19, 10.50, 10.95, 11.01, 11.45, 12.20, 18.77, 20.11]
D2_PSNR = [54.85, 60.94, 66.99, 73.02, 79.03, 85.05, 91.08, 97.14, 103.11, 109.13, 115.14, 121.18, 126.84]


ours_Bitrates = [3.19, 4.97, 6.77, 8.57, 10.37, 12.17, 13.98, 15.78, 17.58, 19.38]
ours_D2_PSNR = [81.67, 87.46, 92.63, 96.34, 98.15, 98.76, 98.93, 98.97, 98.98, 98.98]

KDDI_Bitrates = [2.61, 3.28, 5.05, 7.72, 12.77]
KDDI_D2_PSNR = [73.50, 74.62, 77.41, 81.04, 83.22]

VDMC_Bitrates = [3.43, 4.37, 6.54, 8.68, 12.88]
VDMC_D2_PSNR = [73.57, 74.79, 77.66, 81.10, 83.29]


plt.figure(figsize=(12, 12), num= "RD performance - Mitch")
plt.plot(ours_Bitrates, ours_D2_PSNR, marker='s', label='TVMC, GoF = 10', markersize=14, color = 'C1')
plt.plot(Bitrates, D2_PSNR, marker='o', label='Draco', markersize=14, color = 'C2')
plt.plot(KDDI_Bitrates, KDDI_D2_PSNR, marker='^', label='KDDI', markersize=14, color = 'C0')
plt.plot(VDMC_Bitrates, VDMC_D2_PSNR, marker='D', label='V-DMC 4.0', markersize=14, color = 'C3')
plt.xlabel('Bitrate (Mbps)', fontsize=40)
plt.ylabel('D2-PSNR (dB)', fontsize=40)
plt.grid(True)
plt.legend(fontsize=34, loc = 'lower right')
plt.xlim(0, 25)
plt.ylim(45, 130)
plt.xticks(fontsize=34)
plt.yticks(fontsize=34)
plt.tight_layout()
plt.savefig("./figures/comparison_methods_Mitch.png", dpi=300, bbox_inches='tight')
plt.show()


## Figure 4 (d) RD performance - Thomas
Bitrates = [3.34, 4.13, 5.20, 6.46, 7.81, 9.18, 10.32, 10.90, 11.01, 11.09, 11.30, 11.84, 18.83]
D2_PSNR = [53.53, 59.59, 65.65, 71.72, 77.79, 83.95, 89.76, 95.79, 101.78, 107.81, 113.81, 119.83, 125.50]


ours_Bitrates = [1.94, 3.34, 5.09, 6.89, 8.69, 10.49, 12.29, 14.09, 15.89, 17.69]
ours_D2_PSNR = [69.52, 75.53, 81.50, 87.18, 92.14, 95.44, 96.91, 97.37, 97.49, 97.52]

KDDI_Bitrates = [1.64, 2.82, 4.97, 7.06, 11.98]
KDDI_D2_PSNR = [74.04, 77.16, 79.96, 81.36, 83.52]

VDMC_Bitrates = [3.25, 4.33, 6.58, 8.82, 13.79]
VDMC_D2_PSNR = [74.30, 77.45, 80.21, 81.62, 83.74]


fig, ax = plt.subplots(figsize=(12, 12), num= "RD performance - Thomas")
plt.plot(ours_Bitrates, ours_D2_PSNR, marker='s', label='TVMC, GoF = 10', markersize=14, color = 'C1')
plt.plot(Bitrates, D2_PSNR, marker='o', label='Draco', markersize=14, color = 'C2')
plt.plot(KDDI_Bitrates, KDDI_D2_PSNR, marker='^', label='KDDI', markersize=14, color = 'C0')
plt.plot(VDMC_Bitrates, VDMC_D2_PSNR, marker='D', label='V-DMC 4.0', markersize=14, color = 'C3')
plt.xlabel('Bitrate (Mbps)', fontsize=40)
plt.ylabel('D2-PSNR (dB)', fontsize=40)
plt.grid(True)
plt.legend(fontsize=34, loc = 'lower right')
plt.xlim(0, 25)
plt.ylim(45, 130)
plt.xticks(fontsize=34)
plt.yticks(fontsize=34)
plt.tight_layout()
plt.savefig(r"./figures/comparison_methods_Thomas.png", dpi=300, bbox_inches='tight')
plt.show()



## Figure 8: RD performance of "Dancer" under different GoFs of 5, 10, and 15
ours_Bitrates_5 = [1.79, 2.78, 4.34, 6.12, 7.92, 9.72, 11.51, 13.31, 15.11, 16.91]
ours_D2_PSNR_5 = [66.15, 72.18, 78.18, 84.10, 89.61, 94.17, 96.78, 97.80, 98.11, 98.19]

ours_Bitrates_10 = [1.76, 3.03, 4.7, 6.49, 8.29, 10.10, 11.90, 13.69, 15.49, 17.29]
ours_D2_PSNR_10 = [68.52, 74.57, 80.51, 86.27, 91.42, 95.18, 97.15, 97.97, 98.19, 98.25]

ours_Bitrates_15 = [1.77, 3.17, 4.89, 6.69, 8.49, 10.28, 12.08, 13.88, 15.68, 17.48]
ours_D2_PSNR_15 = [68.68, 74.73, 80.70, 86.44, 91.63, 95.37, 97.28, 97.98, 98.17, 98.22]



plt.figure(figsize=(16, 8), num="RD performance of 'Dancer' under different GoFs of 5, 10, and 15")
plt.plot(ours_Bitrates_5, ours_D2_PSNR_5, marker='^', label='GoF = 5', markersize=12)
plt.plot(ours_Bitrates_10, ours_D2_PSNR_10, marker='o', label='GoF = 10', markersize=12)
plt.plot(ours_Bitrates_15, ours_D2_PSNR_15, marker='s', label='GoF = 15', markersize=12)
plt.xlabel('Bitrate (Mbps)', fontsize=40)
plt.ylabel('D2-PSNR (dB)', fontsize=40)
plt.grid(True)
plt.legend(fontsize=34, loc = 'lower right')
plt.xlim(0, 20)
plt.xticks(fontsize=34)
plt.yticks(fontsize=34)
plt.tight_layout()
plt.savefig("./figures/comparison_gof_Dancer.png", dpi=600, bbox_inches='tight')
plt.show()



## Figure 7: Cumulative distribution function (CDF) of deformation distance for "Basketball player" and "Thomas".
loaded_decimated_reference_mesh = o3d.io.read_triangle_mesh('../tvm-editing/TVMEditor.Test/bin/Release/net5.0/Data/basketball_player_1995/reference_mesh/decimated_reference_mesh.obj', enable_post_processing=False)
#print(loaded_decimated_reference_mesh)
subdivided_decimated_reference_mesh = o3d.geometry.TriangleMesh.subdivide_midpoint(loaded_decimated_reference_mesh, number_of_iterations=1)
#print(subdivided_decimated_reference_mesh)
loaded_decimated_reference_mesh.compute_vertex_normals()
subdivided_decimated_reference_mesh.compute_vertex_normals()


vertices = np.array(subdivided_decimated_reference_mesh.vertices)
x_threshold = 0
y_threshold = 0
z_threshold = -1

selected_ids = [idx for idx in range(vertices.shape[0]) if
                #vertices[idx, 0] > x_threshold or
                vertices[idx, 1] > y_threshold and
                vertices[idx, 2] < z_threshold]

not_selected_ids = [idx for idx in range(vertices.shape[0]) if
                #vertices[idx, 1] > y_threshold and
                vertices[idx, 2] > -0.8]

selected_vertices = vertices[not_selected_ids]
selected_points_cloud = o3d.geometry.PointCloud()
selected_points_cloud.points = o3d.utility.Vector3dVector(selected_vertices)


#o3d.visualization.draw_geometries([subdivided_decimated_reference_mesh, selected_points_cloud])

displacement = np.loadtxt('../tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/basketball_player_1995/reference/displacements_basketball_player_018.txt')

dis_select = []
for i in range (selected_ids.__len__()):
    #print(np.linalg.norm(displacement[selected_ids[i]]))
    dis_select.append(np.linalg.norm(displacement[selected_ids[i]]))
dis_not_select = []
for i in range (not_selected_ids.__len__()):
    #print(np.linalg.norm(displacement[not_selected_ids[i]]))
    dis_not_select.append(np.linalg.norm(displacement[not_selected_ids[i]]))
cumulative_select = np.linspace(0, 1, len(dis_select))

sorted_data_select = np.sort(dis_select)
sorted_data_not_select = np.sort(dis_not_select)

cumulative_data_select = np.cumsum(sorted_data_select) / np.sum(sorted_data_select)
cumulative_data_not_select = np.cumsum(sorted_data_not_select) / np.sum(sorted_data_not_select)

plt.figure(figsize=(12, 12), num="Cumulative distribution function (CDF) of deformation distance for 'Basketball player'")
plt.plot(sorted_data_select, cumulative_data_select, label="Moving parts", linewidth=4, color='#1f77b4')
plt.plot(sorted_data_not_select, cumulative_data_not_select, label="Static parts", linewidth=4, color='#ff7f0e')
plt.legend(fontsize=34, loc = 'lower right', frameon=True)
plt.xlabel("Deformation distance", fontsize=40)
plt.ylabel("CDF", fontsize=40)
plt.xlim(left=0, right=1.8)
plt.ylim(top=1.05, bottom=-0.05)
x_min, x_max = plt.xlim()
plt.xticks(np.arange(0, np.ceil(x_max) , 0.2), fontsize=34)
plt.yticks(fontsize=34)
plt.grid(True, which='both', linestyle='--', linewidth=0.7)
#plt.title("Cumulative Distribution Function (CDF)")
plt.savefig("./figures/cumulative_distribution_function_basketball.png", dpi=300, bbox_inches='tight')
plt.show()
