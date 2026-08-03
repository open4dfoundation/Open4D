import os
import numpy as np
import argparse
import torch
import nvdiffrast.torch as dr
import trimesh
import random
from util import *
import render
import loss
import imageio

from fmc import dynamic_marching_cubes, construct_voxel_grid, base_cube_edges
import point_cloud_utils as pcu

def mse(t1, t2):
    return torch.mean((t1 - t2) ** 2).item()


def lr_schedule(iter):
    return max(0.0, 10**(-(iter)*0.004)) # Exponential falloff from [1.0, 0.1] over 500 epochs.    

class STEQuantize(torch.autograd.Function):
  """Straight-Through Estimator for Quantization.

  Forward pass implements quantization by rounding to integers,
  backward pass is set to gradients of the identity function.
  """
  @staticmethod
  def forward(ctx, x):
    ctx.save_for_backward(x)
    return x.round()

  @staticmethod
  def backward(ctx, grad_outputs):
    return grad_outputs
  
def diff_quantized_tensor(input,num_bits=8,min=-1,max=1,quant=True):
    input=torch.clamp(input,min,max)
    if True:
        quant=STEQuantize.apply
        scale=(max - min) / (2**num_bits)
        quanted_tensor=quant((input-min)/(scale))*scale+min
        return quanted_tensor
    else:
        return input 
    

def _resolve_sdf_sign(init_sdf_np, voxel_grid_res):
    sdf_3d = init_sdf_np.reshape(voxel_grid_res + 1, voxel_grid_res + 1, voxel_grid_res + 1)
    boundary_vals = np.concatenate(
        [
            sdf_3d[0, :, :].ravel(),
            sdf_3d[-1, :, :].ravel(),
            sdf_3d[:, 0, :].ravel(),
            sdf_3d[:, -1, :].ravel(),
            sdf_3d[:, :, 0].ravel(),
            sdf_3d[:, :, -1].ravel(),
        ]
    )
    if np.median(boundary_vals) < 0:
        init_sdf_np *= -1.0
    return init_sdf_np


def opt_fmc(
    input_v,
    input_f,
    iter=500,
    train_res=[2048,2048],
    lr=0.01,
    batch=4,
    voxel_grid_res=255,
    device='cuda',
    sdf_reg_weights=0,
    truncation_vox=3.0,
    quantize_during_optimization=True,
    quantize_before_save=True,
    optimize_deform=True,
):
    x_nx3, cube_fx8 = construct_voxel_grid(voxel_grid_res,device)
    scale = 2
    x_nx3 *= scale # scale up the grid so that it's larger than the target object
    print("scale", scale)
    all_edges = cube_fx8[:, base_cube_edges].reshape(-1, 2)
    grid_edges = torch.unique(all_edges, dim=0)

    gt_v=input_v
    gt_f=input_f

    gt_mesh=Mesh(torch.from_numpy(gt_v).float().to(device),torch.from_numpy(gt_f).long().to(device))
    gt_mesh.auto_normals()

    init_sdf_np,_,_=pcu.signed_distance_to_mesh(x_nx3.cpu().numpy(),gt_v.astype(np.float32),gt_f)
    init_sdf_np = _resolve_sdf_sign(init_sdf_np, voxel_grid_res)
    #print("init_sdf_np: ", init_sdf_np, init_sdf_np.shape)
    sdf=torch.from_numpy(init_sdf_np).float().to(device)
    #print("1sdf: ", sdf, sdf.shape)
    voxel_size = (2 * scale) / voxel_grid_res
    trunc_dist = max(1e-8, truncation_vox * voxel_size)
    sdf=torch.clip(sdf / trunc_dist,-1,1 )    #.cpu().numpy()
    #print("2sdf: ", sdf, sdf.shape)
    #deform = torch.zeros_like(x_nx3).cpu().numpy()

    sdf    = torch.nn.Parameter(sdf.clone().detach(), requires_grad=True)
    deform = torch.nn.Parameter(torch.zeros_like(x_nx3), requires_grad=True)

    optim_params = [sdf, deform] if optimize_deform else [sdf]
    optimizer = torch.optim.Adam(optim_params, lr=lr)
    scheduler = torch.optim.lr_scheduler.LambdaLR(optimizer, lr_lambda=lambda x: lr_schedule(x)) 
    
    #for it in tqdm(range(iter)):
    for it in range(iter):
        optimizer.zero_grad()
        # sample random camera poses
        mv, mvp = render.get_random_camera_batch(batch, iter_res=train_res, device=device)
        # render gt mesh
        target = render.render_mesh(gt_mesh, mv, mvp, train_res)
        # extract and render FlexiCubes mesh
        if optimize_deform:
            if quantize_during_optimization:
                deform_eval = diff_quantized_tensor(deform)
            else:
                deform_eval = torch.tanh(deform)
            grid_verts = x_nx3 + (2-1e-8) / (voxel_grid_res * 2) * deform_eval
        else:
            grid_verts = x_nx3

        if quantize_during_optimization:
            sdf_eval = diff_quantized_tensor(sdf)
        else:
            sdf_eval = torch.clamp(sdf, -1, 1)

        vertices, faces=dynamic_marching_cubes(grid_verts,cube_fx8,sdf_eval)
        flexicubes_mesh = Mesh(vertices, faces)
        buffers = render.render_mesh(flexicubes_mesh, mv, mvp, train_res)
        
        mask_loss = (buffers['mask'] - target['mask']).abs().mean()
        depth_loss = (((((buffers['depth'] - (target['depth']))* target['mask'])**2).sum(-1)+1e-8)).sqrt().mean() * 10

        if sdf_reg_weights:
            t_iter = it / iter
            sdf_weight = sdf_reg_weights - (sdf_reg_weights - sdf_reg_weights/20)*min(1.0, 4.0 * t_iter)
            reg_loss = loss.sdf_reg_loss(sdf, grid_edges).mean() * sdf_weight # Loss to eliminate internal floaters that are not visible
        else:
            reg_loss=0

        deform_reg = 10*torch.abs(deform).mean() if optimize_deform else 0
        total_loss = mask_loss + depth_loss + reg_loss + deform_reg
        if not total_loss.requires_grad:
            # Keep the optimization alive if raster output gets disconnected (e.g., degenerate surface).
            total_loss = total_loss + 1e-4 * (sdf ** 2).mean()
            if optimize_deform:
                total_loss = total_loss + 1e-4 * (deform ** 2).mean()

        total_loss.backward()
        optimizer.step()
        scheduler.step()    

    with torch.no_grad():
        if optimize_deform:
            if quantize_during_optimization:
                deform_eval = diff_quantized_tensor(deform)
            else:
                deform_eval = torch.tanh(deform)
            grid_verts = x_nx3 + (2-1e-8) / (voxel_grid_res * 2) * deform_eval
        else:
            grid_verts = x_nx3

        if quantize_during_optimization:
            sdf_eval = diff_quantized_tensor(sdf)
        else:
            sdf_eval = torch.clamp(sdf, -1, 1)
        vertices, faces = dynamic_marching_cubes(grid_verts, cube_fx8, sdf_eval)

    mesh_np = trimesh.Trimesh(vertices = vertices.detach().cpu().numpy(), faces=faces.detach().cpu().numpy(), process=False)

    if quantize_before_save:
        sdf_np=diff_quantized_tensor(sdf).detach().cpu().numpy()
        deform_np=diff_quantized_tensor(deform).detach().cpu().numpy()
    else:
        sdf_np=torch.clamp(sdf, -1, 1).detach().cpu().numpy()
        deform_np=torch.tanh(deform).detach().cpu().numpy() if optimize_deform else np.zeros_like(x_nx3.cpu().numpy())
    
    return sdf_np.reshape(voxel_grid_res+1,voxel_grid_res+1,voxel_grid_res+1,1),deform_np.reshape(voxel_grid_res+1,voxel_grid_res+1,voxel_grid_res+1,3),mesh_np, scale


    #return sdf.reshape(voxel_grid_res+1,voxel_grid_res+1,voxel_grid_res+1,1),deform.reshape(voxel_grid_res+1,voxel_grid_res+1,voxel_grid_res+1,3)


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--niter',type=int,default=500)
    parser.add_argument('--batch',type=int,default=4)
    parser.add_argument('--data_path',type=str)
    parser.add_argument('--save_path',type=str)
    parser.add_argument('--num_frames',type=int)
    parser.add_argument('--voxel_grid_res',type=int, default=127)
    parser.add_argument('--high_quality_tsdf', action='store_true')
    parser.add_argument('--truncation_vox', type=float, default=3.0)
    parser.add_argument('--optimize_deform', action=argparse.BooleanOptionalAction, default=None)
    parser.add_argument('--quantize_during_optimization', action=argparse.BooleanOptionalAction, default=None)
    parser.add_argument('--quantize_before_save', action=argparse.BooleanOptionalAction, default=None)
    args=parser.parse_args()
    #seq_name=args.seq   #'soldier'
    voxel_grid_res=args.voxel_grid_res
    save_path= args.save_path #     

    data_path=args.data_path 

    os.makedirs(save_path,exist_ok=True)
    

    index_list=list(range(args.num_frames))
    #random.shuffle(index_list)
    torch.cuda.empty_cache()
    optimize_deform = args.optimize_deform if args.optimize_deform is not None else (not args.high_quality_tsdf)
    quantize_during_optimization = (
        args.quantize_during_optimization
        if args.quantize_during_optimization is not None
        else (not args.high_quality_tsdf)
    )
    quantize_before_save = (
        args.quantize_before_save
        if args.quantize_before_save is not None
        else (not args.high_quality_tsdf)
    )
    print(
        f"high_quality_tsdf={args.high_quality_tsdf}, truncation_vox={args.truncation_vox}, "
        f"optimize_deform={optimize_deform}, quantize_during_optimization={quantize_during_optimization}, "
        f"quantize_before_save={quantize_before_save}"
    )
    #print(index_list)
    for i in tqdm(index_list):
        gt_mesh=trimesh.load_mesh(os.path.join(data_path,f'frame_0{i:03}.obj')) #change here for reading meshes
        gt_v,gt_f=gt_mesh.vertices,gt_mesh.faces
        #print(gt_v.shape, gt_f.shape)

        #if os.path.exists(os.path.join(save_path,f'data','%04d.npz'%i)):
        #    print(os.path.join(save_path,f'data','%04d.npz'%i),' exists, skip !!!')
        #    continue

        sdf_grid, offset_grid, mesh_np, scale=opt_fmc(
            gt_v,
            gt_f,
            iter=args.niter,
            voxel_grid_res=voxel_grid_res,
            sdf_reg_weights=0,
            batch=args.batch,
            truncation_vox=args.truncation_vox,
            quantize_during_optimization=quantize_during_optimization,
            quantize_before_save=quantize_before_save,
            optimize_deform=optimize_deform,
        )

        os.makedirs(os.path.join(save_path,'meshes','%04d'%i),exist_ok=True)
        os.makedirs(os.path.join(save_path,f'data',),exist_ok=True)
        os.makedirs(os.path.join(save_path,f'data', 'TSDF'),exist_ok=True)

        np.savez_compressed(os.path.join(save_path,f'data','%04d.npz'%i),sdf=sdf_grid, offset=offset_grid)
        np.savez_compressed(os.path.join(save_path,f'data', 'TSDF','%04d.npz'%i),sdf=sdf_grid)
        #print(gt_v.dtype, gt_f.dtype)
        #pcu.save_mesh_vf(os.path.join(save_path,'meshes','%04d'%i,'gt_mesh.obj'),gt_v,gt_f, dtype=np.float32)
        #print(type(mesh_np))
        mesh_np.export(os.path.join(save_path, 'meshes', f'{i:04d}', f'mesh{i:04d}.obj'))
