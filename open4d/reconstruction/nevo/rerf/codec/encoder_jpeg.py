import torch
import numpy as np
from codec import dct_3d,idct_3d,quantize_quality,gen_3d_quant_tbl,Timer,merge_volume,zero_unpads
from codec import split_volume,zero_pads,get_origin_size
import torch.nn.functional as F
from ac_dc.ncvv_ac_dc import ac_dc_encode, ac_dc_decode, ac_dc_encode2, ac_dc_decode2

QTY_3d =gen_3d_quant_tbl()


def encode_jpeg_huffman(res,quality,path):

    RES = dct_3d(res, norm='ortho')

    quant_table = quantize_quality(QTY_3d, quality)
    RES_quant = RES / quant_table
    data = RES_quant.cpu().numpy()
    data = np.rint(data).astype(np.int16)
    #dc
    data_size=data.shape
    ac_dc_encode2(data, 0, data_size[0], data_size[1], path)
    header={
        'size': data_size,
        'quality': quality,
        # Remaining bits length is the fake filled bits for 8 bits as a
        # byte.
    }
    return header


def decode_jpeg_huffman(file_object,header,device):
    data_size = header['size']
    quality = header['quality']

    # Preprocessing Byte Sequence:
    #   1. Remove Remaining (Fake Filled) Bits.
    #   2. Slice Bits into Dictionary Data Structure

    rec=np.zeros(data_size, dtype=np.int16)
    ac_dc_decode2(rec, 0, data_size[0], data_size[1], file_object)

    rec = torch.tensor(rec, device=device).to(torch.float)
    quant_table = quantize_quality(QTY_3d, quality)
    rec=rec*quant_table

    rec = idct_3d(rec, norm='ortho')
    return rec


@torch.no_grad()
def recover_misc(residual_rec_dct,former_rec,header, mask,n_channel=13,voxel_size=8,device='cuda'):
    residual_rec=torch.zeros((mask.size(0),n_channel,voxel_size,voxel_size,voxel_size),device=device)
    residual_rec[mask] = residual_rec_dct

    residual_rec = residual_rec.reshape( mask.size(0), n_channel, -1)
    rec_feature = former_rec + residual_rec

    # recover
    rec_feature = rec_feature.reshape( mask.size(0), n_channel, voxel_size, voxel_size,
                                      voxel_size)
    grid_size=header["grid_size"]
    origin_size=header["origin_size"][-3:]
    rec_feature = merge_volume(rec_feature, grid_size)
    rec_feature = zero_unpads(rec_feature, origin_size).unsqueeze(0)

    return rec_feature


@torch.no_grad()
def deform_warp(xyz,deformation_field,xyz_min,xyz_max, align_corners=True):
    '''

    :param xyz: [N,3]
    :param align_corners:
    :return:
    '''
    mode = 'bilinear'

    shape = xyz.shape[:-1]

    xyz_r = xyz.reshape(1, 1, 1, -1, 3)
    ind_norm = ((xyz_r - xyz_min) / (xyz_max - xyz_min)).flip((-1,)) * 2 - 1

    deform_vector = F.grid_sample(deformation_field, ind_norm, mode=mode, align_corners=align_corners). \
        reshape(deformation_field.shape[1], -1).T.reshape(*shape, deformation_field.shape[1])

    xyz = xyz + deform_vector

    return xyz

@torch.no_grad()
def grid_sampler( xyz,xyz_min,xyz_max, *grids,mode=None, align_corners=True):
    '''Wrapper for the interp operation'''
    mode = 'bilinear'
    shape = xyz.shape[:-1]

    xyz = xyz.reshape(1, 1, 1, -1, 3)
    ind_norm = ((xyz - xyz_min) / (xyz_max - xyz_min)).flip((-1,)) * 2 - 1

    ret_lst = [
        F.grid_sample(grid, ind_norm, mode=mode, align_corners=align_corners).reshape(grid.shape[1], -1).T.reshape(
            *shape, grid.shape[1])
        for grid in grids
    ]
    for i in range(len(grids)):
        if ret_lst[i].shape[-1] == 1:
            ret_lst[i] = ret_lst[i].squeeze(-1)
    if len(ret_lst) == 1:
        return ret_lst[0]
    return ret_lst

@torch.no_grad()
def encode_motion(deformation_field,voxel_size=8):
    #mode cube
    origin_size = get_origin_size(deformation_field)
    deform, grid_size = split_volume(zero_pads(deformation_field, voxel_size=voxel_size),
                                      voxel_size=voxel_size)

    deform=deform.reshape(deform.size(0),deform.size(1),-1)
    deform=torch.mean(deform,dim=-1)

    return deform, grid_size,origin_size

@torch.no_grad()
def decode_motion(deform,grid_size,origin_size,voxel_size=8):
    deform=deform.unsqueeze(-1)
    deform=deform.repeat(1,1,voxel_size**3)
    deform=deform.reshape(deform.size(0),deform.size(1),voxel_size,voxel_size,voxel_size)
    deform=merge_volume(deform,grid_size)
    deform=zero_unpads(deform,origin_size).unsqueeze(0)
    return deform

@torch.no_grad()
def recover_misc_deform(residual_rec_dct,former_rec,header, mask,deform,model_states, n_channel=13,voxel_size=8,device='cuda'):
    grid_size = header["grid_size"]
    origin_size = header["origin_size"][-3:]
    deform_rec = deform
    deformation_field = decode_motion(deform_rec, grid_size, origin_size)

    xyz_min = torch.Tensor(model_states['xyz_min'],device=device)
    xyz_max = torch.Tensor(model_states['xyz_max'],device=device)

    self_grid_xyz = torch.stack(torch.meshgrid(
        torch.linspace(xyz_min[0], xyz_max[0], deformation_field.shape[2]),
        torch.linspace(xyz_min[1], xyz_max[1], deformation_field.shape[3]),
        torch.linspace(xyz_min[2], xyz_max[2], deformation_field.shape[4]),
    ), -1)
    deform_xyz = deform_warp(self_grid_xyz, deformation_field, xyz_min, xyz_max)
    former_rec = grid_sampler(deform_xyz, xyz_min, xyz_max, former_rec).permute(3, 0, 1, 2).unsqueeze(0)
    residual_rec = torch.zeros((mask.size(0), n_channel, voxel_size, voxel_size, voxel_size), device=device)
    residual_rec[mask] = residual_rec_dct

    residual_rec = residual_rec.reshape(mask.size(0), n_channel, -1)
    # recover
    rec_feature = residual_rec.reshape(mask.size(0), n_channel, voxel_size, voxel_size,
                                      voxel_size)
    rec_feature = merge_volume(rec_feature, grid_size)
    rec_feature = zero_unpads(rec_feature, origin_size).unsqueeze(0)

    rec_feature=former_rec+rec_feature

    return rec_feature
