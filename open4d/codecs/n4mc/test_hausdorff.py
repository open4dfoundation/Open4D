import torch
import trimesh
from pytorch3d.io import load_objs_as_meshes
from pytorch3d.loss import chamfer_distance
from metrics import compute_D2_psnr, compute_D1_psnr, chamfer_distance
import open3d as o3d

def compute_mesh_losses(pm, gm, device, w_chamfer=1.0, w_normal=1.0, w_volume=1.0):
    """
    Compute Chamfer, Normal consistency, and Volume losses between predicted and GT meshes.
    """
    # Get vertices
    p_verts = pm.verts_list()[0].unsqueeze(0).to(device)  # (1, P, 3)
    g_verts = gm.verts_list()[0].unsqueeze(0).to(device)  # (1, G, 3)

    # --- Chamfer loss ---
    chamfer_loss, _ = chamfer_distance(
        g_verts, p_verts,
        batch_reduction="mean",
        point_reduction="sum",
        single_directional=False
    )

    # --- Normal consistency loss ---
    p_normals = pm.verts_normals_list()[0].unsqueeze(0).to(device)
    g_normals = gm.verts_normals_list()[0].unsqueeze(0).to(device)

    dist_matrix = torch.cdist(p_verts[0], g_verts[0])  # (P, G)
    idx = dist_matrix.argmin(dim=1)  # (P,)
    corr_g_normals = g_normals[0][idx]

    cosine_sim = (p_normals[0] * corr_g_normals).sum(dim=1).clamp(min=-1, max=1)
    normal_loss = 1 - cosine_sim.abs().mean()

    # --- Volume loss ---
    pm_tri = trimesh.Trimesh(vertices=p_verts[0].cpu().numpy(),
                             faces=pm.faces_list()[0].cpu().numpy())
    gm_tri = trimesh.Trimesh(vertices=g_verts[0].cpu().numpy(),
                             faces=gm.faces_list()[0].cpu().numpy())
    volume_loss = torch.abs(torch.tensor(pm_tri.volume) - torch.tensor(gm_tri.volume)).to(device)

    # --- Total weighted loss ---
    total_loss = w_chamfer * chamfer_loss + w_normal * normal_loss + w_volume * volume_loss

    return {
        "chamfer_loss": chamfer_loss,
        "normal_loss": normal_loss,
        "volume_loss": volume_loss,
        "total_loss": total_loss
    }


if __name__ == "__main__":
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")

    mesh1 = "/media/frozzzen/DataDrive/VS2022Projects/arap-volume-tracking-main/data/Dancer/dancer_fr0005.obj"
    mesh2 = "/media/frozzzen/DataDrive/PycharmProjects/Mesh_Editing/Results/decode_Draco/Dancer/Dancer_qp_9/dancer_fr0005_qp_9_decoded.obj"


    mesh1 = "/media/frozzzen/DataDrive/ChromeDownloads/Dancer_dataset/C4/TSDF_256/meshes/0004/mesh0004.obj"
    mesh2 = "/media/frozzzen/DataDrive/ChromeDownloads/Dancer_dataset/C4/scaled/geometry-only/dancer_fr0005.obj"



    mesh1 = o3d.io.read_triangle_mesh(mesh1)
    mesh2 = o3d.io.read_triangle_mesh(mesh2)
    mesh1.compute_vertex_normals()
    mesh2.compute_vertex_normals()
    o3d.visualization.draw_geometries([mesh1, mesh2])
    psnr = max(compute_D2_psnr(mesh1, mesh2), compute_D2_psnr(mesh1, mesh2))
    d1psnr = max(compute_D1_psnr(mesh1, mesh2), compute_D1_psnr(mesh1, mesh2))
    print(psnr)
    print(d1psnr)

    cd = chamfer_distance(mesh1, mesh2)
    print("Chamfer Distance:", cd)


