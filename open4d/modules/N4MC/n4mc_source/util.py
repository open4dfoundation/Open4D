# Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES.  All rights reserved.
#
# NVIDIA CORPORATION & AFFILIATES and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION & AFFILIATES is strictly prohibited.
import os
import random

import constriction
import cv2
import numpy as np
import torch
import trimesh
import kaolin
from tqdm import tqdm
import point_cloud_utils as pcu
import math
import torch.nn.functional as F
import open3d as o3d
from skimage.metrics import structural_similarity as ssim
import py7zr
import zipfile
from metrics import compute_D1_psnr, compute_D2_psnr, compute_D1_D2_psnr
###############################################################################
# Functions adapted from https://github.com/NVlabs/nvdiffrec
###############################################################################

def dot(x: torch.Tensor, y: torch.Tensor) -> torch.Tensor:
    return torch.sum(x*y, -1, keepdim=True)

def length(x: torch.Tensor, eps: float =1e-8) -> torch.Tensor:
    return torch.sqrt(torch.clamp(dot(x,x), min=eps)) # Clamp to avoid nan gradients because grad(sqrt(0)) = NaN

def safe_normalize(x: torch.Tensor, eps: float =1e-8) -> torch.Tensor:
    return x / length(x, eps)

def perspective(fovy=0.7854, aspect=1.0, n=0.1, f=1000.0, device=None):
    y = np.tan(fovy / 2)
    return torch.tensor([[1/(y*aspect),    0,            0,              0], 
                         [           0, 1/-y,            0,              0], 
                         [           0,    0, -(f+n)/(f-n), -(2*f*n)/(f-n)], 
                         [           0,    0,           -1,              0]], dtype=torch.float32, device=device)

def translate(x, y, z, device=None):
    return torch.tensor([[1, 0, 0, x], 
                         [0, 1, 0, y], 
                         [0, 0, 1, z], 
                         [0, 0, 0, 1]], dtype=torch.float32, device=device)

@torch.no_grad()
def random_rotation_translation(t, device=None):
    m = np.random.normal(size=[3, 3])
    m[1] = np.cross(m[0], m[2])
    m[2] = np.cross(m[0], m[1])
    m = m / np.linalg.norm(m, axis=1, keepdims=True)
    m = np.pad(m, [[0, 1], [0, 1]], mode='constant')
    m[3, 3] = 1.0
    m[:3, 3] = np.random.uniform(-t, t, size=[3])
    return torch.tensor(m, dtype=torch.float32, device=device)

def rotate_x(a, device=None):
    s, c = np.sin(a), np.cos(a)
    return torch.tensor([[1,  0, 0, 0], 
                         [0,  c, s, 0], 
                         [0, -s, c, 0], 
                         [0,  0, 0, 1]], dtype=torch.float32, device=device)

def rotate_y(a, device=None):
    s, c = np.sin(a), np.cos(a)
    return torch.tensor([[ c, 0, s, 0], 
                         [ 0, 1, 0, 0], 
                         [-s, 0, c, 0], 
                         [ 0, 0, 0, 1]], dtype=torch.float32, device=device)
    
class Mesh:
    def __init__(self, vertices, faces):
        self.vertices = vertices
        self.faces = faces
        
    def auto_normals(self):
        v0 = self.vertices[self.faces[:, 0], :]
        v1 = self.vertices[self.faces[:, 1], :]
        v2 = self.vertices[self.faces[:, 2], :]
        nrm = safe_normalize(torch.cross(v1 - v0, v2 - v0))
        self.nrm = nrm

def load_mesh(path, device):
    mesh_np = trimesh.load(path)
    vertices = torch.tensor(mesh_np.vertices, device=device, dtype=torch.float)
    faces = torch.tensor(mesh_np.faces, device=device, dtype=torch.long)
    
    # Normalize
    vmin, vmax = vertices.min(dim=0)[0], vertices.max(dim=0)[0]
    scale = 1.8 / torch.max(vmax - vmin).item()
    vertices = vertices - (vmax + vmin) / 2 # Center mesh on origin
    vertices = vertices * scale # Rescale to [-0.9, 0.9]
    return Mesh(vertices, faces)

def compute_sdf(points, vertices, faces):
    face_vertices = kaolin.ops.mesh.index_vertices_by_faces(vertices.clone().unsqueeze(0), faces)
    distance = kaolin.metrics.trianglemesh.point_to_mesh_distance(points.unsqueeze(0), face_vertices)[0]
    with torch.no_grad():
        sign = (kaolin.ops.mesh.check_sign(vertices.unsqueeze(0), faces, points.unsqueeze(0))<1).float() * 2 - 1
    sdf = (sign*distance).squeeze(0)
    return sdf

def sample_random_points(n, mesh):
    pts_random = (torch.rand((n//2,3),device='cuda') - 0.5) * 2
    pts_surface = kaolin.ops.mesh.sample_points(mesh.vertices.unsqueeze(0), mesh.faces, 500)[0].squeeze(0)
    pts_surface += torch.randn_like(pts_surface) * 0.05
    pts = torch.cat([pts_random, pts_surface])
    return pts #pts_surface


def load_meshes_seq(path_list, return_centere_scale=False):
    v_list=[]
    f_list=[]
    scaled_v_list=[]

    for mesh_path in tqdm(path_list,desc='loading mesh'):
        v,f=pcu.load_mesh_vf(mesh_path)
        v_list.append(v)
        f_list.append(f)

    v_all=np.concatenate(v_list,axis=0)

    center = (v_all.max(0)+v_all.min(0))/2   #v_all.mean(0)
    scale=np.max(v_all.max(0)-v_all.min(0))

    for v in v_list:
        scaled_v_list.append((v-center.reshape(1,3))/scale*1.99)

    if not return_centere_scale:

        return scaled_v_list,f_list
    
    else:
        return scaled_v_list, f_list, center, scale
    
def filter_connected_components(mesh, min_triangle_count):
    # 获取网格的连通组件
    components = mesh.split()

    # 过滤出满足条件的连通组件
    filtered_components = [component for component in components if component.faces.shape[0] > min_triangle_count]

    # 获取满足条件的三角形索引
    filtered_mesh =  trimesh.util.concatenate(filtered_components)           #[triangle for component in filtered_components for triangle in component]

    return filtered_mesh

class SSIM3D(torch.nn.Module):
    def __init__(self,window_size=5,sigma=1.5,channel=4):
        super().__init__()

        self.padding = window_size // 2
        self.channel=channel
        #self.data_range=data_range
        self.register_buffer('kernel',self.create_gaussian_kernel(window_size, sigma, channel))
        

    def create_gaussian_kernel(self,window_size, sigma, channels):
        kernel = torch.tensor([
            [[self.gaussian(x-window_size//2, y-window_size//2, z-window_size//2, sigma) for z in range(window_size)]
            for y in range(window_size)]
            for x in range(window_size)
        ])
        kernel = kernel / torch.sum(kernel)
        kernel = kernel.view(1, 1, window_size, window_size, window_size).repeat(channels, 1, 1, 1, 1)
        return kernel

    def gaussian(self,x, y, z, sigma):
        return math.exp(-(x ** 2 + y ** 2 + z ** 2) / (2 * sigma ** 2))
    
    def forward(self,x,y):
        x=(x+1)/2
        y=(y+1)/2
        mu1 = F.conv3d(x, self.kernel, padding=self.padding, groups=self.channel)
        mu2 = F.conv3d(y, self.kernel, padding=self.padding, groups=self.channel)

        mu1_sq = mu1.pow(2)
        mu2_sq = mu2.pow(2)
        mu12 = mu1 * mu2

        sigma1_sq = F.conv3d(x * x, self.kernel, padding=self.padding, groups=self.channel) - mu1_sq
        sigma2_sq = F.conv3d(y * y, self.kernel, padding=self.padding, groups=self.channel) - mu2_sq
        sigma12 = F.conv3d(x * y, self.kernel, padding=self.padding, groups=self.channel) - mu12
 
        c1 = 0.01**2    #(0.01 * self.data_range) ** 2
        c2 = 0.03**2    #(0.03 * self.data_range) ** 2

        numerator = (2 * mu12 + c1) * (2 * sigma12 + c2)
        denominator = (mu1_sq + mu2_sq + c1) * (sigma1_sq + sigma2_sq + c2)

        ssim_map = numerator / denominator
        ssim_val = torch.mean(ssim_map)

        return ssim_val


def compress_and_decompress_matrix(D: np.ndarray, output_path: str, filename_prefix: str,
                                   scaling_factor: float = 10000.0):
    """
    Compress a float matrix using QuantizedGaussian entropy coding and reconstruct it.

    Args:
        D (np.ndarray): The input 2D float matrix to be compressed.
        output_path (str): Directory to store the compressed file.
        filename_prefix (str): Prefix for the saved file names.
        scaling_factor (float): Scale factor for quantization (default 10000).

    Returns:
        reconstructed (np.ndarray): The decompressed and reconstructed matrix.
        compression_ratio (float): Original bits / compressed bits.
        mse (float): Mean squared error between input and reconstructed matrix.
    """
    assert D.ndim == 2, "Input matrix D must be 2D"

    # Quantize float values to integers
    D_quantized = np.round(D * scaling_factor).astype(np.int32)
    min_val, max_val = np.min(D_quantized), np.max(D_quantized)
    print(f"Quantized data range: [{min_val}, {max_val}]")

    # Define entropy model
    model_range = (min_val, max_val)
    model_family = constriction.stream.model.QuantizedGaussian(*model_range)

    # Flatten the matrix
    symbols = D_quantized.flatten().astype(np.int32)

    # Estimate entropy model parameters (mean and std for each column)
    means = np.zeros_like(symbols, dtype=np.float64)
    stds = np.zeros_like(symbols, dtype=np.float64)
    cols = D.shape[1]
    for j in range(cols):
        col_data = D_quantized[:, j]
        mean = np.mean(col_data)
        std = np.std(col_data) if np.std(col_data) > 0 else 1.0
        means[j::cols] = mean
        stds[j::cols] = std

    # Encode the symbols
    encoder = constriction.stream.stack.AnsCoder()
    encoder.encode_reverse(symbols, model_family, means, stds)

    # Save the compressed bitstream
    compressed = encoder.get_compressed()
    np.save(os.path.join(output_path, f"{filename_prefix}_encoded.npy"), compressed)
    np.savez_compressed(os.path.join(output_path, f"{filename_prefix}_encoded.npz"), coeffs=compressed)
    # Report compression results
    compressed_bits = encoder.num_bits()
    original_bits = D.nbytes * 8
    compression_ratio = original_bits / compressed_bits
    print(f"Compressed size: {compressed_bits} bits")
    print(f"Compression ratio: {compression_ratio:.2f}")

    # Decode
    decoder = constriction.stream.stack.AnsCoder(compressed)
    reconstructed_quantized = decoder.decode(model_family, means, stds)
    reconstructed = reconstructed_quantized.reshape(D.shape) / scaling_factor

    # Evaluate reconstruction
    mse = np.mean((D - reconstructed) ** 2)
    print(f"Mean squared error: {mse:.2e}")
    print(f"Reconstructed matrix sample (first 5 rows):\n{reconstructed[:5, :]}")
    print("Matrix successfully encoded and decoded!")

    return reconstructed, compression_ratio, mse


def compute_tsdf_normals(tsdf):
    """
    tsdf: (B, X, Y, Z) single-channel tensor
    Returns: (B, X, Y, Z, 3) normalized gradient vectors
    """
    # Central differences for gradient
    dz = tsdf[..., 2:] - tsdf[..., :-2]
    dy = tsdf[..., 1:-1, 2:, :] - tsdf[..., 1:-1, :-2, :]
    dx = tsdf[..., 1:-1, :, 2:] - tsdf[..., 1:-1, :, :-2]

    # Pad to keep same size
    dx = F.pad(dx, (1, 1, 0, 0, 0, 0))
    dy = F.pad(dy, (0, 0, 1, 1, 0, 0))
    dz = F.pad(dz, (0, 0, 0, 0, 1, 1))

    normals = torch.stack([dx, dy, dz], dim=-1)
    normals = F.normalize(normals, p=2, dim=-1, eps=1e-6)
    return normals


def normal_consistency_loss(pred_sdf, gt_sdf, points):
    # pred_sdf, gt_sdf: (B, P, 1)
    # points: (B, P, 3) sampled query points
    grads_pred = torch.autograd.grad(
        outputs=pred_sdf,
        inputs=points,
        grad_outputs=torch.ones_like(pred_sdf),
        create_graph=True,
        retain_graph=True,
        only_inputs=True,
    )[0]  # (B, P, 3)

    grads_gt = torch.autograd.grad(
        outputs=gt_sdf,
        inputs=points,
        grad_outputs=torch.ones_like(gt_sdf),
        create_graph=True,
        retain_graph=True,
        only_inputs=True,
    )[0]

    normals_pred = F.normalize(grads_pred, dim=-1)
    normals_gt   = F.normalize(grads_gt, dim=-1)

    return (1.0 - (normals_pred * normals_gt).sum(-1)).mean()

def ramp_weight(epoch, start, end, target):
    """
    Linear ramp-up scheduler:
    epoch < start → 0
    start <= epoch <= end → linearly grows to target
    epoch > end → target
    """
    if start == end:
        return target
    if epoch < start:
        return 0.0
    elif epoch > end:
        return target
    else:
        return target * (epoch - start) / (end - start)

def set_seed(seed: int = 42):
    # Python random
    random.seed(seed)
    # Numpy
    np.random.seed(seed)
    # PyTorch (CPU + CUDA)
    torch.manual_seed(seed)
    torch.cuda.manual_seed(seed)
    torch.cuda.manual_seed_all(seed)  # if using multi-GPU

    # Ensure deterministic behavior in cuDNN
    torch.backends.cudnn.deterministic = True
    torch.backends.cudnn.benchmark = False

    print(f"Training seed set to {seed}")


