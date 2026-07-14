import argparse
import time

import torch
import numpy as np
import os
from glob import glob
from numpy.lib.stride_tricks import sliding_window_view
from fmc import dynamic_marching_cubes, construct_voxel_grid, base_cube_edges
import trimesh
from util import compress_and_decompress_matrix
import zstd
'''
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
'''
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

def load_tsdf_tensor(path):
    tsdf = np.load(path)["sdf"]
    tsdf = np.squeeze(tsdf)
    return torch.tensor(tsdf, dtype=torch.float32).to(device)


import os, time, torch, numpy as np, trimesh, csv
from glob import glob
from fmc import dynamic_marching_cubes, construct_voxel_grid
from util import compress_and_decompress_matrix
import zstd
from tqdm import tqdm
import time


device = torch.device("cuda:0" if torch.cuda.is_available() else "cpu")

### ---------- Configuration ----------
input_path = "/mnt/datadrive/ChromeDownloads/Mesh_dataset/combined_scaled/TSDF_128/data/TSDF"
num_components = 128
block_size = 8
voxel_grid_res = 127
K_total = 2048 * 8
fps = 30
#
# 16 8 2048 *4 2.375 Mbps
# 32 8 2048 *6  4 Mbps
# 128 8 2048 * 8  7Mbps
output_root = f"/mnt/datadrive/ChromeDownloads/Mesh_dataset/combined_scaled/klt_{block_size}_{num_components}_{K_total}"

compressed_folder = os.path.join(output_root, "compressed")
reconstructed_folder = os.path.join(output_root, "reconstructed_meshes")
os.makedirs(compressed_folder, exist_ok=True)
os.makedirs(reconstructed_folder, exist_ok=True)

### ---------- Helper functions ----------
def compute_bitrate(total_bits, num_frames, fps):
    duration_sec = num_frames / fps
    return total_bits / duration_sec / 1000  # kbps

# ==== Load TSDF paths ====
tsdf_paths = sorted(glob(os.path.join(input_path, "*.npz")))
num_frames = 100
print(num_frames)
print(f"Found {num_frames} TSDF frames in {input_path}")

device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
'''
# ==== Compute KLT basis (from first few frames) ====
training_tsdfs = [load_tsdf_tensor(p) for p in tsdf_paths[1:2]]
training_blocks = extract_training_blocks_torch(training_tsdfs, block_size=block_size)
klt_basis, mean_vec = compute_klt_basis_torch(training_blocks)
del training_tsdfs, training_blocks
torch.cuda.empty_cache()

# ==== Process each frame ====
import zipfile

# ==== Process each frame ====
total_bits = 0
decode_times = []

for frame_idx, tsdf_path in enumerate(tqdm(tsdf_paths[0:100], desc="Compressing frames")):
    target_tsdf = load_tsdf_tensor(tsdf_path)
    target_blocks = get_nonoverlapping_blocks_torch(target_tsdf, block_size=block_size)

    # 1️⃣ Compress
    coeffs = compress_blocks_torch(target_blocks, klt_basis, mean_vec, num_components=num_components)
    coeffs_np = coeffs.detach().cpu().numpy()

    # 2️⃣ Quantize + save
    eigenvalues = torch.linalg.eigvalsh(coeffs.T @ coeffs / (coeffs.shape[0] - 1))
    quantized_indices, bin_centers, fixed_dims = quantize_coeffs(coeffs, eigenvalues, K_total=K_total)

    frame_name = os.path.splitext(os.path.basename(tsdf_path))[0]
    output_coeffs_path = os.path.join(compressed_folder, f"{frame_name}_quantized")
    save_quantized_coeffs(quantized_indices, bin_centers, fixed_dims, output_coeffs_path)

    # Compute theoretical bits for bitrate (before entropy coding)
    bits = quantized_indices.numel() * np.log2(K_total)  # 9 bits per symbol for K=512
    total_bits += bits

    # 3️⃣ Decode / reconstruct timing
    start_time = time.time()

    reconstructed = decompress_coeffs(quantized_indices, bin_centers, fixed_dims, H=coeffs.shape[1])
    recon_blocks = reconstruct_blocks_torch(reconstructed, klt_basis, mean_vec, num_components=num_components)
    recon_vol = reconstruct_volume_from_blocks_torch(
        recon_blocks,
        volume_shape=(target_tsdf.shape[0], target_tsdf.shape[1], target_tsdf.shape[2]),
        block_size=block_size
    )

    # 4️⃣ Marching cubes and save mesh
    x_nx3, cube_fx8 = construct_voxel_grid(target_tsdf.shape[0]-1, device)
    x_nx3 *= 2.2
    vertices, faces = dynamic_marching_cubes(x_nx3, cube_fx8, recon_vol.flatten())

    decode_time = time.time() - start_time
    decode_times.append(decode_time)

    mesh_np = trimesh.Trimesh(vertices=vertices.cpu().numpy(), faces=faces.cpu().numpy(), process=False)
    mesh_path = os.path.join(reconstructed_folder, f"mesh_{frame_name}.obj")
    mesh_np.export(mesh_path)
    print(mesh_np)
    # Free memory
    del target_tsdf, target_blocks, coeffs, reconstructed, recon_blocks, recon_vol
    torch.cuda.empty_cache()

# ==== After the loop ====
avg_decode_time = np.mean(decode_times)
fps_decode = 1.0 / avg_decode_time

# ==== Compute theoretical bitrate ====
bitrate_kbps = compute_bitrate(total_bits, num_frames, fps)

# ==== Entropy coding simulation (.zip) ====
zip_path = os.path.join(output_root, "compressed_archive.zip")
with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as zipf:
    for root, _, files in os.walk(compressed_folder):
        for file in files:
            file_path = os.path.join(root, file)
            zipf.write(file_path, arcname=os.path.relpath(file_path, compressed_folder))

zip_size_bytes = os.path.getsize(zip_path)
zip_bits = zip_size_bytes * 8
zip_bitrate_kbps = compute_bitrate(zip_bits, num_frames, fps)

# ==== Print summary ====
print(f"\n✅ Processed {num_frames} frames at {fps} FPS")
print(f"💾 Theoretical bitrate (no entropy coding): {bitrate_kbps:.2f} kbps")
print(f"📦 Entropy-coded bitrate (.zip size): {zip_bitrate_kbps:.2f} kbps")
print(f"🚀 Average decode time per frame: {avg_decode_time:.4f} s ({fps_decode:.2f} FPS)")
print(f"📁 Compressed coeffs saved in: {compressed_folder}")
print(f"📁 Zipped archive saved in: {zip_path}")
print(f"📁 Reconstructed meshes saved in: {reconstructed_folder}")
'''
import open3d as o3d
import pymeshlab
from util import select_viewpoints, evaluate_meshes, compress_folder, print_sizes, compute_bitrate, evaluate_psnr, \
    load_mesh_list, compress_file, compress_folder_7z


KLT_meshes_path = reconstructed_folder
Ground_truth_path = '/mnt/datadrive/ChromeDownloads/Mesh_dataset/combined_scaled'

out_dir = os.path.join(Ground_truth_path, "SSIM")
print(out_dir)
num_views = 4
viewpoints = [o3d.io.read_pinhole_camera_parameters(f"{out_dir}/view_{i:02d}.json") for i in range(num_views)]

(
    avg_d1_KLT, max_d1_KLT, min_d1_KLT,
    avg_d2_KLT, max_d2_KLT, min_d2_KLT
) = evaluate_psnr(Ground_truth_path, KLT_meshes_path, 10, mode="default")


print("KLT PSNR:")
print(f"  D1 Avg: {avg_d1_KLT:.3f}, Max: {max_d1_KLT:.3f}, Min: {min_d1_KLT:.3f}")
print(f"  D2 Avg: {avg_d2_KLT:.3f}, Max: {max_d2_KLT:.3f}, Min: {min_d2_KLT:.3f}\n")


# KLT metrics
SSIM_depth_KLT = []
SSIM_color_KLT = []
PSNR_depth_KLT = []
PSNR_color_KLT = []


gt_meshes = load_mesh_list(Ground_truth_path, "default")
KLT_rec_meshes = load_mesh_list(KLT_meshes_path, "default")

# ---------- Fix orientation for KLT meshes (only first time) ----------
marker_file_KLT = os.path.join(KLT_meshes_path, "orientation_fixed.txt")
if not os.path.exists(marker_file_KLT):
    print("Fixing orientation for KLT meshes...")
    for rec_file in KLT_rec_meshes:
        ms = pymeshlab.MeshSet()
        ms.load_new_mesh(rec_file)
        ms.meshing_invert_face_orientation()
        ms.save_current_mesh(rec_file)
    with open(marker_file_KLT, "w") as f:
        f.write("orientation fixed")


# ---------- KLT SSIM ----------
for i, (gt_file, rec_file) in enumerate(zip(gt_meshes[0:10], KLT_rec_meshes[0:10])):
    if i >= num_frames:
        break

    gt_mesh = o3d.io.read_triangle_mesh(gt_file)
    rec_mesh = o3d.io.read_triangle_mesh(rec_file)

    gt_mesh.compute_vertex_normals()
    rec_mesh.compute_vertex_normals()

    avg_ssim_depth, avg_ssim_color, avg_psnr_depth, avg_psnr_normal = evaluate_meshes(
        gt_mesh, rec_mesh, viewpoints,
        output_dir=os.path.join(KLT_meshes_path, "SSIM", "renderings")
    )
    SSIM_depth_KLT.append(avg_ssim_depth)
    SSIM_color_KLT.append(avg_ssim_color)
    PSNR_depth_KLT.append(avg_psnr_depth)
    PSNR_color_KLT.append(avg_psnr_normal)

print("KLT SSIM/PSNR:")
print(f"  SSIM Depth Avg: {np.mean(SSIM_depth_KLT):.4f}")
print(f"  SSIM Color Avg: {np.mean(SSIM_color_KLT):.4f}")
print(f"  PSNR Depth Avg: {np.mean(PSNR_depth_KLT):.3f}")
print(f"  PSNR Color Avg: {np.mean(PSNR_color_KLT):.3f}\n")