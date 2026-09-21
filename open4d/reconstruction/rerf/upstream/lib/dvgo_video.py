from ast import Assign
import os
import time
import functools
from turtle import forward
import numpy as np

import torch
import torch.nn as nn
import torch.nn.functional as F
import ipdb
from torch_scatter import segment_coo

from torch.utils.cpp_extension import load
import copy

from . import utils, dvgo, dmpigo

class RGB_Net(torch.nn.Module):
    def __init__(self,dim0=None, rgbnet_width=None, rgbnet_depth=None):
        super(RGB_Net, self).__init__()
        self.rgbnet = None

        if dim0 is not None and rgbnet_width is not None and rgbnet_depth is not None:
            self.set_params(dim0,rgbnet_width,rgbnet_depth)

    def set_params(self, dim0, rgbnet_width, rgbnet_depth):
        
        if self.rgbnet is None:
            self.dim0= dim0
            self.rgbnet_width = rgbnet_width
            self.rgbnet_depth = rgbnet_depth
            self.rgbnet = nn.Sequential(
                    nn.Linear(dim0, rgbnet_width), nn.ReLU(inplace=True),
                    *[
                        nn.Sequential(nn.Linear(rgbnet_width, rgbnet_width), nn.ReLU(inplace=True))
                        for _ in range(rgbnet_depth-2)
                    ],
                    nn.Linear(rgbnet_width, 3),
                )
            nn.init.constant_(self.rgbnet[-1].bias, 0)
            print('***** rgb_net_ reset   *******')
        else:
            if self.dim0!=dim0 or self.rgbnet_width!=rgbnet_width or self.rgbnet_depth!=rgbnet_depth:
                ipdb.set_trace()
                raise Exception("Inconsistant parameters!")

        return lambda x: self.forward(x)

    def forward(self,x):
        if self.rgbnet is None:
            raise Exception("call set_params() first!")
        return self.rgbnet(x)

    def get_kwargs(self):
        return {
            'dim0': self.dim0,
            'rgbnet_width': self.rgbnet_width,
            'rgbnet_depth': self.rgbnet_depth
        }

class Deform_Net(torch.nn.Module):
    def __init__(self, width=128, depth=6):
        super(Deform_Net, self).__init__()

        self.width=width
        self.depth=depth

        self.deform_net=nn.Sequential(
                    nn.Linear(3, width), nn.ReLU(inplace=True),
                    *[
                        nn.Sequential(nn.Linear(width, width), nn.ReLU(inplace=True))
                        for _ in range(depth)
                    ],
                    nn.Linear(width, 3),
                )

    def forward(self,x):
        return self.deform_net(x)






'''Model'''
class DirectVoxGO_Video(torch.nn.Module):
    def __init__(self):
        super(DirectVoxGO_Video, self).__init__()

        self.rgbnet = RGB_Net()
   
        self.activated_dvgos = []
        self.dvgos = nn.ModuleDict()


    def set_dvgo_update(self, frame_ids):
        unique_frame_ids = torch.unique(torch.tensor(frame_ids), sorted=True).cpu()
        unique_frame_ids = unique_frame_ids.long()
        unique_frame_ids = unique_frame_ids.numpy().tolist()

        self.activated_dvgos = []

        for id in unique_frame_ids:
            if str(id) in self.dvgos:
                self.activated_dvgos.append(str(id))

        #self.activate_refinenet()
        print('Activated DVGOS:', self.activated_dvgos)


    def load_previous_models(self,frame_ids, args, cfg,stage = 'fine',finetune = False):

        if cfg.train_mode == 'individual' and not finetune:
            self.current_frame_id = frame_ids[-1].item()
            print('current frame:', self.current_frame_id)
            return None

        device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
        frame_ids = torch.unique(frame_ids, sorted=True).long()
        for id in frame_ids:
            model = None
            
            # find whether there is existing checkpoint path
            last_ckpt_path = os.path.join(cfg.basedir, cfg.expname, f'{stage}_last_%d.tar' % id)
            if args.no_reload:
                reload_ckpt_path = None
            elif args.ft_path:
                reload_ckpt_path = args.ft_path
            elif os.path.isfile(last_ckpt_path):
                reload_ckpt_path = last_ckpt_path
            else:
                reload_ckpt_path = None

            # init model and optimizer
            if reload_ckpt_path is not None:
                print(f'scene_rep_reconstruction ({stage}): reload from {reload_ckpt_path}')
                if cfg.data.ndc:
                    model_class = dmpigo.DirectMPIGO
                else:
                    model_class = dvgo.DirectVoxGO

                ckpt = torch.load(reload_ckpt_path)
                model_kwargs= ckpt['model_kwargs']
                model_kwargs['rgbnet'] = self.rgbnet
                model_kwargs['rgbfeat_sigmoid'] = cfg.codec.rgbfeat_sigmoid
                model = model_class(**model_kwargs)
                model.load_state_dict(ckpt['model_state_dict'])
                model  = model.to(device)
            if model is not None:
                self.dvgos[str(id.item())]=model

        self.current_frame_id = frame_ids[-1].item()
        print('current frame:', self.current_frame_id)

        return len(self.dvgos)


    def create_current_model(self,frame_id,xyz_min,xyz_max,stage, cfg, cfg_model, cfg_train, coarse_ckpt_path, use_pca = True,
                             use_res=False,use_deform=False):
        
        device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
        print(f'scene_rep_reconstruction ({stage}): train from scratch')

        print("----------use res-------", use_res)

        # init model
        model_kwargs = copy.deepcopy(cfg_model)
        model_kwargs['rgbnet'] = self.rgbnet
        model_kwargs['use_pca'] = use_pca
        model_kwargs['use_res'] = use_res
        model_kwargs['use_deform'] = use_deform
        model_kwargs['cfg'] = cfg
        model_kwargs['rgbfeat_sigmoid'] = cfg.codec.rgbfeat_sigmoid

        print(f'scene_rep_reconstruction ({stage}): \033[96muse dense voxel grid\033[0m')
        num_voxels = model_kwargs.pop('num_voxels')
        if len(cfg_train.pg_scale) :
            num_voxels = int(num_voxels / (2**len(cfg_train.pg_scale)))
        model = dvgo.DirectVoxGO(
            xyz_min=xyz_min, xyz_max=xyz_max,
            num_voxels=num_voxels,
            mask_cache_path=coarse_ckpt_path,
            **model_kwargs)
            
        model = model.to(device)
        self.dvgos[str(int(frame_id))]=model

        self.current_frame_id =  frame_id


        return self.dvgos[str(int(frame_id))]


    # frame_ids should be sorted !!!!!!
    def forward(self, rays_o, rays_d, viewdirs, frame_ids=0, global_step=None, **render_kwargs):

        ret_dict = None
        N = len(rays_o)

        if type(frame_ids)==int:
            frame_ids = torch.ones(N)*frame_ids
            frame_ids = frame_ids.long().cpu()
        assert N==frame_ids.size(0)
        assert frame_ids.type()=='torch.LongTensor'

        unique_frame_ids = torch.unique(frame_ids, sorted=True)
        assert unique_frame_ids.size(0)<= len(self.dvgos)

        #ipdb.set_trace()

        for id in unique_frame_ids:
            # !!!!!!  Only for one frame
            #mask = (frame_ids==id)
            #rays_o_tmp = rays_o[mask,...]
            #rays_d_tmp = rays_d[mask,...]
            #viewdirs_tmp = viewdirs[mask,...]

            rays_o_tmp = rays_o
            rays_d_tmp = rays_d
            viewdirs_tmp = viewdirs

            res = self.dvgos[str(id.item())](rays_o_tmp,rays_d_tmp,viewdirs_tmp,global_step,**render_kwargs)

            if ret_dict is None:
                ret_dict = res
            else:
                for k in res.keys():
                    ret_dict[k] = torch.cat([ret_dict[k], res[k]],dim=0)

        return ret_dict

    def get_current_model(self):
        return self.dvgos[str(self.current_frame_id)]

    def get_current_frameid(self):
        return self.current_frame_id

    
    def load_rgb_net(self, cfg, exception = False):
        last_ckpt_path = os.path.join(cfg.basedir, cfg.expname, f'rgb_net.tar')

        if cfg.train_mode == 'individual' and  not cfg.fix_rgbnet and not exception:
             last_ckpt_path = os.path.join(cfg.basedir, cfg.expname, f'rgb_net_%d.tar' % self.get_current_frameid() )

        if not os.path.isfile(last_ckpt_path):
            print('rgb_net checkpoint not found.')
            return

        ckpt = torch.load(last_ckpt_path)
        model_kwargs = ckpt['model_kwargs']

        self.rgbnet.set_params(**model_kwargs)
        self.rgbnet.load_state_dict(ckpt['model_state_dict'])
        print('****** rgb net loaded.***** !!!!',last_ckpt_path.split('/')[-1] )

    def load_rgb_net_mmap(self, cfg,ckpt):
        model_kwargs = ckpt['model_kwargs']

        self.rgbnet.set_params(**model_kwargs)
        self.rgbnet.load_state_dict(ckpt['model_state_dict'])
        print('****** rgb net loaded.***** !!!!')

    def set_deform_net(self,cfg):
        if cfg.use_deform=="mlp":
            self.deform_net=Deform_Net()





    def save_rgb_net(self, cfg,exception = False):


        last_ckpt_path = os.path.join(cfg.basedir, cfg.expname, f'rgb_net.tar')

        if cfg.train_mode == 'individual' and  not cfg.fix_rgbnet and not exception:
            last_ckpt_path = os.path.join(cfg.basedir, cfg.expname, f'rgb_net_%d.tar' % self.get_current_frameid() )

        torch.save({
                'model_kwargs': self.rgbnet.get_kwargs(),
                'model_state_dict': self.rgbnet.state_dict(),
                #'optimizer_state_dict': optimizer.state_dict(),
                    }, last_ckpt_path)

        print('save rgb net:',last_ckpt_path)

        

    def save_all_model(self, cfg, use_pca=False,use_deform=False, exception = False):
        for i in self.activated_dvgos:
            id = int(i)

            if use_pca:
                last_ckpt_path = os.path.join(cfg.basedir, cfg.expname, f'fine_last_%d_pca.tar' % id)
            elif use_deform and id !=0:
                last_ckpt_path=os.path.join(cfg.basedir, cfg.expname, f'fine_last_%d_deform.tar' % id)
            else:
                last_ckpt_path = os.path.join(cfg.basedir, cfg.expname, f'fine_last_%d.tar' % id)
            torch.save({
                'global_step': cfg.fine_train.N_iters,
                'model_kwargs': self.dvgos[i].get_kwargs(),
                'model_state_dict': self.dvgos[i].state_dict(),
                #'optimizer_state_dict': optimizer.state_dict(),
                }, last_ckpt_path)
            print(f'scene_rep_reconstruction (final): saved checkpoints at', last_ckpt_path)

        self.save_rgb_net(cfg, exception = exception)



        
        
