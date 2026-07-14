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
from network import get_network, adjust_lr, diff_quantized_tensor, InterpolationTransformerCrossAttnV5, InterpolationTransformerCrossAttnV6, load_frame_points, LatentMapperPointNet, build_latent_codes_from_points
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
    #print("data_loader: ", data_loader)
    for step, data_dict in enumerate(data_loader):
        print("step: ", step)
        print("data_loader11: ", data_dict['indices'])
        print("Keys in data_dict:", data_dict.keys())
        for k, v in data_dict.items():
            print(f"{k}: shape = {v.shape if hasattr(v, 'shape') else type(v)}")
        break  # remove this when done inspecting
    val_data_loader = DataLoader(dataset=voxel_dataset, batch_size=1, shuffle=False, num_workers=1, drop_last=False)

    num_sequences = len(voxel_dataset)
    print("num_sequences: ", num_sequences)
    #latent_codes = torch.randn((num_sequences, args.group_size - 2, args.latent_dim)).to(args.device)
    #print("latent_codes: ", latent_codes.shape)
    #latent_codes.requires_grad = True


    # Network
    net = InterpolationTransformerCrossAttnV6(
        voxel_feat_dim=args.embed_dim,
        in_feat_dim=args.embed_dim,
        latent_dim=args.latent_dim,
        group_size=args.group_size,
        voxel_res=(args.embed_hwd, args.embed_hwd, args.embed_hwd)
    ).to(args.device)
    print("net: ", net)
    #optimizer = torch.optim.Adam(net.parameters(), lr=args.lr)
    '''
    optimizer = torch.optim.Adam(
        [
            {"params": net.parameters(), "lr": args.lr},
            {"params": [latent_codes], "lr": args.lr}
        ]
    )
    '''
    ssim_1_channel = SSIM3D(channel=1).to(args.device)
    ssim_3_channel = SSIM3D(channel=3).to(args.device)

    #centers_pattern = "/home/frozzzen/Documents/Github_SINRG/TSMC/arap-volume-tracking/data/basketball-100-output-max-2000/frame_0res_2000_*"
    #centers_pattern = "/home/frozzzen/Documents/Github_SINRG/TSMC/arap-volume-tracking/data/combined-100-max-2000/frame_0res_2000_*"
    #centers_pattern = "/home/frozzzen/Documents/Github_SINRG/TSMC/arap-volume-tracking/data/combined-3-100-max-2000/frame_0res_2000_*"
    #centers_pattern = "/home/frozzzen/Documents/Github_SINRG/TSMC/arap-volume-tracking/data/mitch-100-output-max-2000/frame_0res_2000_*"
    #centers_pattern = "/home/frozzzen/Documents/Github_SINRG/TSMC/arap-volume-tracking/data/thomas-100-output-max-2000/frame_0res_2000_*"
    #centers_pattern = "/home/frozzzen/Documents/Github_SINRG/TSMC/arap-volume-tracking/data/dancer-300-max-2000/dancer_fr0res_2000_*"
    centers_pattern = "/home/frozzzen/Documents/Github_SINRG/TSMC/arap-volume-tracking/data/cmu_arena4-max-2000/frame_0res_2000_*"
    centers = load_frame_points(
        #pattern="/home/frozzzen/Documents/Github_SINRG/TSMC/arap-volume-tracking/data/dancer-300-max-2000/dancer_fr0res_2000_*",  # matches ..._001, ..._002, ...
        pattern=centers_pattern,
        device=args.device,
        assume_extension=".xyz"  # or None if mixed
    )  # (T,3)
    print(centers_pattern)
    print(centers.shape, centers[0])

    latent_mapper = LatentMapperPointNet(latent_dim=args.latent_dim).to(args.device)

    optimizer = torch.optim.Adam(
        [
            {"params": net.parameters(), "lr": args.lr},
            {"params": latent_mapper.parameters(), "lr": args.lr}
            #{"params": pretrained_autocodec.parameters(), "lr": 0.1 * args.lr}
        ]
    )

    for epoch in range(1, args.n_epoch + 1):
        for step, data_dict in enumerate(data_loader):
            embed_features = data_dict["embed_features"].to(args.device)  # Shape: (batch, group_size, 4, 4, 4, 16)
            indices = data_dict["indices"]  # Shape: (batch, group_size)
            #print("embed_features: ", embed_features.shape, len(indices), len(indices))
            #print("indices: ", indices, indices.shape)
            # Select latent codes for the current batch
            seq_ids = data_dict["seq_id"]
            #batch_indices = (indices[:, 0]// args.group_size)  # Use the first index of each sequence
            #print("batch_indices: ", batch_indices, batch_indices.shape, batch_indices[0])
            latent_codes = build_latent_codes_from_points(
                indices=indices,
                points=centers,
                mapper=latent_mapper,
                zero_based=True
            )
            #print("latent code:", latent_codes.shape)
            #batch_latent_codes = latent_codes[seq_ids].to(args.device)  # Shape: (batch, group_size-2, latent_dim)
            #print("batch_latent_codes: ", batch_latent_codes.shape, len(batch_latent_codes))
            # Inputs: F_start (F_1), F_end (F_group_size)
            f_start = embed_features[:, 0]  # (batch, 4, 4, 4, 16)
            #print("f_start: ", f_start.shape)
            f_end = embed_features[:, -1]  # (batch, 4, 4, 4, 16)
            #print("f_end: ", f_end.shape)
            # Ground truth: intermediate features F_2, ..., F_(group_size-1)
            gt_f_intermediate = embed_features[:, 1:-1]  # (batch, group_size-2, 4, 4, 4, 16)
            #print("gt_f_intermediate: ", gt_f_intermediate.shape)

            gt_masks = data_dict["masks"].to(args.device)
            #print("gt_masks: ", gt_masks.shape)
            gt_masks_intermediate = gt_masks[:, 1:-1]
            #print("gt_masks_intermediate: ", gt_masks_intermediate[0][0].shape, torch.sum(gt_masks_intermediate[0][0]), gt_masks_intermediate.shape)
            #print("shape: ", gt_masks[:, 1:-1].contiguous().view(args.batch_size*(args.group_size - 2), 128, 128, 128, 1).shape)

            sdf_offsets = data_dict["sdf_offsets"].to(args.device)
            gt_sdf_offsets = sdf_offsets[:, 1:-1]
            #print("gt_sdf_offsets: ", gt_sdf_offsets.shape, step)
            #print("gt_sdf_offsets_intermediate: ", gt_sdf_offsets_intermediate.shape)
            B = gt_sdf_offsets.shape[0]  # actual batch size
            Gm = gt_sdf_offsets.shape[1]  # actual group_size - 2
            gt_sdf_offsets = gt_sdf_offsets.contiguous().view(B * Gm, args.voxel_grid_res+1, args.voxel_grid_res+1, args.voxel_grid_res+1, 4)
            #gt_sdf_offsets = gt_sdf_offsets.contiguous().view(args.batch_size*(args.group_size - 2), 128, 128, 128, 4)
            #print("gt_sdf_offsets_intermediate: ", gt_sdf_offsets.shape)

            # Forward pass
            pred_f_intermediate = net(f_start, f_end, latent_codes)  # (batch, group_size-2, 4, 4, 4, 16)
            #print("pred_f_intermediate: ", pred_f_intermediate.shape)
            #print("pred_f_intermediate: ", pred_f_intermediate.view(args.batch_size*(args.group_size - 2), 4, 4, 4, 16).shape)

            quant_pred_f_intermediate = diff_quantized_tensor(pred_f_intermediate, args.num_bits)

            # Loss
            pred_sdf_offset = pretrained_autocodec(embed_features=quant_pred_f_intermediate.view(B * Gm, args.embed_hwd, args.embed_hwd, args.embed_hwd, args.embed_dim))
            #gt_sdf_offset = pretrained_autocodec(embed_features=gt_f_intermediate.contiguous().view(args.batch_size*(args.group_size - 2), 4, 4, 4, 16))
            #print("pred_sdf_offset: ", pred_sdf_offset.shape)
            #print("gt_sdf_offset: ", gt_sdf_offset.shape)
            #print("pred_sdf_offset[...,0:1]: ", pred_sdf_offset[...,0:1].shape)
            #gt_masks = (torch.abs(gt_masks[:, 1:-1].contiguous().view(args.batch_size*(args.group_size - 2), 128, 128, 128, 1))<args.mask_threshold).float()
            gt_masks = gt_masks_intermediate.contiguous().view(B * Gm, args.voxel_grid_res+1, args.voxel_grid_res+1, args.voxel_grid_res+1, 1)
            #print("gt_masks: ", torch.sum(gt_masks[0]), torch.sum(gt_masks[1]), torch.sum(gt_masks[2]),torch.sum(gt_masks[3]), gt_masks.shape)

            #print("f loss:", torch.mean(torch.abs(pred_f_intermediate - gt_f_intermediate)))
            #print("v loss:", torch.mean(torch.abs(gt_sdf_offsets - pred_sdf_offset)))
            #loss = torch.mean(torch.abs(pred_f_intermediate - gt_f_intermediate))
            #emb_loss = torch.mean(torch.abs(pred_f_intermediate - gt_f_intermediate))
            loss = 0.0

            # 1. Feature embedding loss
            #emb_loss = F.l1_loss(quant_pred_f_intermediate, gt_f_intermediate) + F.l1_loss(pred_sdf_offset, gt_sdf_offsets)
            tsdf_loss = F.l1_loss(pred_sdf_offset, gt_sdf_offsets)
            f_loss = F.l1_loss(pred_f_intermediate, gt_f_intermediate)
            #print("emb loss: ", tsdf_loss, f_loss)
            emb_loss =  tsdf_loss
            B = f_start.shape[0]
            Gm = args.group_size - 2
            loss += emb_loss

            # 3. Important region TSDF loss (Huber, sign-preserving)
            if args.important_weight:
                '''
                mask_loss = args.important_weight * (
                        F.smooth_l1_loss(
                            pred_sdf_offset * gt_masks,
                            gt_sdf_offsets * gt_masks,
                            beta=1,
                            reduction="sum"
                        ) / gt_masks.sum().clamp(min=1.0)
                )
                '''
                mask_loss = args.important_weight * torch.sum(gt_masks * torch.abs(gt_sdf_offsets - pred_sdf_offset)) / torch.sum(gt_masks)
                #print("mask loss: ", mask_loss)
                loss += mask_loss

            # 4. SSIM
            if args.ssim_weight:
                ssim_1 = 1 - ssim_1_channel(pred_sdf_offset[..., 0:1].permute(0, 4, 1, 2, 3),gt_sdf_offsets[..., 0:1].permute(0, 4, 1, 2, 3))
                ssim_3 = 1 - ssim_3_channel(pred_sdf_offset[..., 1:].permute(0, 4, 1, 2, 3),gt_sdf_offsets[..., 1:].permute(0, 4, 1, 2, 3))
                ssim_loss = args.ssim_weight * (ssim_1 + ssim_3)
                #print("ssim loss: ", ssim_loss)
                loss += ssim_loss

            if args.embed_reg:
                loss += args.embed_reg * torch.abs(quant_pred_f_intermediate).mean()

            if args.surface_weight and epoch > 700:
                w_surface = args.surface_weight
                if w_surface > 0:
                    tau = getattr(args, "surface_band", 0.05)  # Tighten from 0.1 for sharper surface focus
                    surface_mask = (torch.abs(gt_sdf_offsets[..., 0]) < tau).float()  # (B, H, W, D)

                    # TSDF surface loss (on SDF channel)
                    tsdf_surface_loss = w_surface * F.smooth_l1_loss(
                        pred_sdf_offset[..., 0] * surface_mask,
                        gt_sdf_offsets[..., 0] * surface_mask,
                        beta=0.01,
                        reduction="sum"
                    ) / surface_mask.sum().clamp(min=1.0)
                    loss += tsdf_surface_loss

                    # New: Offset surface loss (on offset channels, critical for grid warping)
                    offset_surface_loss = w_surface * F.smooth_l1_loss(
                        pred_sdf_offset[..., 1:] * surface_mask.unsqueeze(-1),  # Broadcast mask to 3 channels
                        gt_sdf_offsets[..., 1:] * surface_mask.unsqueeze(-1),
                        beta=0.01,
                        reduction="sum"
                    ) / (surface_mask.sum() * 3).clamp(min=1.0)  # Normalize by channels
                    loss += offset_surface_loss
                    #print("surface loss: ", tsdf_surface_loss, offset_surface_loss)
            #if epoch < 200:
            #    loss = mask_loss
            '''
            if epoch > 1000:  # give buffer before ramp
                w_mesh = ramp_weight(epoch, 800, 800, 1)  # Chamfer target weight
                w_normal = ramp_weight(epoch, 800, 800, 10) # Normals
                w_volume = ramp_weight(epoch, 800, 800, 1)  # Volume
                # flatten intermediate frames: (B*Gm, 4, 4, 4, 16)
                #print("mesh loss")
                pred_f_intermediate_viewed = pred_f_intermediate.view(B * Gm, 4, 4, 4, 16)
                gt_f_intermediate_viewed = gt_f_intermediate.contiguous().view(B * Gm, 4, 4, 4, 16)

                # quantize in batch
                quant_f_all = diff_quantized_tensor(pred_f_intermediate_viewed, args.num_bits)
                quant_gt_all = diff_quantized_tensor(gt_f_intermediate_viewed, args.num_bits)

                with torch.no_grad():
                    # decode all frames in batch
                    pred_sdf_offset_all = pretrained_autocodec(embed_features=quant_f_all)  # (B*Gm, 128, 128, 128, 4)
                    gt_sdf_offset_all = pretrained_autocodec(embed_features=quant_gt_all)

                    pred_sdf_all = pred_sdf_offset_all[..., 0]  # (B*Gm, 128, 128, 128)
                    pred_offset_all = pred_sdf_offset_all[..., 1:]  # (B*Gm, 128, 128, 128, 3)

                    gt_sdf_all = gt_sdf_offset_all[..., 0]
                    gt_offset_all = gt_sdf_offset_all[..., 1:]

                    # voxel grid (shared for all)
                    x_nx3, cube_fx8 = construct_voxel_grid(args.voxel_grid_res, args.device)
                    x_nx3 *= 2

                pred_meshes = []
                gt_meshes = []

                # loop only over mesh extraction (marching cubes is not vectorizable easily)
                for j in range(B * Gm):
                    grid_verts = x_nx3 + pred_offset_all[j].reshape(-1, 3) * (2 - 1e-8) / (args.voxel_grid_res * 2)
                    vertices, faces = dynamic_marching_cubes(grid_verts, cube_fx8, pred_sdf_all[j].reshape(-1))

                    gt_grid_verts = x_nx3 + gt_offset_all[j].reshape(-1, 3) * (2 - 1e-8) / (args.voxel_grid_res * 2)
                    gt_vertices, gt_faces = dynamic_marching_cubes(gt_grid_verts, cube_fx8, gt_sdf_all[j].reshape(-1))

                    #print(vertices.shape, faces.shape)
                    #print(gt_vertices.shape, gt_faces.shape)
                    if (vertices is not None and gt_vertices is not None and vertices.numel() > 0 and gt_vertices.numel() > 0):
                        pred_meshes.append(Meshes(verts=[vertices], faces=[faces]))
                        gt_meshes.append(Meshes(verts=[gt_vertices], faces=[gt_faces]))

                        #mesh_gt = trimesh.Trimesh(vertices=gt_vertices.detach().cpu().numpy(),faces=gt_faces.detach().cpu().numpy(), process=False)
                        #mesh_gt.show()

                        #mesh_pred = trimesh.Trimesh(vertices=vertices.detach().cpu().numpy(),faces=faces.detach().cpu().numpy(), process=False)
                        #mesh_pred.show()

                #print(pred_meshes)
                #print(gt_meshes)
                # only compute Chamfer if we have valid meshes
                if len(pred_meshes) > 0 and len(gt_meshes) > 0:
                    mesh_losses = []
                    normal_losses = []
                    volume_losses = []

                    for pm, gm in zip(pred_meshes, gt_meshes):
                        p_verts = pm.verts_list()[0].unsqueeze(0)  # (1, P, 3)
                        g_verts = gm.verts_list()[0].unsqueeze(0)  # (1, G, 3)

                        # Chamfer with mean reduction (better for average shape)
                        chamfer_loss, _ = chamfer_distance(
                            g_verts, p_verts,
                            batch_reduction="mean",
                            point_reduction="sum",  # Changed from "max"
                            single_directional=False
                        )
                        chamfer_loss = chamfer_loss/len(pred_meshes)
                        # New: Normal consistency (preserves orientation in thin areas)
                        p_normals = pm.verts_normals_list()[0].unsqueeze(0)  # (1, P, 3)
                        g_normals = gm.verts_normals_list()[0].unsqueeze(0)  # (1, G, 3)
                        # Approximate correspondences via min dist (simple but approximate)
                        dist_matrix = torch.cdist(p_verts[0], g_verts[0])  # (P, G)
                        idx = dist_matrix.argmin(dim=1)  # (P,)
                        corr_g_normals = g_normals[0][idx]
                        normal_loss = 1 - (p_normals[0] * corr_g_normals).sum(dim=1).mean().clamp(min=-1, max=1).abs()

                        # New: Volume preservation (prevents shrinkage)
                        pm_tri = trimesh.Trimesh(vertices=p_verts[0].cpu().numpy(),
                                                 faces=pm.faces_list()[0].cpu().numpy())
                        gm_tri = trimesh.Trimesh(vertices=g_verts[0].cpu().numpy(),
                                                 faces=gm.faces_list()[0].cpu().numpy())
                        volume_loss = torch.abs(torch.tensor(pm_tri.volume) - torch.tensor(gm_tri.volume)).to(
                            args.device)

                        mesh_losses.append(chamfer_loss)
                        normal_losses.append(normal_loss)
                        volume_losses.append(volume_loss)

                    if len(mesh_losses) > 0:
                        mesh_loss = torch.stack(mesh_losses).mean() * w_mesh  # Keep scaling, or tune
                        normal_loss = torch.stack(normal_losses).mean() * w_normal  # New arg, e.g., 5.0
                        volume_loss = torch.stack(volume_losses).mean() * w_volume  # New arg, e.g., 0.1
                        loss += normal_loss
                        print("mean mesh_loss:", mesh_loss.item(), "normal_loss:", normal_loss.item(), "volume_loss:", volume_loss.item())
            '''















            #print("loss_ssim:", loss.item())
            # Optimization
            current_lr = adjust_lr(optimizer, (epoch - 1) % args.n_epoch, step, len(voxel_dataset), args)
            optimizer.zero_grad()
            loss.backward()
            optimizer.step()

            if step % 30 == 0:
                print('%s epoch: %04d, step: %d/%d, current lr: %f, loss: %f' % (
                    time.strftime("%Y-%m-%d %H:%M:%S", time.localtime()),
                    epoch, step, len(data_loader), current_lr, loss.cpu().detach().numpy()
                ))
            '''
            if epoch % 50 == 0:
                print('%s epoch: %04d, step: %d/%d, current lr: %f, loss: %f, emb_loss: %f, mask_loss: %f, ssim_loss: %f' % (
                    time.strftime("%Y-%m-%d %H:%M:%S", time.localtime()),
                    epoch, step, len(data_loader), current_lr, loss.cpu().detach().numpy(), emb_loss.cpu().detach().numpy(), mask_loss.cpu().detach().numpy(), ssim_loss.cpu().detach().numpy()
                ))
            '''
        # Validation
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
                    embed_features = data_dict["embed_features"].to(args.device)  # (1, group_size, 4, 4, 4, 16)
                    #print("embed_features: ", embed_features.shape)
                    indices = data_dict["indices"]
                    #print("indices:", indices, indices.shape)
                    latent_codes = build_latent_codes_from_points(
                        indices=indices,
                        points=centers,
                        mapper=latent_mapper,
                        zero_based=True
                    )
                    latent_code_list.append(latent_codes.detach().cpu())
                    indices = indices[0].detach().cpu().numpy()
                    #print("indices:", indices, indices.shape)
                    seq_ids = data_dict["seq_id"]
                    #batch_indices = (indices[:, 0]// args.group_size)  # (1, group_size)
                    #batch_latent_codes = latent_codes[seq_ids].to(args.device)  # (1, group_size-2, latent_dim)
                    f_start, f_end = embed_features[:, 0], embed_features[:, -1]
                    start = time.time()
                    # Forward pass
                    pred_f_intermediate = net(f_start, f_end, latent_codes)  # (1, group_size-2, 4, 4, 4, 16)
                    #print("pred_f_intermediate: ", pred_f_intermediate.shape)
                    #print("pred_f_intermediate: ", pred_f_intermediate.view(-1, 4, 4, 4, 16).shape)
                    # Decode predicted features to TSDF using pre-trained auto-decoder
                    pred_f_intermediate = pred_f_intermediate.view(args.group_size - 2, args.embed_hwd, args.embed_hwd, args.embed_hwd, args.embed_dim)
                    #print("pred_f_intermediate: ", pred_f_intermediate.shape)

                    gt_f_intermediate = embed_features[:, 1:-1].contiguous().view(args.group_size - 2, args.embed_hwd, args.embed_hwd, args.embed_hwd, args.embed_dim)
                    #print("gt_f_intermediate: ", gt_f_intermediate.shape)

                    #save f_start and f_end
                    quant_f_start = diff_quantized_tensor(f_start, args.num_bits)
                    quant_f_end = diff_quantized_tensor(f_end, args.num_bits)
                    with torch.no_grad():
                        start_sdf_offset = pretrained_autocodec(embed_features=quant_f_start)  # shape (1, 128, 128, 128, 4)
                        end_sdf_offset = pretrained_autocodec(embed_features=quant_f_end)  # shape (1, 128, 128, 128, 4)

                    #print("pred_sdf_offset: ", pred_sdf_offset.shape)

                    start_sdf = start_sdf_offset[..., 0].reshape(-1)  # shape (N,)
                    start_offset = start_sdf_offset[..., 1:].reshape(-1, 3)  # shape (N, 3)

                    end_sdf = end_sdf_offset[..., 0].reshape(-1)  # shape (N,)
                    end_offset = end_sdf_offset[..., 1:].reshape(-1, 3)  # shape (N, 3)

                    start_grid_verts = x_nx3 + start_offset * (2 - 1e-8) / (args.voxel_grid_res * 2)

                    start_vertices, start_faces = dynamic_marching_cubes(start_grid_verts, cube_fx8, start_sdf)

                    end_grid_verts = x_nx3 + end_offset * (2 - 1e-8) / (args.voxel_grid_res * 2)
                    end_vertices, end_faces = dynamic_marching_cubes(end_grid_verts, cube_fx8, end_sdf)
                    end = time.time()
                    print("start and end frame reconstruction time", end - start)
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
                    #print('indices_inter:', indices_inter)
                    for i in range(args.group_size - 2):
                        inter_decoding_time = 0
                        #print(f"Processing intermediate frame {i + 2}")
                        start = time.time()
                        f_i = pred_f_intermediate[i].unsqueeze(0)  # shape (1, 4, 4, 4, 16)
                        quant_f_i = diff_quantized_tensor(f_i, args.num_bits)
                        end = time.time()
                        inter_decoding_time += end-start
                        gt_f_i = gt_f_intermediate[i].unsqueeze(0)
                        quant_gt_f_i = diff_quantized_tensor(gt_f_i, args.num_bits)
                        with torch.no_grad():
                            start = time.time()
                            pred_sdf_offset = pretrained_autocodec(embed_features=quant_f_i)  # shape (1, 128, 128, 128, 4)
                            end = time.time()
                            inter_decoding_time += end-start
                            gt_sdf_offset = pretrained_autocodec(embed_features=quant_gt_f_i)  # shape (1, 128, 128, 128, 4)

                        #print("pred_sdf_offset: ", pred_sdf_offset.shape)
                        start = time.time()
                        pred_sdf = pred_sdf_offset[..., 0].reshape(-1)  # shape (N,)
                        pred_offset = pred_sdf_offset[..., 1:].reshape(-1, 3)  # shape (N, 3)

                        grid_verts = x_nx3 + pred_offset * (2 - 1e-8) / (args.voxel_grid_res * 2)
                        vertices, faces = dynamic_marching_cubes(grid_verts, cube_fx8, pred_sdf)

                        end = time.time()
                        inter_decoding_time += end-start
                        decoding_time_network.append(inter_decoding_time)

                        gt_sdf = gt_sdf_offset[..., 0].reshape(-1)  # shape (N,)
                        gt_offset = gt_sdf_offset[..., 1:].reshape(-1, 3)  # shape (N, 3)

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
