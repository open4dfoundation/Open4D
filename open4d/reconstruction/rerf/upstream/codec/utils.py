import torch
import copy
import numpy as np
import matplotlib.pyplot as plt

def torch_and(a,b):
    return (a*b).bool()

def get_color(sdf):

    color=torch.zeros((1,sdf.size(0),3))#.cuda()

    color[...,torch_and(sdf>0, sdf<0.25),2 ]=255
    color[..., torch_and(sdf>0, sdf<0.25), 1] = (sdf[torch_and(sdf>0, sdf<0.25)]-0.1)/0.25*255

    color[...,torch_and(sdf>=0.25 , sdf<0.75),1]=255
    color[..., torch_and(sdf>=0.25 , sdf<0.75), 2]= (0.5-sdf[torch_and(sdf>=0.25 , sdf<0.75)])/0.25*255

    color[...,torch_and(sdf >= 0.5 , sdf < 0.75), 0] = (sdf[torch_and(sdf >= 0.5 , sdf < 0.75)] - 0.5) / 0.25 * 255

    color[..., sdf >= 0.75, 0] = 255
    color[..., torch_and(sdf >= 0.75 ,sdf < 1), 1] = (1 - sdf[torch_and(sdf >= 0.75 ,sdf < 1)]) / 0.25 * 255

    return color

def get_origin_size(data,channel=False):
    size=list(data.size())
    if channel:
        return size[1:]
    else:
        return size[2:]

def zero_pads(data, voxel_size=16):
    if data.size(0) == 1:
        data = data.squeeze(0)

    size = list(data.size())

    new_size = copy.deepcopy(size)
    for i in range(1, len(size)):
        if new_size[i] % voxel_size == 0:
            continue
        new_size[i] = (new_size[i] // voxel_size + 1) * voxel_size

    res = torch.zeros(new_size, device=data.device)
    res[:, :size[1], :size[2], :size[3]] = data.clone()
    return res


def zero_unpads(data, size):
    return data[:, :size[0], :size[1], :size[2]]


def split_volume(data, voxel_size=16):
    size = list(data.size())
    for i in range(1, len(size)):
        size[i] = size[i] // voxel_size

    res = []
    for x in range(size[1]):
        for y in range(size[2]):
            for z in range(size[3]):
                res.append(data[:, x * voxel_size:(x + 1) * voxel_size, y * voxel_size:(y + 1) * voxel_size,
                           z * voxel_size:(z + 1) * voxel_size].clone())

    res = torch.stack(res)

    return res, size[1:]


def merge_volume(data, size):
    M, NF, Vx, Vy, Vz = data.shape
    data_tmp = data[:size[0] * size[1] * size[2]].reshape(size[0], size[1], size[2], NF, Vx, Vy, Vz)
    data_tmp = data_tmp.permute(3, 0, 4, 1, 5, 2, 6)
    res = data_tmp.reshape(NF, size[0] * Vx, size[1] * Vy, size[2] * Vz)
    return res




