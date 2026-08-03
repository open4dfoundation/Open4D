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
from network import get_network,adjust_lr,diff_quantized_tensor, InterpolationTransformerCrossAttnV6
from dataset import get_dataset

from fmc import dynamic_marching_cubes, construct_voxel_grid, base_cube_edges
from util import Mesh, SSIM3D
import imageio
import trimesh
import time



# First model config
args_encoder = get_config().parse_args([
    '--config_path', 'configs/configs_128.txt'
])

net_codec = get_network(args_encoder.model, args_encoder).to(args_encoder.device)

encoder_ckpt_path = '/mnt/datadrive/ChromeDownloads/Mesh_dataset/combined_scaled/TSDF_128/log/48_36_24_16_12_2025_10_20_18_20_16/checkpoint_0600/encoder_compressed.pt'
decoder_ckpt_path = '/mnt/datadrive/ChromeDownloads/Mesh_dataset/combined_scaled/TSDF_128/log/48_36_24_16_12_2025_10_20_18_20_16/checkpoint_0600/decoder_compressed.pt'

encoder_state_dict = torch.load(encoder_ckpt_path)
net_codec.encoder.load_state_dict(encoder_state_dict)

decoder_quanted_state_dict = torch.load(decoder_ckpt_path)
net_codec.decoder.load_state_dict(decoder_quanted_state_dict)

net_codec.eval()


# Second model config
args_transformer = get_config().parse_args([
    '--config_path', 'configs/configs_interpolation_128.txt'
])

'''
net_transformer = InterpolationTransformerCrossAttnV6(
    voxel_feat_dim=args_transformer.embed_dim,
    in_feat_dim=args_transformer.embed_dim,
    latent_dim=args_transformer.latent_dim,
    group_size=args_transformer.group_size,
    voxel_res=(args_transformer.embed_hwd, args_transformer.embed_hwd, args_transformer.embed_hwd)
).to(args_transformer.device)
'''

net_transformer_compressed = InterpolationTransformerCrossAttnV6(
    voxel_feat_dim=args_transformer.embed_dim,
    in_feat_dim=args_transformer.embed_dim,
    latent_dim=args_transformer.latent_dim,
    group_size=args_transformer.group_size,
    voxel_res=(args_transformer.embed_hwd, args_transformer.embed_hwd, args_transformer.embed_hwd)
).to(args_transformer.device)


'''
transformer_path = '/media/frozzzen/DataDrive/ChromeDownloads/Dancer_dataset/C4/TSDF/log/dancer_128_100_128_4_v7_transformer_32/checkpoint_/transformer.pt'
#transformer_path = '/home/frozzzen/Documents/Github/Implicit-mesh-compression/transformer_compressed.pt'
transformer_dict = torch.load(transformer_path)
net_transformer.load_state_dict(transformer_dict)


#net_transformer.eval()

net_transformer.save_quanted_weights_lossless("transformer_compressed.pt")

#weights = torch.load("transformer_compressed.pt")
#torch.save(weights, "transformer_compressed_zip.pt", _use_new_zipfile_serialization=True)
'''

net_transformer_compressed.load_quanted_weights_lossless("/mnt/datadrive/ChromeDownloads/Mesh_dataset/combined_scaled/TSDF_128/log/interpolation_2025_10_26_14_22_23/checkpoint_1000/transformer_compressed.pt")
net_transformer_compressed.eval()


device = next(net_transformer_compressed.parameters()).device

embed_np_1 = np.load('/mnt/datadrive/ChromeDownloads/Mesh_dataset/combined_scaled/TSDF_128/log/48_36_24_16_12_2025_10_20_18_20_16/checkpoint_0600/embed_features/embed_feature_0000.npy')
print(embed_np_1.shape)
embed_np_5 = np.load('/mnt/datadrive/ChromeDownloads/Mesh_dataset/combined_scaled/TSDF_128/log/48_36_24_16_12_2025_10_20_18_20_16/checkpoint_0600/embed_features/embed_feature_0004.npy')

latent_code = torch.load('/mnt/datadrive/ChromeDownloads/Mesh_dataset/combined_scaled/TSDF_128/log/interpolation_2025_10_26_14_22_23/checkpoint_1000/latent_codes.pt')
print(latent_code.shape)
latent_code = latent_code.to(args_transformer.device)
print("latent code: ", latent_code[0].unsqueeze(0).shape)
# === 7. Compare ===
def mse(t1, t2):
    return torch.mean((t1 - t2) ** 2).item()

f_start = torch.from_numpy(embed_np_1).float().to(device)
f_end = torch.from_numpy(embed_np_5).float().to(device)
print(f_start.shape)
print(f_end.shape)
with torch.no_grad():
    _ = net_transformer_compressed(f_start, f_end, latent_code[0].unsqueeze(0))
    _ = net_codec(embed_features=f_start)
torch.cuda.synchronize()

torch.cuda.synchronize()
start = time.time()
pre_dict_embed = net_transformer_compressed(f_start, f_end, latent_code[0].unsqueeze(0))
torch.cuda.synchronize()
end = time.time()
transformer_time = end - start
print("transformer_time", transformer_time)
#print("pre_dict_embed: ", pre_dict_embed.shape)

torch.cuda.synchronize()
start = time.time()
quant_pred_f_intermediate = diff_quantized_tensor(pre_dict_embed, 8)
x_nx3, cube_fx8 = construct_voxel_grid(args_transformer.voxel_grid_res, args_transformer.device)
x_nx3 *= 2
torch.cuda.synchronize()
end = time.time()
voxel_grid_time = end - start
print("voxel_grid_time", voxel_grid_time)

decoding_time = 0
necgs_time = 0

for i in range(args_transformer.group_size - 2):
    # print(f"Processing intermediate frame {i + 2}")
    print(i)
    #print(f_i.shape)
    embed_np_i = np.load(f'/mnt/datadrive/ChromeDownloads/Mesh_dataset/combined_scaled/TSDF_128/log/48_36_24_16_12_2025_10_20_18_20_16/checkpoint_0600/embed_features/embed_feature_0{i+1:03}.npy')
    embed_np_i = torch.from_numpy(embed_np_i).float().to(device)
    #print(mse(embed_np_i, f_i))
    torch.cuda.synchronize()
    start = time.time()
    f_i = quant_pred_f_intermediate.squeeze(0)[i].unsqueeze(0)  # shape (1, 4, 4, 4, 16)
    recon_voxel_from_embed = net_codec(embed_features=f_i)
    torch.cuda.synchronize()
    end = time.time()
    print("decoder time", end - start)
    decoding_time += end - start

    torch.cuda.synchronize()
    start = time.time()
    recon_voxel_from_embed_gt = net_codec(embed_features=embed_np_i)
    print(mse(recon_voxel_from_embed, recon_voxel_from_embed_gt))
    torch.cuda.synchronize()
    end = time.time()
    print("decoder time necgs", end - start)
    necgs_time += end - start

    npz_data = np.load(f'/mnt/datadrive/ChromeDownloads/Mesh_dataset/combined_scaled/TSDF_128/data/0{i+1:03}.npz')
    input_voxel_np = npz_data['sdf']  # shape [D, H, W, 4] or possibly [4, D, H, W]
    offset_grids = npz_data['offset']
    input_voxel_np = np.concatenate([input_voxel_np, offset_grids], axis=-1).astype(np.float32)
    input_voxel = torch.from_numpy(input_voxel_np).unsqueeze(0).float().to(device)
    print(mse(recon_voxel_from_embed, input_voxel))

    torch.cuda.synchronize()
    start = time.time()
    pred_sdf = recon_voxel_from_embed[..., 0].reshape(-1)  # shape (N,)
    pred_offset = recon_voxel_from_embed[..., 1:].reshape(-1, 3)  # shape (N, 3)
    grid_verts = x_nx3 + pred_offset * (2 - 1e-8) / (args_transformer.voxel_grid_res * 2)
    vertices, faces = dynamic_marching_cubes(grid_verts, cube_fx8, pred_sdf)
    torch.cuda.synchronize()
    end = time.time()
    print("marching cubes time", end - start)
    decoding_time += end - start

    torch.cuda.synchronize()
    start = time.time()
    gt_sdf = recon_voxel_from_embed_gt[..., 0].reshape(-1)
    gt_offset = recon_voxel_from_embed_gt[..., 1:].reshape(-1, 3)  # shape (N, 3)
    grid_verts_gt = x_nx3 + gt_offset * (2 - 1e-8) / (args_transformer.voxel_grid_res * 2)
    vertices_gt, faces_gt = dynamic_marching_cubes(grid_verts_gt, cube_fx8, gt_sdf)
    torch.cuda.synchronize()
    end = time.time()
    print("marching cubes time necgs", end - start)
    necgs_time += end - start





    mesh_np = trimesh.Trimesh(vertices=vertices.detach().cpu().numpy(),
                                  faces=faces.detach().cpu().numpy(), process=False)
    mesh_np.export(os.path.join(f"./mesh_pre_{i+1:03}.obj"))

    mesh_gt = trimesh.Trimesh(vertices=vertices_gt.detach().cpu().numpy(),
                              faces=faces_gt.detach().cpu().numpy(), process=False)
    mesh_gt.export(os.path.join(f"./mesh_gt_{i+1:03}.obj"))

print(decoding_time/(args_transformer.group_size - 2))
print(necgs_time/(args_transformer.group_size - 2))