import torch
from tables import *

class FMC:
    def __init__(self,device='cuda'):
        self.device=device
        self.mc_table=torch.tensor(mc_table,dtype=torch.long,device=device,requires_grad=False)
        self.cube_corners = torch.tensor([[0, 0, 0], [1, 0, 0], [0, 1, 0], [1, 1, 0], [0, 0, 1], [
                                         1, 0, 1], [0, 1, 1], [1, 1, 1]], dtype=torch.float, device=device)
        self.cube_corners_idx = torch.pow(2, torch.arange(8, requires_grad=False))
        self.cube_edges = torch.tensor([0, 1, 1, 5, 4, 5, 0, 4, 2, 3, 3, 7, 6, 7, 2, 6,
                                       2, 0, 3, 1, 7, 5, 6, 4], dtype=torch.long, device=device, requires_grad=False)

    def construct_voxel_grid(self, res):
        base_cube_f=torch.arange(8).to(self.device)
        if isinstance(res,int):
            res=(res,res,res)
        voxel_grid_template=torch.ones(res,device=self.device)  #(res, res, res)

        res=torch.tensor([res],dtype=torch.float,device=self.device)    #(1,3)
        coords=torch.nonzero(voxel_grid_template).float()/res
        verts=(self.cube_corners.unsqueeze(0)/res+coords.unsqueeze(1)).reshape(-1,3)
        cubes = (base_cube_f.unsqueeze(0) +
                 torch.arange(coords.shape[0], device=self.device).unsqueeze(1) * 8).reshape(-1)

        verts_rounded = torch.round(verts * 10**5) / (10**5)
        verts_unique, inverse_indices = torch.unique(verts_rounded, dim=0, return_inverse=True)
        cubes = inverse_indices[cubes.reshape(-1)].reshape(-1, 8)

        return verts_unique - 0.5, cubes
    
    def _identify_surf_cubes(self,s_n,cube_fx8):
        occ_n=s_n < 0
        occ_fx8 = occ_n[cube_fx8.reshape(-1)].reshape(-1,8)
        _occ_sum = torch.sum(occ_fx8, -1)                       #(N**3,)
        surf_cubes = (_occ_sum > 0) & (_occ_sum < 8)            #(N**3,)
        return surf_cubes, occ_fx8      #(N**3,)   (N**3, 8)
    
    @torch.no_grad()
    def _get_case_id(self,occ_fx8,surf_cubes,res):
        case_ids = (occ_fx8[surf_cubes] * self.cube_corners_idx.to(self.device).unsqueeze(0)).sum(-1)   #(N',)
        return case_ids

    @torch.no_grad()
    def _indentify_surf_edges(self,s_n,cube_fx8,surf_cubes):
        occ_n=s_n<0
        all_edges=cube_fx8[surf_cubes][:,self.cube_edges].reshape(-1,2)
        unique_edges, _idx_map, counts = torch.unique(all_edges, dim=0, return_inverse=True, return_counts=True)

        unique_edges = unique_edges.long()
        mask_edges = occ_n[unique_edges.reshape(-1)].reshape(-1, 2).sum(-1) == 1    #whether the edge has a vertex , (n_unique_edges,)

        surf_edges_mask = mask_edges[_idx_map]          #whether the edge has a vertex, (all_edge,)
        counts = counts[_idx_map]                       #how many cubes the edge shared, (all_edge,)

        #b=mask_edges.sum()
        #a=torch.arange(mask_edges.sum(), device=cube_fx8.device)

        mapping = torch.ones((unique_edges.shape[0]), dtype=torch.long, device=cube_fx8.device) * -1
        mapping[mask_edges] = torch.arange(mask_edges.sum(), device=cube_fx8.device)

        #c=mapping[mask_edges]

        # Shaped as [number of cubes x 12 edges per cube]. This is later used to map a cube edge to the unique index
        # for a surface-intersecting edge. Non-surface-intersecting edges are marked with -1.
        idx_map = mapping[_idx_map]                    #(all_edge,) -1 means the edge has no vertex, >=0 means i-th appear in the all edges
        surf_edges = unique_edges[mask_edges]           #index of the edge with a vertex, (unique_edge,)
        return surf_edges, idx_map, counts, surf_edges_mask

    def _linear_interp(self,edges_weight, edges_x):
        edge_dim = edges_weight.dim() - 2
        assert edges_weight.shape[edge_dim] == 2
        edges_weight = torch.cat([torch.index_select(input=edges_weight, index=torch.tensor(1, device=self.device), dim=edge_dim), -
                                 torch.index_select(input=edges_weight, index=torch.tensor(0, device=self.device), dim=edge_dim)], edge_dim)
        denominator = edges_weight.sum(edge_dim)
        ue = (edges_x * edges_weight).sum(edge_dim) / denominator
        return ue

    def _compute_vd(self,x_nx3,surf_cubes_fx8,surf_edges,s_n,case_ids,idx_map):
        surf_edges_x = torch.index_select(input=x_nx3, index=surf_edges.reshape(-1), dim=0).reshape(-1, 2, 3)
        surf_edges_s = torch.index_select(input=s_n, index=surf_edges.reshape(-1), dim=0).reshape(-1, 2, 1)
        zero_crossing = self._linear_interp(surf_edges_s, surf_edges_x)     #(n_edges,3)
        
        idx_map = idx_map.reshape(-1, 12)

    def __call__(self, x_nx3, s_n, cube_fx8, res):
        surf_cubes, occ_fx8 = self._identify_surf_cubes(s_n, cube_fx8)
        if surf_cubes.sum() == 0:
            return  torch.zeros((0, 3), dtype=torch.long, device=self.device), \
                    torch.zeros((0), device=self.device)
        
        case_ids=self._get_case_id(occ_fx8,surf_cubes,res)

#base_cube_edges = torch.tensor([0, 1, 1, 5, 4, 5, 0, 4, 2, 3, 3, 7, 6, 7, 2, 6, 2, 0, 3, 1, 7, 5, 6, 4], dtype=torch.long, requires_grad=False)
#base_cube_edges = torch.tensor([0, 1, 1, 3, 3, 2, 2, 0, 4, 5, 5, 7, 7, 6, 6, 4, 0, 4, 1, 5, 3, 7, 2, 6], dtype=torch.long, requires_grad=False)
base_cube_edges = torch.tensor([0,1,1,2,3,2,0,3,4,5,5,6,7,6,4,7,0,4,1,5,2,6,3,7], dtype=torch.long, requires_grad=False)

triangle_table=torch.tensor(mc_table,dtype=torch.long)

def _sort_edges(edges):
    """sort last dimension of edges of shape (E, 2)"""
    with torch.no_grad():
        order = (edges[:, 0] > edges[:, 1]).long()
        order = order.unsqueeze(dim=1)

        a = torch.gather(input=edges, index=order, dim=1)
        b = torch.gather(input=edges, index=1 - order, dim=1)

    return torch.stack([a, b], -1)

v_id = torch.pow(2, torch.arange(8, dtype=torch.long))

def dynamic_marching_cubes(vertices, cubes, sdf):
    # vertices: N, 3
    # cubes:    M, 8
    # sdf:      N,
    #print("DMC: ", sdf, sdf.shape)
    device=vertices.device
    with torch.no_grad():
        occ_n=sdf>0
        occ_fx8=occ_n[cubes.reshape(-1)].reshape(-1,8)
        occ_sum=torch.sum(occ_fx8,-1)                   # (M)
        valid_cubes= (occ_sum>0) & (occ_sum<8)          # select cubes with occ=1,2,3,4,5,6,7
        occ_sum=occ_sum[valid_cubes]

        all_edges=cubes[valid_cubes][:,base_cube_edges.to(device)].reshape(-1, 2)    #   (n_valid_cubes*12,2)
        all_edges = _sort_edges(all_edges)
        unique_edges, idx_map = torch.unique(all_edges, dim=0, return_inverse=True) #   (n_valid_cubes*12,2)

        unique_edges = unique_edges.long()
        mask_edges = occ_n[unique_edges.reshape(-1)].reshape(-1, 2).sum(-1) == 1
        mapping = torch.ones((unique_edges.shape[0]), dtype=torch.long, device=device) * -1
        mapping[mask_edges] = torch.arange(mask_edges.sum(), dtype=torch.long, device=device)
        idx_map = mapping[idx_map]

        interp_v = unique_edges[mask_edges]

    edges_to_interp = vertices[interp_v.reshape(-1)].reshape(-1, 2, 3)
    edges_to_interp_sdf = sdf[interp_v.reshape(-1)].reshape(-1, 2, 1)
    edges_to_interp_sdf[:, -1] *= -1

    denominator = edges_to_interp_sdf.sum(1, keepdim=True)
    edges_to_interp_sdf = torch.flip(edges_to_interp_sdf, [1]) / denominator

    verts = (edges_to_interp * edges_to_interp_sdf).sum(1)

    idx_map = idx_map.reshape(-1, 12)

    cubeindex = (occ_fx8[valid_cubes] * v_id.to(device).unsqueeze(0)).sum(-1)       #(M', )

    num_triangles = torch.from_numpy(num_triangles_table).to(device)[cubeindex]     #(M', )

    triangle_table_device = triangle_table.to(device)

    faces = torch.cat((
        torch.gather(input=idx_map[num_triangles == 1], dim=1,
                     index=triangle_table_device[cubeindex[num_triangles == 1]][:, :3]).reshape(-1, 3),
        torch.gather(input=idx_map[num_triangles == 2], dim=1,
                     index=triangle_table_device[cubeindex[num_triangles == 2]][:, :6]).reshape(-1, 3),
        torch.gather(input=idx_map[num_triangles == 3], dim=1,
                     index=triangle_table_device[cubeindex[num_triangles == 3]][:, :9]).reshape(-1, 3),
        torch.gather(input=idx_map[num_triangles == 4], dim=1,
                     index=triangle_table_device[cubeindex[num_triangles == 4]][:, :12]).reshape(-1, 3),
        torch.gather(input=idx_map[num_triangles == 5], dim=1,
                     index=triangle_table_device[cubeindex[num_triangles == 5]][:, :15]).reshape(-1, 3),
    ), dim=0)

    return verts, faces



def construct_voxel_grid(res,device):
    if 1:
        cube_corners = torch.tensor([[0, 0, 0], [1, 0, 0], [1,0,1], [0,0,1], [0,1,0], [1,1,0], [1,1,1], [0,1,1]], dtype=torch.float, device=device)
        base_cube_f=torch.arange(8).to(device)
        if isinstance(res,int):
            res=(res,res,res)
        voxel_grid_template=torch.ones(res,device=device)  #(res, res, res)

        res=torch.tensor([res],dtype=torch.float,device=device)
        coords=torch.nonzero(voxel_grid_template).float()/res
        verts=(cube_corners.unsqueeze(0)/res+coords.unsqueeze(1)).reshape(-1,3)
        cubes = (base_cube_f.unsqueeze(0) +
                 torch.arange(coords.shape[0], device=device).unsqueeze(1) * 8).reshape(-1)

        verts_rounded = torch.round(verts * 10**5) / (10**5)
        verts_unique, inverse_indices = torch.unique(verts_rounded, dim=0, return_inverse=True)
        cubes = inverse_indices[cubes.reshape(-1)].reshape(-1, 8)
        #print(verts_unique)
        #print(cubes)
        return verts_unique - 0.5, cubes