import argparse
import json
import os
from bitarray import bitarray
from codec import  recover_misc,recover_misc_deform,\
    decode_jpeg_huffman,decode_entropy_motion_npy,unproject_pca_mmap
import torch

from tqdm import tqdm, trange
import cv2
import mmcv
import imageio
from PIL import Image
import numpy as np
import math
from lib import utils, dvgo, dvgo_video
from run import seed_everything,load_everything_frame

import torch.nn.functional as F

def rodrigues_rotation_matrix(axis, theta):
    axis = np.asarray(axis)
    theta = np.asarray(theta)
    axis = axis / math.sqrt(np.dot(axis, axis))
    a = math.cos(theta / 2.0)
    b, c, d = -axis * math.sin(theta / 2.0)
    aa, bb, cc, dd = a * a, b * b, c * c, d * d
    bc, ad, ac, ab, bd, cd = b * c, a * d, a * c, a * b, b * d, c * d
    return np.array([[aa + bb - cc - dd, 2 * (bc + ad), 2 * (bd - ac)],
                     [2 * (bc - ad), aa + cc - bb - dd, 2 * (cd + ab)],
                     [2 * (bd + ac), 2 * (cd - ab), aa + dd - bb - cc]])

def list_type(string):
    my_list=string.split(',')
    return [int(x) for x in my_list]


@torch.no_grad()
def render_viewpoints_frames(model, cfg,render_poses, HW, Ks, frame_ids,ndc, render_kwargs,
                      gt_imgs=None, savedir=None, render_factor=0,
                      eval_ssim=False, eval_lpips_alex=False, eval_lpips_vgg=False, model_callback=None):
    '''Render images for the given viewpoints; run evaluation if gt given.
    '''
    assert len(render_poses) == len(HW) and len(HW) == len(Ks)

    if render_factor != 0:
        HW = np.copy(HW)
        Ks = np.copy(Ks)
        HW //= render_factor
        Ks[:, :2, :3] //= render_factor

    rgbs = []
    depths = []
    psnrs = []
    ssims = []
    lpips_alex = []
    lpips_vgg = []

    if model_callback is None:
        model_callback = lambda x, y, z: (x, y)


    for i, c2w in enumerate(tqdm(render_poses)):

        model, render_kwargs = model_callback(model, render_kwargs, frame_ids[i])

        H, W = HW[i]
        K = Ks[i]
        rays_o, rays_d, viewdirs = dvgo.get_rays_of_a_view(
            H, W, K, c2w, ndc, inverse_y=render_kwargs['inverse_y'],
            flip_x=cfg.data.flip_x, flip_y=cfg.data.flip_y)
        keys = ['rgb_marched', 'depth', 'rgb_marched_raw']
        rays_o = rays_o.flatten(0, -2).cuda()
        rays_d = rays_d.flatten(0, -2).cuda()
        viewdirs = viewdirs.flatten(0, -2).cuda()

        render_result_chunks = [
            {k: v for k, v in model(ro, rd, vd, **render_kwargs).items() if k in keys}
            for ro, rd, vd in zip(rays_o.split(8192*64, 0), rays_d.split(8192*64, 0), viewdirs.split(8192*64, 0))
        ]

        render_result = {
            k: torch.cat([ret[k] for ret in render_result_chunks]).reshape(H, W, -1)
            for k in render_result_chunks[0].keys()
        }
        rgb = render_result['rgb_marched'].cpu().numpy()
        depth = render_result['depth'].cpu().numpy()

        rgbs.append(rgb)
        depths.append(depth)

        if savedir is not None:
            print(f'Writing images to {savedir}')

            rgb8 = utils.to8b(rgb)
            filename = os.path.join(savedir, '{:03d}.jpg'.format(i))
            imageio.imwrite(filename, rgb8)
            depth8 = utils.to8b(1 - depth / np.max(depth))
            filename = os.path.join(savedir, '{:03d}_depth.jpg'.format(i))
            imageio.imwrite(filename, depth8)

        if gt_imgs is not None and render_factor == 0:
            p = -10. * np.log10(np.mean(np.square(rgb - gt_imgs[i])))
            psnrs.append(p)
            if eval_ssim:
                ssims.append(utils.rgb_ssim(rgb, gt_imgs[i], max_val=1))
            if eval_lpips_alex:
                lpips_alex.append(utils.rgb_lpips(rgb, gt_imgs[i], net_name='alex', device=c2w.device))
            if eval_lpips_vgg:
                lpips_vgg.append(utils.rgb_lpips(rgb, gt_imgs[i], net_name='vgg', device=c2w.device))

    if len(psnrs):
        print('Testing psnr', np.mean(psnrs), '(avg)')
        if eval_ssim: print('Testing ssim', np.mean(ssims), '(avg)')
        if eval_lpips_vgg: print('Testing lpips (vgg)', np.mean(lpips_vgg), '(avg)')
        if eval_lpips_alex: print('Testing lpips (alex)', np.mean(lpips_alex), '(avg)')

    rgbs = np.array(rgbs)
    depths = np.array(depths)

    return rgbs, depths


def config_parser():
    '''Define command line arguments
    '''

    parser = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('--config', required=True,
                        help='config file path')
    parser.add_argument("--seed", type=int, default=777,
                        help='Random seed')

    parser.add_argument("--compression_path", type=str, default='', help='path to the folder stored compressed files')
    parser.add_argument("--pca", action='store_true', help='Whether to use pca to compress')
    parser.add_argument('--pca_chs', type=list_type, help='Determine the channels to be used for each component of PCA',
                        default='7,13')

    parser.add_argument("--render_start_frame", type=int, default=0, help='start frame')
    parser.add_argument("--render_360", type=int, default=-1, help='total num of frames to render')

    parser.add_argument("--group_size", type=int, default=-1,
                       help='key frame cover how many frames')
    parser.add_argument("--frame_num", type=int, default=20000,
                        help='frame_num')

    parser.add_argument('--fp16', action='store_true')

    return parser


parser = config_parser()
args = parser.parse_args()
cfg = mmcv.Config.fromfile(args.config)

n_channel=cfg.fine_model_and_render.rgbnet_dim+1
voxel_size=cfg.voxel_size


# init enviroment
if torch.cuda.is_available():
    torch.set_default_tensor_type('torch.cuda.FloatTensor')
    device = torch.device('cuda')
else:
    device = torch.device('cpu')
seed_everything(args)

file_path=args.compression_path
os.makedirs(os.path.join(cfg.basedir, cfg.expname), exist_ok=True)

frame_id = args.render_start_frame
data_dict = load_everything_frame(args=args, cfg=cfg, frame_id=frame_id, only_current=True)

model = dvgo_video.DirectVoxGO_Video()
model.current_frame_id = frame_id
if os.path.exists( os.path.join(args.compression_path, f'rgb_net.tar')):
    last_ckpt_path = os.path.join(args.compression_path, f'rgb_net.tar')
else:
    last_ckpt_path = os.path.join(cfg.basedir, cfg.expname, f'rgb_net.tar')

ckpt = torch.load(last_ckpt_path)
model.load_rgb_net_mmap(cfg,ckpt)

jsonfile = os.path.join(file_path, f'model_kwargs.json')
with open(jsonfile) as f:
    model_kwargs = json.load(f)

model_kwargs['rgbnet'] = model.rgbnet
dvgo_model=dvgo.DirectVoxGO(**model_kwargs)

pca=args.pca
pca_chs=args.pca_chs
group_size=args.group_size if args.group_size !=-1 else args.frame_num

def mmap_decode():
    frame_count = 0
    frame_id = args.render_start_frame
    while(True):
        key_frame = (frame_id % group_size == 0)
        jsonfile=os.path.join(file_path,f'header_{frame_id}.json')
        with open(jsonfile) as f:
            headers = json.load(f)
            header = headers["headers"][0]
            mask_size = header['mask_size']
            if mask_size % 8 != 0:
                mask_size_8 = (mask_size // 8 + 1) * 8
            else:
                mask_size_8 = mask_size

        with open(os.path.join(file_path, f'mask_{frame_id}.rerf'), 'rb') as masked_file:
            mask_bits = bitarray()
            mask_bits.fromfile(masked_file)
            mask=torch.from_numpy(np.unpackbits(mask_bits).reshape(mask_size_8)[:mask_size].astype(np.bool)).cuda()

        quality=header["quality"]
        if not key_frame and pca:
            rec_feature = []
            rec_feature.append(decode_jpeg_huffman(
                os.path.join(file_path, f'feature_{frame_id}_{quality}.rerf'), headers["headers"][0], device=device))
            rec_feature.append(decode_jpeg_huffman(
                os.path.join(file_path, f'feature_{frame_id}_{quality-1}.rerf'), headers["headers"][1], device=device))
            residual_rec_dct = unproject_pca_mmap(rec_feature, file_path, frame_id, device,voxel_size)
        else:
            residual_rec_dct = decode_jpeg_huffman(
                os.path.join(file_path, f'feature_{frame_id}_{quality}.rerf'), header, device=device)

        if key_frame or frame_count==0 or (not os.path.exists(os.path.join(file_path, f'deform_mask_{frame_id}.rerf'))) or \
                (not os.path.exists(os.path.join(file_path, f'deform_{frame_id}.npy'))):
            former_rec=torch.zeros(( mask.size(0), n_channel, voxel_size**3),device=device) #big_data_0
            former_rec[:, 0, :] = former_rec[:, 0, :] - 4.1
            former_rec = recover_misc(residual_rec_dct, former_rec, header, mask,n_channel=n_channel,device=device)
        else:
            with open(os.path.join(file_path, f'deform_mask_{frame_id}.rerf'), 'rb') as masked_file:
                mask_bits = bitarray()
                mask_bits.fromfile(masked_file)

                deform_mask = np.unpackbits(mask_bits).reshape(mask_size_8)[:mask_size].astype(np.bool)

                deform = np.load(os.path.join(file_path, f'deform_{frame_id}.npy'))

                deform = decode_entropy_motion_npy(deform, deform_mask, device)

            if not key_frame and pca:
                header["size"][1]+=headers["headers"][1]["size"][1]
                header["origin_size"][0]+=headers["headers"][1]["origin_size"][0]

            former_rec = recover_misc_deform(residual_rec_dct, former_rec, header, mask, deform,model_kwargs,
                                             n_channel=n_channel, device=device)
        yield former_rec
        frame_count += 1
        frame_id+=1

mmap_iter=mmap_decode()
model_receive=next(mmap_iter)
dvgo_model.density=torch.nn.Parameter(model_receive[:,:1])
dvgo_model.k0.k0=torch.nn.Parameter(model_receive[:,1:])
dvgo_model.k0.eval()

stepsize = cfg.fine_model_and_render.stepsize
render_viewpoints_kwargs = {
    'model': dvgo_model,
    'ndc': cfg.data.ndc,
    'render_kwargs': {
        'near': data_dict['near'],
        'far': data_dict['far'],
        'bg': 1 if cfg.data.white_bkgd else 0,
        'stepsize': stepsize,
        'inverse_y': cfg.data.inverse_y,
        'flip_x': cfg.data.flip_x,
        'flip_y': cfg.data.flip_y,
        'render_depth': True,
        'frame_ids': frame_id
    },
}

if args.render_360 > 0:
    render_poses = data_dict['poses'][data_dict['i_train']]
    render_poses = torch.tensor(render_poses).cpu()

    bbox_path = os.path.join(cfg.data['datadir'], 'bbox.json')
    with open(bbox_path, 'r') as f:
        bbox_json = json.load(f)
    xyz_min_fine = torch.tensor(bbox_json['xyz_min'])
    xyz_max_fine = torch.tensor(bbox_json['xyz_max'])
    bbox = torch.stack([xyz_min_fine, xyz_max_fine]).cpu()

    center = torch.mean(bbox.float(), dim=0)
    up = -torch.mean(render_poses[:, 0:3, 1], dim=0)
    up = up / torch.norm(up)

    radius = torch.norm(render_poses[0, 0:3, 3] - center) * 2
    center = center + up * radius * 0.002

    v = torch.tensor([0, 0, -1], dtype=torch.float32).cpu()
    v = v - up.dot(v) * up
    v = v / torch.norm(v)

    #
    s_pos = center - v * radius - up * radius * 0

    center = center.numpy()
    up = up.numpy()
    radius = radius.item()
    s_pos = s_pos.numpy()

    lookat = center - s_pos
    lookat = lookat / np.linalg.norm(lookat)

    xaxis = np.cross(lookat, up)
    xaxis = xaxis / np.linalg.norm(xaxis)


    sTs = []
    sKs = []
    HWs = []
    frame_ids=[]

    HW=data_dict['HW'][data_dict['i_train']][0]
    sK=data_dict['Ks'][data_dict['i_train']][0]

    for i in range(0, args.render_360, 1):
        angle = 3.1415926 * 2 * i / 360.0
        pos = s_pos - center
        pos = rodrigues_rotation_matrix(up, -angle).dot(pos)
        pos = pos + center

        lookat = center - pos
        lookat = lookat / np.linalg.norm(lookat)

        xaxis = np.cross(lookat, up)
        xaxis = xaxis / np.linalg.norm(xaxis)

        yaxis = -np.cross(xaxis, lookat)
        yaxis = yaxis / np.linalg.norm(yaxis)

        nR = np.array([xaxis, yaxis, lookat, pos]).T
        nR = np.concatenate([nR, np.array([[0, 0, 0, 1]])])

        sTs.append(nR)
        sKs.append(sK)
        HWs.append(HW)
        frame_ids.append(i%cfg.frame_num)

    sTs = np.stack(sTs)
    sKs = np.stack(sKs)

    def model_callback(model, render_kwargs, frame_id):

        if frame_id != args.render_start_frame:
            model_receive = next(mmap_iter)
            model.density = torch.nn.Parameter(model_receive[:, :1])
            model.k0.k0 = torch.nn.Parameter(model_receive[:, 1:])

            density = F.max_pool3d(model.density, kernel_size=3, padding=1, stride=1)
            alpha = 1 - torch.exp(
                -F.softplus(density + model_kwargs['act_shift']) * model_kwargs['voxel_size_ratio'])
            mask = (alpha >= model.mask_cache_thres).squeeze(0).squeeze(0)
            xyz_min = torch.Tensor(model_kwargs['xyz_min'])
            xyz_max = torch.Tensor(model_kwargs['xyz_max'])

            model.mask_cache.mask= mask
            xyz_len = xyz_max - xyz_min
            model.mask_cache.xyz2ijk_scale= (torch.Tensor(list(mask.shape)) - 1) / xyz_len
            model.mask_cache.xyz2ijk_shift= -xyz_min * model.mask_cache.xyz2ijk_scale


        return model, render_kwargs


    render_viewpoints_kwargs['model_callback'] = model_callback

    testsavedir = os.path.join(cfg.basedir, cfg.expname, f'render_360_rerf_{args.render_360}')
    os.makedirs(testsavedir, exist_ok=True)

    with torch.cuda.amp.autocast(enabled=args.fp16):
        rgbs, depths = render_viewpoints_frames(
            cfg=cfg,
            render_poses=torch.tensor(sTs).float(),
            HW=HWs,
            Ks=torch.tensor(sKs).float(),frame_ids=frame_ids,
            gt_imgs=None,
            savedir=testsavedir,
            **render_viewpoints_kwargs)
