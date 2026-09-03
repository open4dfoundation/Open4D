import torch
import os
import numpy as np

from codec import split_volume,zero_pads,zero_unpads,get_origin_size,merge_volume,encode_jpeg_huffman,decode_jpeg_huffman

from bitarray import bitarray
import json


def get_masks(masks,voxel_size):

    masks, grid_size_mask = split_volume(zero_pads(masks, voxel_size=voxel_size),
                                         voxel_size=voxel_size)
    masks = masks.cuda()

    masks = masks.reshape(masks.size(0), 2, -1)
    masks = torch.nn.functional.softplus(masks - 4.1) > 0.4
    masks = masks.sum(dim=-1)

    masks = (masks[:, 0] + masks[:, 1]).bool()

    return masks

def get_pca(residual,masks,voxel_size):
    residual_cur = residual[masks]
    # start pca
    tmp = residual_cur.reshape(residual_cur.size(0), residual_cur.size(1), -1)
    tmp = tmp.permute((0, 2, 1))  #
    tmp = tmp.reshape(-1, tmp.size(-1))[:, 1:]  # [N,12]
    _, _, V = torch.pca_lowrank(tmp, q=tmp.size(-1))

    pca_V = V.transpose(0, 1).cpu().numpy()

    return V,pca_V

def project_pca(residual_full,V):
    residual_pca=residual_full[0,1:]
    residual_pca=residual_pca.reshape(residual_pca.size(0),-1).transpose(0,1)

    residual_pca=residual_pca@V
    residual_pca=residual_pca.transpose(0,1).reshape(1,residual_pca.size(1),
                                 residual_full.size(2),residual_full.size(3),residual_full.size(4))
    return torch.cat([residual_full[:,:1],residual_pca],dim=1)


def encode_pca(residual,masks_reso,quality,path,expr_name,frame_id,voxel_size):

    residual_cur, grid_size = split_volume(zero_pads(residual, voxel_size=voxel_size),
                                       voxel_size=voxel_size)

    residual_cur=residual_cur[masks_reso]

    header = encode_jpeg_huffman(residual_cur, quality, os.path.join(path, expr_name, f'feature_{frame_id}_{quality}.rerf'))

    origin_size = get_origin_size(residual,True)
    header["origin_size"] = origin_size
    header["grid_size"] = grid_size
    header["mask_size"] = masks_reso.size(0)

    return  header



def decode_pca(path,expr_name,frame_id,quality,voxel_size,device):

    jsonfile = os.path.join(path, expr_name, f'header_{frame_id}.json')
    with open(jsonfile) as f:
        header = json.load(f)
        header=header["headers"]
        if header[0]['quality']==quality:
            header=header[0]
        else:
            header=header[1]

    origin_size=header["origin_size"][1:]
    grid_size=header["grid_size"]
    n_channel=header["origin_size"][0]


    with open(os.path.join(path, expr_name, f'mask_{frame_id}.rerf'), 'rb') as masked_file:
        mask_bits = bitarray()
        mask_bits.fromfile(masked_file)
        masks_reso = torch.from_numpy(np.unpackbits(mask_bits)[:header['mask_size']].reshape(header['mask_size']).astype(np.bool)).to(device)

    residual_rec_dct = decode_jpeg_huffman(
        os.path.join(path, expr_name, f'feature_{frame_id}_{quality}.rerf'), header, device=device)

    residual_rec = torch.zeros((masks_reso.size(0), n_channel, voxel_size, voxel_size, voxel_size), device=device)
    residual_rec[masks_reso] = residual_rec_dct

    residual_rec = residual_rec.reshape(masks_reso.size(0),n_channel , -1)

    # recover
    rec_feature = residual_rec.reshape(masks_reso.size(0),n_channel , voxel_size, voxel_size, voxel_size)

    rec_feature = merge_volume(rec_feature, grid_size)

    rec_feature = zero_unpads(rec_feature, origin_size)

    return rec_feature


def unproject_pca(rec_feature,path,expr_name,frame_id,device):
    rec_feature=torch.cat(rec_feature,dim=0)[None,...]
    pca_V= np.load(os.path.join(path, expr_name, 'pca_m_%d.npy' % frame_id))
    pca_V=torch.from_numpy(pca_V).to(device)

    rec_feature = project_pca(rec_feature, pca_V)
    return rec_feature

def unproject_pca_mmap(rec_feature,path,frame_id,device,voxel_size=8):

    rec_feature=torch.cat(rec_feature,dim=1)
    rec_feature=rec_feature.reshape(rec_feature.size(0),rec_feature.size(1),-1,1)
    rec_feature=rec_feature.permute(1,0,2,3).unsqueeze(0)
    pca_V= np.load(os.path.join(path, 'pca_m_%d.npy' % frame_id))
    pca_V=torch.from_numpy(pca_V).to(device)
    rec_feature=project_pca(rec_feature,pca_V)
    rec_feature=rec_feature.squeeze()
    rec_feature=rec_feature.permute(1,0,2)
    rec_feature=rec_feature.reshape(rec_feature.size(0),rec_feature.size(1),voxel_size,voxel_size,voxel_size)
    return rec_feature



