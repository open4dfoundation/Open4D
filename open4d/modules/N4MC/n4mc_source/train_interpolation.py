import os
import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F
from PIL import Image
from torch.utils.data import Dataset,DataLoader
import math
from tqdm import tqdm
from config_load import get_config, save_config
from network import get_network, adjust_lr, diff_quantized_tensor, InterpolationTransformer, load_frame_points, LatentMapperPointNet, build_latent_codes_from_points
from dataset import get_dataset, InterpolationDataset

from fmc import dynamic_marching_cubes, construct_voxel_grid, base_cube_edges
from util import Mesh, SSIM3D, compute_tsdf_normals, ramp_weight, set_seed
import imageio
import trimesh
import time
from pytorch3d.loss import chamfer_distance
from pytorch3d.ops import sample_points_from_meshes
from pytorch3d.structures import Meshes

import os
os.environ["TORCH_USE_CUDA_DSA"] = "1"
# Usage
set_seed(4)

def train_interpolation(args):
    args.log_path = os.path.join(args.log_path, 'interpolation_' + time.strftime("%Y_%m_%d_%H_%M_%S", time.localtime()))
    os.makedirs(args.log_path, exist_ok=True)

    pretrained_autocodec = get_network(args.model, args).to(args.device)

    encoder_ckpt_path = os.path.join(args.autocodec_path, "encoder.pt")
    decoder_ckpt_path = os.path.join(args.autocodec_path, "decoder.pt")

    # Load encoder (saved in float)
    encoder_state_dict = torch.load(encoder_ckpt_path)
    pretrained_autocodec.encoder.load_state_dict(encoder_state_dict)

    # Load decoder (quantized weights)
    decoder_quanted_state_dict = torch.load(decoder_ckpt_path)
    pretrained_autocodec.decoder.load_state_dict(decoder_quanted_state_dict)

    pretrained_autocodec.eval()

    # Dataset
    voxel_dataset = InterpolationDataset(get_dataset(args.dataset, args), args)
    print("voxel datset: ", voxel_dataset[0]["embed_features"].shape, len(voxel_dataset))
    data_loader = DataLoader(dataset=voxel_dataset, batch_size=args.batch_size, shuffle=False, num_workers=1, drop_last=False)
    for step, data_dict in enumerate(data_loader):
        print("step: ", step)
        print("data_loader11: ", data_dict['indices'])
        print("Keys in data_dict:", data_dict.keys())
        for k, v in data_dict.items():
            print(f"{k}: shape = {v.shape if hasattr(v, 'shape') else type(v)}")
        break 
    val_data_loader = DataLoader(dataset=voxel_dataset, batch_size=1, shuffle=False, num_workers=1, drop_last=False)

    num_sequences = len(voxel_dataset)
    print("num_sequences: ", num_sequences)


    net = InterpolationTransformer(
        voxel_feat_dim=args.embed_dim,
        in_feat_dim=args.embed_dim,
        latent_dim=args.latent_dim,
        group_size=args.group_size,
        voxel_res=(args.embed_hwd, args.embed_hwd, args.embed_hwd)
    ).to(args.device)

    ssim_1_channel = SSIM3D(channel=1).to(args.device)
    ssim_3_channel = SSIM3D(channel=3).to(args.device)


    centers_pattern = "./frame_0res_2000_*"
    centers = load_frame_points(
        pattern=centers_pattern,
        device=args.device,
        assume_extension=".xyz" 
    ) 

    latent_mapper = LatentMapperPointNet(latent_dim=args.latent_dim).to(args.device)

    optimizer = torch.optim.Adam(
        [
            {"params": net.parameters(), "lr": args.lr},
            {"params": latent_mapper.parameters(), "lr": args.lr}
        ]
    )

    for epoch in range(1, args.n_epoch + 1):
        for step, data_dict in enumerate(data_loader):
            embed_features = data_dict["embed_features"].to(args.device)  
            indices = data_dict["indices"]
            seq_ids = data_dict["seq_id"]
        
            latent_codes = build_latent_codes_from_points(
                indices=indices,
                points=centers,
                mapper=latent_mapper,
                zero_based=True
            )
           
            f_start = embed_features[:, 0] 
            f_end = embed_features[:, -1] 
            gt_f_intermediate = embed_features[:, 1:-1]  
            gt_masks = data_dict["masks"].to(args.device)
            gt_masks_intermediate = gt_masks[:, 1:-1]
           

            sdf_offsets = data_dict["sdf_offsets"].to(args.device)
            gt_sdf_offsets = sdf_offsets[:, 1:-1]

            B = gt_sdf_offsets.shape[0]  
            Gm = gt_sdf_offsets.shape[1] 
            gt_sdf_offsets = gt_sdf_offsets.contiguous().view(B * Gm, args.voxel_grid_res+1, args.voxel_grid_res+1, args.voxel_grid_res+1, 4)
           

            pred_f_intermediate = net(f_start, f_end, latent_codes) 
            quant_pred_f_intermediate = diff_quantized_tensor(pred_f_intermediate, args.num_bits)


            pred_sdf_offset = pretrained_autocodec(embed_features=quant_pred_f_intermediate.view(B * Gm, args.embed_hwd, args.embed_hwd, args.embed_hwd, args.embed_dim))
            gt_masks = gt_masks_intermediate.contiguous().view(B * Gm, args.voxel_grid_res+1, args.voxel_grid_res+1, args.voxel_grid_res+1, 1)
        
            loss = 0.0

            tsdf_loss = F.l1_loss(pred_sdf_offset, gt_sdf_offsets)
            f_loss = F.l1_loss(pred_f_intermediate, gt_f_intermediate)
            emb_loss =  tsdf_loss
            B = f_start.shape[0]
            Gm = args.group_size - 2
            loss += emb_loss

            if args.important_weight:
                mask_loss = args.important_weight * torch.sum(gt_masks * torch.abs(gt_sdf_offsets - pred_sdf_offset)) / torch.sum(gt_masks)
                #print("mask loss: ", mask_loss)
                loss += mask_loss

            if args.ssim_weight:
                ssim_1 = 1 - ssim_1_channel(pred_sdf_offset[..., 0:1].permute(0, 4, 1, 2, 3),gt_sdf_offsets[..., 0:1].permute(0, 4, 1, 2, 3))
                ssim_3 = 1 - ssim_3_channel(pred_sdf_offset[..., 1:].permute(0, 4, 1, 2, 3),gt_sdf_offsets[..., 1:].permute(0, 4, 1, 2, 3))
                ssim_loss = args.ssim_weight * (ssim_1 + ssim_3)
                loss += ssim_loss

            if args.embed_reg:
                loss += args.embed_reg * torch.abs(quant_pred_f_intermediate).mean()

           
            current_lr = adjust_lr(optimizer, (epoch - 1) % args.n_epoch, step, len(voxel_dataset), args)
            optimizer.zero_grad()
            loss.backward()
            optimizer.step()

            if step % 30 == 0:
                print('%s epoch: %04d, step: %d/%d, current lr: %f, loss: %f' % (
                    time.strftime("%Y-%m-%d %H:%M:%S", time.localtime()),
                    epoch, step, len(data_loader), current_lr, loss.cpu().detach().numpy()
                ))

        if epoch % args.val_frequence == 0:
            decoding_time = 0
            decoding_time_network = []
            os.makedirs(os.path.join(args.log_path, f'checkpoint_{epoch:04d}', 'rec_mesh'), exist_ok=True)
            os.makedirs(os.path.join(args.log_path, f'checkpoint_{epoch:04d}', 'gt_mesh'), exist_ok=True)
            latent_code_list = []
            start=time.time()
            x_nx3, cube_fx8 = construct_voxel_grid(args.voxel_grid_res, args.device)
            scale = 2
            x_nx3 *= scale
            end = time.time()
            #decoding_time += end-start
            print("scale", scale)
            print("construct_voxel_grid time", end-start)
            for index_t, data_dict in enumerate(tqdm(val_data_loader, desc=f'val epoch {epoch}')):
                try:
                    embed_features = data_dict["embed_features"].to(args.device) 
                    indices = data_dict["indices"]

                    latent_codes = build_latent_codes_from_points(
                        indices=indices,
                        points=centers,
                        mapper=latent_mapper,
                        zero_based=True
                    )
                    latent_code_list.append(latent_codes.detach().cpu())
                    indices = indices[0].detach().cpu().numpy()
                    seq_ids = data_dict["seq_id"]

                    f_start, f_end = embed_features[:, 0], embed_features[:, -1]

                    pred_f_intermediate = net(f_start, f_end, latent_codes) 
                    pred_f_intermediate = pred_f_intermediate.view(args.group_size - 2, args.embed_hwd, args.embed_hwd, args.embed_hwd, args.embed_dim)


                    gt_f_intermediate = embed_features[:, 1:-1].contiguous().view(args.group_size - 2, args.embed_hwd, args.embed_hwd, args.embed_hwd, args.embed_dim)


                    quant_f_start = diff_quantized_tensor(f_start, args.num_bits)
                    quant_f_end = diff_quantized_tensor(f_end, args.num_bits)
                    with torch.no_grad():
                        start_sdf_offset = pretrained_autocodec(embed_features=quant_f_start)  
                        end_sdf_offset = pretrained_autocodec(embed_features=quant_f_end) 


                    start_sdf = start_sdf_offset[..., 0].reshape(-1) 
                    start_offset = start_sdf_offset[..., 1:].reshape(-1, 3)  

                    end_sdf = end_sdf_offset[..., 0].reshape(-1) 
                    end_offset = end_sdf_offset[..., 1:].reshape(-1, 3) 

                    start_grid_verts = x_nx3 + start_offset * (2 - 1e-8) / (args.voxel_grid_res * 2)

                    start_vertices, start_faces = dynamic_marching_cubes(start_grid_verts, cube_fx8, start_sdf)

                    end_grid_verts = x_nx3 + end_offset * (2 - 1e-8) / (args.voxel_grid_res * 2)
                    end_vertices, end_faces = dynamic_marching_cubes(end_grid_verts, cube_fx8, end_sdf)
                    end = time.time()
                    decoding_time += end-start
                    # Save mesh
                    if epoch % 100 == 0:
                        mesh_start = trimesh.Trimesh(vertices=start_vertices.detach().cpu().numpy(),
                                                  faces=start_faces.detach().cpu().numpy(), process=False)
                        mesh_start.export(os.path.join(args.log_path,
                                                    f'checkpoint_{epoch:04d}', 'gt_mesh',
                                                    f'gt_mesh_{indices[0]}_{index_t}.obj'))
                        mesh_start.export(os.path.join(args.log_path,
                                                       f'checkpoint_{epoch:04d}', 'rec_mesh',
                                                       f'rec_mesh_{indices[0]}_{index_t}.obj'))

                        mesh_end = trimesh.Trimesh(vertices=end_vertices.detach().cpu().numpy(),
                                                  faces=end_faces.detach().cpu().numpy(), process=False)
                        mesh_end.export(os.path.join(args.log_path,
                                                    f'checkpoint_{epoch:04d}', 'gt_mesh',
                                                    f'gt_mesh_{indices[-1]}_{index_t}.obj'))
                        mesh_end.export(os.path.join(args.log_path,
                                                     f'checkpoint_{epoch:04d}', 'rec_mesh',
                                                     f'rec_mesh_{indices[-1]}_{index_t}.obj'))
                    indices_inter =indices[1:-1]
                    for i in range(args.group_size - 2):
                        inter_decoding_time = 0
                        start = time.time()
                        f_i = pred_f_intermediate[i].unsqueeze(0) 
                        quant_f_i = diff_quantized_tensor(f_i, args.num_bits)
                        end = time.time()
                        inter_decoding_time += end-start
                        gt_f_i = gt_f_intermediate[i].unsqueeze(0)
                        quant_gt_f_i = diff_quantized_tensor(gt_f_i, args.num_bits)
                        with torch.no_grad():
                            start = time.time()
                            pred_sdf_offset = pretrained_autocodec(embed_features=quant_f_i)  
                            end = time.time()
                            inter_decoding_time += end-start
                            gt_sdf_offset = pretrained_autocodec(embed_features=quant_gt_f_i) 


                        start = time.time()
                        pred_sdf = pred_sdf_offset[..., 0].reshape(-1) 
                        pred_offset = pred_sdf_offset[..., 1:].reshape(-1, 3) 

                        grid_verts = x_nx3 + pred_offset * (2 - 1e-8) / (args.voxel_grid_res * 2)
                        vertices, faces = dynamic_marching_cubes(grid_verts, cube_fx8, pred_sdf)

                        end = time.time()
                        inter_decoding_time += end-start
                        decoding_time_network.append(inter_decoding_time)

                        gt_sdf = gt_sdf_offset[..., 0].reshape(-1) 
                        gt_offset = gt_sdf_offset[..., 1:].reshape(-1, 3) 

                        gt_grid_verts = x_nx3 + gt_offset * (2 - 1e-8) / (args.voxel_grid_res * 2)
                        gt_vertices, gt_faces = dynamic_marching_cubes(gt_grid_verts, cube_fx8, gt_sdf)


                        # Save mesh
                        if epoch % 100 == 0:
                            mesh_np = trimesh.Trimesh(vertices=vertices.detach().cpu().numpy(),
                                                      faces=faces.detach().cpu().numpy(), process=False)
                            mesh_np.export(os.path.join(args.log_path,
                                                        f'checkpoint_{epoch:04d}', 'rec_mesh',
                                                        f'rec_mesh_{indices_inter[i]}_{index_t}.obj'))

                            mesh_gt = trimesh.Trimesh(vertices=gt_vertices.detach().cpu().numpy(),
                                                      faces=gt_faces.detach().cpu().numpy(), process=False)
                            mesh_gt.export(os.path.join(args.log_path,
                                                        f'checkpoint_{epoch:04d}', 'gt_mesh',
                                                        f'gt_mesh_{indices_inter[i]}_{index_t}.obj'))
                except Exception as e:
                    print(f"Validation error at index {index_t}: {e}")
                    pass
            print('decoding time network: ', decoding_time, np.mean(decoding_time_network))

            all_latent_codes = torch.cat(latent_code_list, dim=0)
            quantized_latent_codes = diff_quantized_tensor(all_latent_codes, num_bits=args.num_bits)

            # Save model
            torch.save(net.state_dict(), os.path.join(args.log_path, f'checkpoint_{epoch:04d}', 'transformer.pt'))
            net.save_quanted_weights(os.path.join(args.log_path, 'checkpoint_%04d' % epoch, 'transformer_compressed_lossy.pt'), args.num_bits)
            net.save_quanted_weights_lossless(os.path.join(args.log_path, 'checkpoint_%04d' % epoch, 'transformer_compressed.pt'))
            torch.save(latent_mapper.state_dict(), os.path.join(args.log_path, f'checkpoint_{epoch:04d}', 'latent_mapper.pt'))

            torch.save(all_latent_codes, os.path.join(args.log_path, f'checkpoint_{epoch:04d}', 'latent_codes.pt'))
            torch.save(quantized_latent_codes, os.path.join(args.log_path, f'checkpoint_{epoch:04d}', 'latent_codes_compressed.pt'))


        # Save final model and latent codes
    torch.save(net.state_dict(), os.path.join(args.log_path, 'transformer_last.pt'))
    net.save_quanted_weights(os.path.join(args.log_path, 'transformer_compressed_lossy.pt'), args.num_bits)
    net.save_quanted_weights_lossless(os.path.join(args.log_path, 'transformer_compressed.pt'))
    torch.save(latent_mapper.state_dict(), os.path.join(args.log_path, f'checkpoint_{epoch:04d}', 'latent_mapper.pt'))

    torch.save(all_latent_codes, os.path.join(args.log_path, 'latent_codes.pt'))
    torch.save(quantized_latent_codes,os.path.join(args.log_path, 'latent_codes_compressed.pt'))




if __name__=='__main__':
    args=get_config().parse_args()
    print("args: ", args)
    torch.cuda.empty_cache()
    train_interpolation(args)
