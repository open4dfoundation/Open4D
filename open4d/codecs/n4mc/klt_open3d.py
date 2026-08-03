import argparse
import time
import open3d as o3d
import skimage
import torch
import numpy as np
import os
from glob import glob
from numpy.lib.stride_tricks import sliding_window_view
from skimage import measure

from fmc import dynamic_marching_cubes, construct_voxel_grid, base_cube_edges
import trimesh
from util import compress_and_decompress_matrix
import zstd

parser = argparse.ArgumentParser(description="Use KLT to compress TSDF")
parser.add_argument('--input_path', type=str, required=True, help="Intput path for TSDFs")
parser.add_argument('--output_path', type=str, required=True, help="Output path for compressed TSDFs and reconstructed meshes")
parser.add_argument('--num_components', type=int, required=True, help="Number of klt basis for compression")
parser.add_argument('--block_size', type=int, required=True, help="Resolution for a block, e.g., 4 means one block contains 4 x 4 x 4 TSDFs")
parser.add_argument('--voxel_grid_res', type=int, required=True, help="Resolution for TSDFs")


args = parser.parse_args()

# Extract arguments
input_path = args.input_path
output_path = args.output_path
num_components = args.num_components
block_size = args.block_size
voxel_grid_res = args.voxel_grid_res

def extract_training_blocks_torch(tsdf_volumes, block_size=4):
    """
    Extract overlapping blocks by sliding-window.
    Input: list of TSDF tensors, each shape (D, H, W)
    """
    blocks = []
    for tsdf in tsdf_volumes:
        tsdf = tsdf.unsqueeze(0).unsqueeze(0)  # shape: (1,1,D,H,W)
        windows = tsdf.unfold(2, block_size, 1).unfold(3, block_size, 1).unfold(4, block_size, 1)

        print("unfold 2: ", tsdf.unfold(2, block_size, 1).shape)
        print("unfold 3: ", tsdf.unfold(2, block_size, 1).unfold(3, block_size, 1).shape)
        print("unfold 4: ", tsdf.unfold(2, block_size, 1).unfold(3, block_size, 1).unfold(4, block_size, 1).shape)
        # shape: (1,1,D',H',W',B,B,B)
        windows = windows.contiguous().view(-1, block_size**3)
        blocks.append(windows)
    return torch.cat(blocks, dim=0)

def compute_klt_basis_torch(blocks):
    mean = blocks.mean(dim=0, keepdim=True)
    centered = blocks - mean
    U, S, Vh = torch.linalg.svd(centered, full_matrices=False)
    print("Singular values:", S[:20])
    return Vh, mean  # P, μ


def get_nonoverlapping_blocks_torch(tsdf, block_size=4):
    tsdf = tsdf.unsqueeze(0).unsqueeze(0)  # (1,1,D,H,W)
    D, H, W = tsdf.shape[2:]

    # Calculate padding to make dimensions divisible by block_size
    pad_d = (block_size - (D % block_size)) % block_size
    pad_h = (block_size - (H % block_size)) % block_size
    pad_w = (block_size - (W % block_size)) % block_size

    # Pad the volume symmetrically
    tsdf = torch.nn.functional.pad(tsdf, (0, pad_w, 0, pad_h, 0, pad_d))

    # Extract non-overlapping blocks
    tsdf = tsdf.unfold(2, block_size, block_size)
    tsdf = tsdf.unfold(3, block_size, block_size)
    tsdf = tsdf.unfold(4, block_size, block_size)
    blocks = tsdf.contiguous().view(-1, block_size ** 3)
    return blocks

def compress_blocks_torch(blocks, klt_basis, mean, num_components=64):
    centered = blocks - mean
    coeffs = centered @ klt_basis[:num_components].T
    return coeffs

def reconstruct_blocks_torch(coeffs, klt_basis, mean, num_components=64):
    return coeffs @ klt_basis[:num_components] + mean


def reconstruct_volume_from_blocks_torch(blocks, volume_shape=(256, 256, 256), block_size=4):
    D, H, W = volume_shape
    # Calculate number of blocks, accounting for padding
    n_blocks_d = (D + block_size - 1) // block_size
    n_blocks_h = (H + block_size - 1) // block_size
    n_blocks_w = (W + block_size - 1) // block_size

    # Reshape blocks into a grid
    blocks_reshaped = blocks.view(n_blocks_d, n_blocks_h, n_blocks_w, block_size, block_size, block_size)
    blocks_reshaped = blocks_reshaped.permute(0, 3, 1, 4, 2, 5).contiguous()

    # Reconstruct volume and trim to desired shape
    volume = blocks_reshaped.view(n_blocks_d * block_size, n_blocks_h * block_size, n_blocks_w * block_size)
    volume = volume[:D, :H, :W]
    return volume


def quantize_coeffs(coeffs, eigenvalues, K_total=256, max_iterations=100, tol=1e-4):
    """
    Quantize KLT coefficients using 1D K-means with eigenvalue-based bin allocation.

    Args:
        coeffs (torch.Tensor): Latent coefficients, shape [N, H].
        eigenvalues (torch.Tensor): Eigenvalues from KLT, shape [H].
        K_total (int): Total number of quantization bins to distribute.
        max_iterations (int): Max iterations for K-means.
        tol (float): Convergence tolerance for K-means.

    Returns:
        quantized_indices (torch.Tensor): Quantized bin indices, shape [N, H].
        bin_centers (list): List of bin centers [eta_h^k] for each dimension h.
        fixed_dims (dict): Dict of {dim: value} for dimensions with K_h = 1.
    """
    N, H = coeffs.shape
    device = coeffs.device

    # Truncate eigenvalues to match coeffs dimensions
    if eigenvalues.shape[0] > H:
        eigenvalues = eigenvalues[:H]
    elif eigenvalues.shape[0] < H:
        raise ValueError(f"Eigenvalues shape {eigenvalues.shape} does not match coeffs dimensions {H}")

    # Rest of the function remains the same
    eigenvalues = eigenvalues.clamp(min=0)
    V_total = eigenvalues.sum()
    if V_total == 0:
        raise ValueError("All eigenvalues are zero; cannot assign bins.")
    stds = torch.sqrt(eigenvalues)
    std_total = torch.sqrt(V_total)
    K_h = torch.floor(K_total * stds / std_total).long()
    K_h = torch.clamp(K_h, min=1)

    # Identify fixed dimensions (zero variance)
    fixed_dims = {}
    for h in range(H):
        if eigenvalues[h] < 1e-6:
            K_h[h] = 1
            fixed_dims[h] = coeffs[:, h].mean().item()  # Store mean as fixed value

    # Initialize bin centers and indices
    bin_centers = []
    quantized_indices = torch.zeros(N, H, dtype=torch.long, device=device)

    # Perform 1D K-means for each dimension
    for h in range(H):
        if h in fixed_dims:
            # Fixed dimension: assign all to single bin
            bin_centers.append(torch.tensor([fixed_dims[h]], device=device))
            quantized_indices[:, h] = 0
            continue

        # Get coefficients for dimension h
        X_h = coeffs[:, h]  # Shape [N]

        # Initialize K_h[h] bin centers (uniformly spaced)
        K = K_h[h].item()
        if K == 1:
            bin_centers.append(X_h.mean().reshape(1))
            quantized_indices[:, h] = 0
            continue

        min_val, max_val = X_h.min(), X_h.max()
        eta_h = torch.linspace(min_val, max_val, K, device=device)

        # Lloyd's algorithm for 1D K-means
        for _ in range(max_iterations):
            # Assign points to nearest bin
            distances = (X_h[:, None] - eta_h[None, :]) ** 2
            assignments = torch.argmin(distances, dim=1)  # Shape [N]

            # Update bin centers
            old_eta_h = eta_h.clone()
            for k in range(K):
                mask = assignments == k
                if mask.sum() > 0:
                    eta_h[k] = X_h[mask].mean()
                else:
                    # Reinitialize empty cluster
                    eta_h[k] = min_val + (max_val - min_val) * k / (K - 1)

            # Check convergence
            if torch.norm(eta_h - old_eta_h) < tol:
                break

        # Final assignment
        distances = (X_h[:, None] - eta_h[None, :]) ** 2
        quantized_indices[:, h] = torch.argmin(distances, dim=1)
        bin_centers.append(eta_h)

    return quantized_indices, bin_centers, fixed_dims


def save_quantized_coeffs(quantized_indices, bin_centers, fixed_dims, output_path):
    """
    Save quantized coefficients and metadata using zstd compression.

    Args:
        quantized_indices (torch.Tensor): Quantized bin indices, shape [N, H].
        bin_centers (list): List of bin centers for each dimension.
        fixed_dims (dict): Dict of fixed dimension values.
        output_path (str): Path to save the compressed data.
    """
    # Determine minimum integer type for indices
    max_K = max(len(centers) for centers in bin_centers)
    if max_K <= 256:
        indices_dtype = torch.uint8
    elif max_K <= 65536:
        indices_dtype = torch.int16
    else:
        indices_dtype = torch.int32

    quantized_indices = quantized_indices.to(indices_dtype)

    # Save indices with zstd compression
    indices_np = quantized_indices.cpu().numpy()
    with open(f"{output_path}_indices.zst", "wb") as f:
        f.write(zstd.compress(indices_np.tobytes()))

    # Save bin centers and fixed dims, storing each bin_centers[h] separately
    metadata = {
        f"bin_centers_{h}": centers.cpu().numpy() for h, centers in enumerate(bin_centers)
    }
    metadata["fixed_dims"] = np.array(fixed_dims, dtype=object)
    np.savez_compressed(f"{output_path}_metadata.npz", **metadata)


def decompress_coeffs(quantized_indices, bin_centers, fixed_dims, H):
    """
    Decompress quantized coefficients back to floating-point values.

    Args:
        quantized_indices (torch.Tensor): Quantized bin indices, shape [N, H].
        bin_centers (list): List of bin centers for each dimension.
        fixed_dims (dict): Dict of fixed dimension values.
        H (int): Number of latent dimensions.

    Returns:
        reconstructed (torch.Tensor): Reconstructed coefficients, shape [N, H].
    """
    N = quantized_indices.shape[0]
    device = quantized_indices.device
    reconstructed = torch.zeros(N, H, device=device)

    for h in range(H):
        if h in fixed_dims:
            reconstructed[:, h] = fixed_dims[h]
        else:
            reconstructed[:, h] = bin_centers[h][quantized_indices[:, h]]

    return reconstructed

class STEQuantize(torch.autograd.Function):
  """Straight-Through Estimator for Quantization.

  Forward pass implements quantization by rounding to integers,
  backward pass is set to gradients of the identity function.
  """
  @staticmethod
  def forward(ctx, x):
    ctx.save_for_backward(x)
    return x.round()

  @staticmethod
  def backward(ctx, grad_outputs):
    return grad_outputs

def diff_quantized_tensor(input,num_bits=8,min=-1,max=1,quant=True):
    input=torch.clamp(input,min,max)
    if True:
        quant=STEQuantize.apply
        scale=(max - min) / (2**num_bits)
        quanted_tensor=quant((input-min)/(scale))*scale+min
        return quanted_tensor
    else:
        return input

torch.cuda.empty_cache()
device = torch.device("cuda:1" if torch.cuda.is_available() else "cpu")
print(device)

# Load .npz as torch tensors
def load_tsdf_tensor(path):
    tsdf = np.load(path)["sdf"]
    tsdf = np.squeeze(tsdf)
    return torch.tensor(tsdf, dtype=torch.float32).to(device)



decoding_time = 0
#print("recon_vol", recon_vol)
#recon_vol = diff_quantized_tensor(recon_vol)
#print("recon_vol_quantized",recon_vol)

#np.save(os.path.join(output_path, "reconstructed_tsdf.npy"), recon_vol.cpu().numpy())
#np.savez_compressed(os.path.join(output_path, "reconstructed_tsdf.npz"), sdf=recon_vol.unsqueeze(-1).cpu().numpy())

tsdf_data = np.load("/home/frozzzen/Downloads/Mesh_Datasets/3D_scene/shared_3d_capture/rgbd_data/arena/arena_scene4_200/sync_tsdf/0001_tsdf.npz")  # Your path here
tsdf = tsdf_data["sdf"]  # Shape (res, res, res, 1)
tsdf = np.squeeze(tsdf)  # Now shape is (res, res, res)
print(f"TSDF shape: {tsdf.shape}")
print(f"TSDF range: {tsdf.min(), tsdf.max()}")
resolution = tsdf.shape[0]
voxel_size = 0.01953125  # Match your original voxel size
volume_length = voxel_size * resolution

start = time.time()

# Convert TSDF to mesh using marching cubes (isosurface at 0)
verts, faces, normals, _ = measure.marching_cubes(tsdf, level=0.0)

# Normalize to your scene scale if needed
voxel_size = 0.01953125
verts *= voxel_size

# Create Open3D mesh
mesh = o3d.geometry.TriangleMesh()
mesh.vertices = o3d.utility.Vector3dVector(verts)
mesh.triangles = o3d.utility.Vector3iVector(faces)
mesh.vertex_normals = o3d.utility.Vector3dVector(normals)
o3d.visualization.draw_geometries([mesh])
end = time.time()
decoding_time += end - start
#mesh_np = trimesh.Trimesh(vertices = vertices.detach().cpu().numpy(), faces=faces.detach().cpu().numpy(), process=False)
#mesh_np.export(os.path.join(output_path, f"mesh_{num_components:03}.obj"))

print("decoding time:", decoding_time)