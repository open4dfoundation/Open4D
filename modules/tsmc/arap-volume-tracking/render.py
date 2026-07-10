import open3d as o3d
import numpy as np

def draw_centers_with_mesh(centers, radius, color, mesh):
    spheres = []
    sphere_radius = radius

    for point in centers:
        mesh_sphere = o3d.geometry.TriangleMesh.create_sphere(radius=sphere_radius)
        mesh_sphere.compute_vertex_normals()

        #mesh_sphere.translate(point + [0,1,-0.2])
        mesh_sphere.translate(point)
        spheres.append(mesh_sphere)

    all_spheres = o3d.geometry.TriangleMesh()
    for sphere in spheres:
        all_spheres += sphere

    all_spheres.paint_uniform_color(color)


    mesh.compute_vertex_normals()
    o3d.visualization.draw_geometries([all_spheres, mesh])

transformed_centers_path = f'./output/answering-2000/mesh_0res_2000_009.xyz'
mesh = o3d.io.read_triangle_mesh("./data/answering/mesh_0009.obj")
loaded_transformed_centers = np.loadtxt(transformed_centers_path)

draw_centers_with_mesh(loaded_transformed_centers, radius=0.02, color=[0.4196078431372549, 0.6823529411764706, 0.8392156862745098], mesh=mesh)