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
from network import get_network,adjust_lr,diff_quantized_tensor
from dataset import get_dataset

from fmc import dynamic_marching_cubes, construct_voxel_grid, base_cube_edges
from util import Mesh, SSIM3D
import imageio
import trimesh
import time


for i in range(100):
    npz_data = np.load(f'/media/frozzzen/DataDrive/ChromeDownloads/Mesh_dataset/basketball_scaled/TSDF_128/data/{i:04d}.npz')
    input_voxel_np = npz_data['sdf']  # shape [D, H, W, 4] or possibly [4, D, H, W]
    #print("input_voxel_np:", input_voxel_np)
    offset_grids=npz_data['offset']
    print("all zero:", np.all(offset_grids == 0))
    print("offset stats:",
          "min:", offset_grids.min(),
          "max:", offset_grids.max(),
          "mean:", offset_grids.mean(),
          "abs_mean:", np.abs(offset_grids).mean())
    nonzero_ratio = np.count_nonzero(offset_grids) / offset_grids.size
    print("nonzero ratio:", nonzero_ratio)
