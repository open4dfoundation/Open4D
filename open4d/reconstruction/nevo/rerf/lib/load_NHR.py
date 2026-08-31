import os
import torch
import numpy as np
import imageio
import json
import torch.nn.functional as F
import cv2
import ipdb
import PIL
from PIL import Image
import collections
import math
import torchvision.transforms as T

class Image_Transforms(object):
    def __init__(self, size, interpolation=Image.BICUBIC, is_center = False):
        assert isinstance(size, int) or (isinstance(size, collections.abc.Iterable) and len(size) == 2)
        self.size = size
        self.interpolation = interpolation
        self.is_center = is_center
        
    def __call__(self, img, Ks , Ts ,  mask = None):

        K = Ks
        Tc = Ts


        img = Image.fromarray(img.astype('uint8'), 'RGB')
        mask = Image.fromarray(mask.astype('uint8'), 'RGB')
        img_np = np.asarray(img)
        

        width, height = img.size


        if self.is_center:
            translation = [width /2-K[0,2],height/2-K[1,2]]
            translation = list(translation)
            ration = 1.05
            
            if (self.size[1]/2)/(self.size[0]*ration  / height) - K[0,2] != translation[0] :
                ration = 1.2
            translation[1] = (self.size[0]/2)/(self.size[0]*ration  / height) - K[1,2]
            translation[0] = (self.size[1]/2)/(self.size[0]*ration  / height) - K[0,2]
            translation = tuple(translation)
        else:
            translation=(0,0)
            ration=1.
        
   
        img = T.functional.affine(img, angle = 0, translate = translation, scale= 1,shear=0)
        img = T.functional.crop(img, 0, 0,  int(height/ration),int(height*self.size[1]/ration/self.size[0]) )
        img = T.functional.resize(img, self.size, self.interpolation)
        img = T.functional.to_tensor(img)
        img = img.permute(1,2,0)

        
        ROI = np.ones_like(img_np)*255.0

        ROI = Image.fromarray(np.uint8(ROI))
        ROI = T.functional.affine(ROI, angle = 0, translate = translation, scale= 1,shear=0)
        ROI = T.functional.crop(ROI, 0,0, int(height/ration),int(height*self.size[1]/ration/self.size[0]) )
        ROI = T.functional.resize(ROI, self.size, self.interpolation)
        ROI = T.functional.to_tensor(ROI)
        ROI = ROI[0:1,:,:]
        
        
        
        
        if mask is not None:
            mask = T.functional.affine(mask, angle = 0, translate = translation, scale= 1,shear=0)
            mask = T.functional.crop(mask, 0, 0,  int(height/ration),int(height*self.size[1]/ration/self.size[0]) )
            mask = T.functional.resize(mask, self.size, self.interpolation)
            mask = T.functional.to_tensor(mask)
            mask = mask.permute(1,2,0)
            mask = mask[:,:,0:1]


        K[0,2] = K[0,2] + translation[0]
        K[1,2] = K[1,2] + translation[1]

        s = self.size[0] * ration / height

        K = K*s

        K[2,2] = 1  
                
 
        return img, K, Tc, mask, ROI
    
    def __repr__(self):
        return self.__class__.__name__ + '()'




def load_NHR_data(basedir, frame = 0, half_res=True):

    transform_path = os.path.join(basedir, 'cams_%d.json' % frame)
    print('load NHR data:',transform_path)
    with open(transform_path, 'r') as f:
            transform = json.load(f)

    frames = transform["frames"]
    frames = sorted(frames, key=lambda d: d['file'])

    poses = []
    images = []
    intrinsic =[]
    masks =[]

    tar_size = (2160, 3840)
    if half_res:
        tar_size = (720,960)


    transforms = Image_Transforms(tar_size)

    for f in frames:
        f_path = f['file']
        f_path_mask =  f['mask']

        # there are non-exist paths in fox...
        if not os.path.exists(f_path):
            print(f_path, "doesn't exist.")
            continue
        if not os.path.exists(f_path_mask):
            print(f_path_mask, "doesn't exist.")
            continue
        
        pose = (np.array(f['extrinsic'], dtype=np.float32)) # [4, 4]

        K = np.array(f['intrinsic'], dtype=np.float32)


        mask = cv2.imread(f_path_mask)
  

        image = cv2.imread(f_path, cv2.IMREAD_UNCHANGED) 
        image = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)

        img, K, Tc, mask, ROI = transforms(image, K, pose,mask)

        poses.append(Tc)
        images.append(img)
        intrinsic.append(K)
        masks.append(mask)

    i_split = [np.arange(0, len(poses)) for i in range(3)]

    poses = np.stack(poses, axis=0).astype(np.float32)
    intrinsic = np.stack(intrinsic, axis=0).astype(np.float32)

    images = torch.stack(images)
    masks = torch.stack(masks)

    images = torch.cat([images, masks],dim = -1).numpy()

    return images, poses, poses, [tar_size[0], tar_size[1], intrinsic[0,0,0]], intrinsic, i_split


class NHR_Dataset(torch.utils.data.Dataset):
    def __init__(self, path, num_frame, tar_size=(1080,1920), cam_num = -1):
        super().__init__()
        self.cam_num = cam_num
        self.num_frame = num_frame
        self.path = path
        self.transforms = Image_Transforms(tar_size)

    def read_frame(self,frame_id, cam_num = -1):
        transform_path = os.path.join(self.path, 'cams_%d.json' % frame_id)
        with open(transform_path, 'r') as f:
            transform = json.load(f)

        frames = transform["frames"]
        frames = sorted(frames, key=lambda d: d['file'])


        if cam_num<0:
            cameras = [i for i in range(len(frames))]
        else:
            cameras = torch.randperm(len(frames))
            if cam_num>0:
                cameras = cameras[:cam_num]

        poses = []
        images = []
        intrinsic =[]
        masks =[]

        for id in cameras:
            f = frames[id]
            f_path = f['file']
            f_path_mask =  f['mask']

            # there are non-exist paths in fox...
            if not os.path.exists(f_path):
                print(f_path, "doesn't exist.")
                continue
            if not os.path.exists(f_path_mask):
                print(f_path_mask, "doesn't exist.")
                continue
            
            pose = (np.array(f['extrinsic'], dtype=np.float32)) # [4, 4]
            K = np.array(f['intrinsic'], dtype=np.float32)

            mask = cv2.imread(f_path_mask)
            image = cv2.imread(f_path, cv2.IMREAD_UNCHANGED) 
            image = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)

            img, K, Tc, mask, ROI = self.transforms(image, K, pose,mask)

            poses.append(Tc)
            images.append(img)
            intrinsic.append(K)
            masks.append(mask)

        poses = np.stack(poses, axis=0).astype(np.float32)
        intrinsic = np.stack(intrinsic, axis=0).astype(np.float32)

        images = torch.stack(images)
        masks = torch.stack(masks)
        
        images = torch.cat([images, masks],dim = -1).float()
        #images = images.permute(0,3,1,2)
        #images = images[:,0:3,:,:] * images[:,3:4,:,:].repeat(1,3,1,1) + torch.ones_like(images[:,0:3,:,:],device = images.device)*(1.0-images[:,3:4,:,:].repeat(1,3,1,1))
        return images,poses,intrinsic

    def __len__(self):
        return 100*1000

    def __getitem__(self, idx):

        frame_id = idx%self.num_frame

        images,poses,intrinsic = self.read_frame(frame_id,cam_num = self.cam_num)


        return images,poses,intrinsic


    def load_data(self, current_id, previous_ids, scale = 1.0):

        scale = int(scale)
        images,poses,intrinsic = self.read_frame(current_id,cam_num = -1)

        N = images.size(0)

        previous_ids.sort()
        if len(previous_ids) == 0:
            P = 0
        else:
            P = int(N*scale)//len(previous_ids)
            if P > N:
                P = N

        frame_ids = []

        res_images = []
        res_poses = []
        res_intrinsic = []

        for id in previous_ids:
            images_t,poses_t,intrinsic_t = self.read_frame(id,cam_num = P)
            res_images.append(images_t)
            res_poses.append(poses_t)
            res_intrinsic.append(intrinsic_t)
            frame_ids.append(torch.ones(P)*id)


        res_images.append(images)
        res_poses.append(poses)
        res_intrinsic.append(intrinsic)
        frame_ids.append(torch.ones(N)*current_id)

        res_images = torch.cat(res_images,dim=0).numpy()
        res_poses = np.concatenate(res_poses,axis=0)
        res_intrinsic = np.concatenate(res_intrinsic,axis=0)
        frame_ids = torch.cat(frame_ids).long()

        i_split = [np.arange(0, len(res_poses)) for i in range(3)]
        i_split.append(np.arange(0, P*len(previous_ids)))  # replay data
        i_split.append(np.arange(P*len(previous_ids),P*len(previous_ids)+N))  # current data

        i_split[0] =  i_split[0][::scale]

        return res_images, res_poses, res_poses, [res_images.shape[1], res_images.shape[2], intrinsic[0,0,0]], res_intrinsic, i_split,frame_ids



