import torch
import torch.nn.functional as F
from pytorch3d._C import point_face_dist_forward
from pytorch3d.structures import Meshes
from math import pi

def get_face_areas(v,f):
    f_norms = torch.cross(v[f[:,0]]-v[f[:,1]], v[f[:,0]]-v[f[:,2]], dim=1)
    f_areas = torch.sum(f_norms**2, dim=1) **0.5 * 0.5
    return f_areas


def sample_points_on_mesh(v,f,num_pts, vn=None):
    f_areas = get_face_areas(v,f)
    cdf = torch.cumsum(F.normalize(f_areas, dim=0, p=1), dim=0)
    num_pts = int(num_pts)
    r = torch.rand(num_pts).to(v.device)
    f_idxs = torch.searchsorted(cdf, r)
    sampled_f = f[f_idxs,:]
    alpha = torch.rand(num_pts,1).to(v.device)
    beta = torch.rand(num_pts,1).to(v.device)
    k = beta**0.5
    a = 1 - k
    b = (1-alpha) * k
    c = alpha * k
    sampled_pts = v[sampled_f[:,0],:]*a + v[sampled_f[:,1],:]*b + v[sampled_f[:,2],:]*c
    if vn is not None:
        sampled_vn = vn[sampled_f[:,0],:]*a + vn[sampled_f[:,1],:]*b + vn[sampled_f[:,2],:]*c
        nd = ((sampled_vn*sampled_vn).sum(dim=-1))**0.5
        sampled_vn = sampled_vn / nd[:,None]
    else:
        sampled_vn = None
    return sampled_pts, sampled_vn


def point2mesh_error(dv, df, ov, of, scale = 1e4):
    orig_device = ov.device.type
    if 'cuda' not in orig_device:
        ov = ov.to("cuda:0")
        of = of.to("cuda:0")
    # dpcl = sample_points_on_mesh(dv,df, 1e6) * scale
    dpcl = dv * scale
    ov = ov*scale
    min_area = torch.min(get_face_areas(ov,of)).item()
    i1 = torch.LongTensor([0]).to(dpcl.device)
    dists, idxs = point_face_dist_forward(dpcl, i1, ov[of], i1, dpcl.size(0), min_area)
    errors = torch.sqrt(dists)
    upper_quantile = torch.quantile(errors, 0.85)
    large_error_points = dpcl[errors>upper_quantile]
    return errors.mean()/scale, dpcl/scale, large_error_points/scale


def get_triangle_error(ov, of, t, ns=100):
    # sample points
    r1, r2 = torch.rand(ns, device=ov.device)**0.5, torch.rand(ns, device=ov.device)
    a,b,c = 1-r1, r1*(1-r2), r1*r2
    t = t.to(ov.device)
    pts = (a[:,None]*t[0,:]+b[:,None]*t[1,:]+c[:,None]*t[2,:])*1e4
    t_n = torch.cross(t[1,:]-t[0,:], t[2,:]-t[0,:])
    t_a = torch.sqrt(torch.sum((t_n**2)))*0.5
    i1 = torch.LongTensor([0]).to(ov.device)
    ov = ov*1e4
    min_area = torch.min(get_face_areas(ov,of)).item()
    dists, _ = point_face_dist_forward(pts, i1, ov[of], i1, pts.size(0), min_area)
    return t_a*(torch.sum(dists)/1e4)/ns, pts


def find_closest_points(ov, of, pts):
    i1 = torch.LongTensor([0]).to(ov.device)
    ov*=1e4
    min_area = torch.min(get_face_areas(ov,of)).item()
    pts*=1e4
    _, idxs = point_face_dist_forward(pts, i1, ov[of], i1, pts.size(0), min_area)
    closest_faces = of[idxs]
    closest_fnorms = torch.cross(ov[closest_faces[:,0]]-ov[closest_faces[:,1]], 
                                 ov[closest_faces[:,0]]-ov[closest_faces[:,2]], dim=1)
    norm_consts = torch.sum(closest_fnorms**2, dim=1) **0.5
    closest_fnorms /= norm_consts[:,None]
    vec_to_closest = ov[closest_faces[:,0]] - pts
    distances = torch.sum(vec_to_closest*closest_fnorms , dim=1, keepdims=True)
    displacements = (distances*closest_fnorms)/1e4
    ov/=1e4
    pts/=1e4
    return displacements+pts


def get_barys(p, fv):
    v0 = fv[:,1,:] - fv[:,0,:]
    v1 = fv[:,2,:] - fv[:,0,:]
    v2 = p-fv[:,0,:]
    d00 = (v0*v0).sum(dim=-1)
    d01 = (v0*v1).sum(dim=-1)
    d11 = (v1*v1).sum(dim=-1)
    d20 = (v2*v0).sum(dim=-1)
    d21 = (v2*v1).sum(dim=-1)
    denom = d00*d11 - d01*d01
    v = (d11 * d20 - d01 * d21) / denom
    w = (d00 * d21 - d01 * d20) / denom
    u = 1.0 - v - w
    return u,v,w

def normal_error(dv, df, ov, of, scale = 1e4):
    om = Meshes([ov], [of])
    ovn = om.verts_normals_packed()
    ofn = om.faces_normals_packed()
    del om
    dvn = Meshes([dv], [df]).verts_normals_packed()
    orig_device = ov.device.type
    if 'cuda' not in orig_device:
        ov = ov.to("cuda:0")
        of = of.to("cuda:0")
    dpcl, dn = sample_points_on_mesh(dv,df, 1e6, dvn)
    dpcl = dpcl * scale
    ov = ov*scale
    min_area = torch.min(get_face_areas(ov,of)).item()
    i1 = torch.LongTensor([0]).to(dpcl.device)
    _, idxs = point_face_dist_forward(dpcl, i1, ov[of], i1, dpcl.size(0), min_area)
    pn = ofn[idxs]

    nd = ((dpcl - ov[of[idxs,0],:])*pn).sum(dim=-1)[:,None]
    proj = dpcl - nd*pn
    u,v,w = get_barys(proj, ov[of[idxs]])
    on = ovn[of[idxs,0],:]*u[:,None] + ovn[of[idxs,1],:]*v[:,None] + ovn[of[idxs,2],:]*w[:,None]
    on = on / (((on*on).sum(dim=-1))**0.5)[:,None]
    costheta = (on*dn).sum(dim=-1)
    theta = torch.acos(torch.clip(costheta,-1.,1.)).nan_to_num_()
    return theta.mean()/pi*180.

if __name__=='__main__':
    from pytorch3d.io import load_obj
    ov, of, _ = load_obj("objs_original/gnome.obj", load_textures=False, device="cuda:0")
    of = of.verts_idx
    mn,_ = ov.min(dim=0)
    ov -= mn
    ov /= ov.max()
    dv, df, _ = load_obj("mlruns/806865175946577052/918f2f91ed324828a66010836604ab24/artifacts/ns2_cs4000_hd56_nl20_/reconstruction.obj", load_textures=False, device="cuda:0")
    df = df.verts_idx
    from time import time
    t = time()
    # ne = normal_error(dv,df,ov,of)
    ne = normal_error(ov,of,dv,df)
    print(time()-t)
    print(ne)