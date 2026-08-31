import sys
import os
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import torch
import shutil
import numpy as np

from codec import split_volume,zero_pads,deform_warp,grid_sampler
from codec import encode_motion,decode_motion,quant_motion,get_masks,get_pca,\
    encode_pca,project_pca,decode_pca,unproject_pca,encode_entropy_motion_npy
import time
from bitarray import bitarray
import json
import argparse
import copy

if torch.cuda.is_available():
    torch.set_default_tensor_type('torch.cuda.FloatTensor')
    device = torch.device('cuda')
else:
    device = torch.device('cpu')

def list_type(string):
    my_list=string.split(',')
    return [int(x) for x in my_list]

def config_parser():
    '''Define command line arguments
    '''

    parser = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('--model_path', required=True, help='trained model path')
    parser.add_argument("--expr_name", type=str, help="experiment name, the folder name to save the result")
    parser.add_argument("--quality", type=list_type, default="99",
                        help='Quality of compression')
    parser.add_argument("--group_size", type=int, default=-1,
                       help='key frame cover how many frames')
    parser.add_argument("--frame_num", type=int, default=20000,
                        help='frame_num')
    parser.add_argument("--start_frame", type=int, default=0,
                        help='choose which frame to start compression, if the program is broken accidentally')

    parser.add_argument("--pca", action='store_true', help='Whether to use pca to compress')
    parser.add_argument('--pca_chs', type=list_type, help='Determine the channels to be used for each component of PCA', default='7,13')

    return parser

parser = config_parser()
args = parser.parse_args()
path = args.model_path

group_size=args.group_size if args.group_size !=-1 else args.frame_num

pca=args.pca
pca_chs=args.pca_chs
expr_name=args.expr_name
qualitys=args.quality
voxel_size = 8
for quality in qualitys:
    os.makedirs(os.path.join(path,expr_name),exist_ok=True)
    shutil.copy(os.path.join(path, 'rgb_net.tar'),os.path.join(path,expr_name,'rgb_net.tar'))
    frame_length=args.frame_num

    masks=[]

    for frame_id in range(args.start_frame,args.start_frame+frame_length):
        key_frame=(frame_id%group_size==0)
        print('process frame', frame_id, f" keyframe {key_frame}")

        ckpt_path = os.path.join(path, 'fine_last_%d.tar' % frame_id)

        while not os.path.exists(ckpt_path):
            torch.cuda.empty_cache()
            print("waiting checkpoint ", ckpt_path)
            time.sleep(1000)

        ckpt = torch.load(ckpt_path, map_location=device)
        model_states = ckpt['model_state_dict']

        if not os.path.exists( os.path.join(path, 'fine_last_%d_deform.tar' % frame_id)) :
            print("no deforming information as canonical frame")
            with open(os.path.join(path, expr_name, f'model_kwargs.json'), 'w') as header_f:
                model_kwargs=copy.deepcopy(ckpt['model_kwargs'])
                model_kwargs['xyz_min'] = model_kwargs['xyz_min'].tolist()
                model_kwargs['xyz_max'] = model_kwargs['xyz_max'].tolist()
                model_kwargs['voxel_size_ratio'] = model_kwargs['voxel_size_ratio'].tolist()
                model_kwargs['use_res'] = False
                model_kwargs['mask_cache_path']=""
                json.dump(model_kwargs, header_f, indent=4)

            density_act =-4.1
            residual_k0=k0= model_states['k0.k0']
            residual_density=density=model_states['density']-density_act
            masks=[torch.zeros_like(residual_density,device=residual_k0.device)+density_act,model_states['density']]

            model_states_0=model_states
        else:

            # for deformation field
            ckpt_deform_path = os.path.join(path, 'fine_last_%d_deform.tar' % frame_id)
            ckpt_deform = torch.load(ckpt_deform_path, map_location=device)
            model_states_deform = ckpt_deform['model_state_dict']
            deformation_field = model_states_deform['deformation_field']

            deform_cube, grid_size, origin_size= encode_motion(deformation_field)

            deformation_field = decode_motion(deform_cube, grid_size, origin_size)

            deformation_field=quant_motion(deformation_field)

            xyz_min=model_states['xyz_min']
            xyz_max=model_states['xyz_max']

            self_grid_xyz = torch.stack(torch.meshgrid(
                torch.linspace(xyz_min[0], xyz_max[0], deformation_field.shape[2]),
                torch.linspace(xyz_min[1], xyz_max[1], deformation_field.shape[3]),
                torch.linspace(xyz_min[2], xyz_max[2], deformation_field.shape[4]),
            ), -1)
            deform_xyz = deform_warp(self_grid_xyz, deformation_field,xyz_min,xyz_max)
            former_rec = grid_sampler(deform_xyz,xyz_min,xyz_max, former_rec).permute(3, 0, 1, 2).unsqueeze(0)
            if 'k0.former_k0' in model_states:
                residual_k0=model_states['k0.k0'] + model_states['k0.former_k0']-former_rec[:,1:]
                k0=model_states['k0.k0'] + model_states['k0.former_k0']
            else:
                if not key_frame:
                    print("Not key frame, but has not former feature. Something wrong at the training?")
                residual_k0 = model_states['k0.k0']  - former_rec[:, 1:]
                k0 = model_states['k0.k0']

            residual_density=model_states['density']-former_rec[:,:1]
            density=model_states['density']-density_act
            masks.append(model_states['density'])

        if os.path.exists(
                os.path.join(path, 'fine_last_%d_deform.tar' % frame_id)) and not key_frame:
            deform_save, deform_mask = encode_entropy_motion_npy(deform_cube)
            np.save(os.path.join(path, expr_name, 'deform_%d.npy' % frame_id), deform_save)
            masks_bit = bitarray()
            masks_bit.pack(deform_mask)
            with open(os.path.join(path, expr_name, f'deform_mask_{frame_id}.rerf'), 'wb') as masked_file:
                masks_bit.tofile(masked_file)

        if key_frame:
            residual_full = torch.cat([density, k0], dim=1)
        else:
            residual_full = torch.cat([residual_density, residual_k0], dim=1)
        masks = torch.cat(masks, dim=1)

        residual, grid_size = split_volume(zero_pads(residual_full, voxel_size=voxel_size),
                                           voxel_size=voxel_size)
        residual = residual.cuda()
        masks_origin = masks
        masks = get_masks(masks, voxel_size)

        headers={}
        headers["headers"]=[]
        if not key_frame and pca:
            V, pca_V = get_pca(residual, masks, voxel_size)

            np.save(os.path.join(path, expr_name, 'pca_m_%d.npy' % frame_id), pca_V)

            residual_pca = project_pca(residual_full, V)

            headers["headers"].append(encode_pca(residual_pca[:, :pca_chs[0]], masks, quality, path, expr_name, frame_id,voxel_size))
            masks_reso = get_masks(masks_origin, voxel_size)
            headers["headers"].append(encode_pca(residual_pca[:, pca_chs[0]:pca_chs[1]], masks_reso, quality - 1, path, expr_name, frame_id,
                       voxel_size))

        else:
            headers["headers"].append(encode_pca(residual_full, masks, quality, path, expr_name, frame_id, voxel_size))

        with open(os.path.join(path, expr_name, f'header_{frame_id}.json'), 'w') as header_f:
            json.dump(headers, header_f, indent=4)

        #mask save once
        masks_save = masks.bool().clone().cpu().numpy()
        masks_bit = bitarray()
        masks_bit.pack(masks_save)
        with open(os.path.join(path, expr_name, f'mask_{frame_id}.rerf'), 'wb') as masked_file:
            masks_bit.tofile(masked_file)

        if key_frame or not pca:
            rec_feature = decode_pca(path, expr_name, frame_id, quality, voxel_size, device)
        else:
            rec_feature = []
            rec_feature.append(decode_pca(path, expr_name, frame_id, quality,  voxel_size, device))
            rec_feature.append(decode_pca(path, expr_name, frame_id, quality - 1,  voxel_size, device))
            rec_feature = unproject_pca(rec_feature, path, expr_name, frame_id, device)[0]

        if key_frame or not os.path.exists(os.path.join(path, 'fine_last_%d_deform.tar' % frame_id)):

            density_rec = (rec_feature[:1] - 4.1).unsqueeze(0)
            feature_rec = rec_feature[1:].unsqueeze(0)
            former_rec = torch.cat([density_rec, feature_rec], dim=1)

        else:
            former_rec = rec_feature + former_rec
            density_rec = former_rec[:, :1]
            feature_rec = former_rec[:, 1:]

        masks = [density_rec]

