"""KLT-based TSDF compression and mesh reconstruction.

This script trains a KLT basis from TSDF blocks, compresses each frame with
quantized KLT coefficients, reconstructs meshes with marching cubes, and reports
bitrate / quality metrics. Defaults match the original experiment configuration.
"""

from __future__ import annotations

import argparse
import os
import time
import zipfile
from glob import glob
from pathlib import Path
from typing import Sequence

np = None
torch = None
trimesh = None
zstd = None
tqdm = None
construct_voxel_grid = None
dynamic_marching_cubes = None


def load_runtime_dependencies() -> None:
    """Import heavy runtime dependencies after CLI parsing."""
    global np, torch, trimesh, zstd, tqdm, construct_voxel_grid, dynamic_marching_cubes

    if torch is not None:
        return

    import numpy as _np
    import torch as _torch
    import trimesh as _trimesh
    import zstd as _zstd
    from tqdm import tqdm as _tqdm

    from fmc import construct_voxel_grid as _construct_voxel_grid
    from fmc import dynamic_marching_cubes as _dynamic_marching_cubes

    np = _np
    torch = _torch
    trimesh = _trimesh
    zstd = _zstd
    tqdm = _tqdm
    construct_voxel_grid = _construct_voxel_grid
    dynamic_marching_cubes = _dynamic_marching_cubes


DEFAULT_INPUT_PATH = "./TSDF"
DEFAULT_GROUND_TRUTH_PATH = "."
DEFAULT_NUM_COMPONENTS = 128
DEFAULT_BLOCK_SIZE = 8
DEFAULT_VOXEL_GRID_RES = 127
DEFAULT_K_TOTAL = 2048 * 8
DEFAULT_FPS = 30
DEFAULT_NUM_FRAMES = 100
DEFAULT_TRAINING_START = 1
DEFAULT_TRAINING_COUNT = 1
DEFAULT_EVALUATION_FRAMES = 10
DEFAULT_NUM_VIEWS = 4


def get_device(device_name: str | None = None) -> torch.device:
    """Return the requested torch device, or CUDA when available."""
    if device_name:
        return torch.device(device_name)
    return torch.device("cuda" if torch.cuda.is_available() else "cpu")


def load_tsdf_tensor(path: str, device: torch.device) -> torch.Tensor:
    """Load a TSDF volume from an ``.npz`` file containing an ``sdf`` array."""
    tsdf = np.load(path)["sdf"]
    return torch.tensor(np.squeeze(tsdf), dtype=torch.float32, device=device)


def extract_training_blocks_torch(
    tsdf_volumes: Sequence[torch.Tensor],
    block_size: int = 4,
    verbose: bool = False,
) -> torch.Tensor:
    """Extract overlapping sliding-window blocks from TSDF volumes."""
    blocks = []
    for tsdf in tsdf_volumes:
        tsdf = tsdf.unsqueeze(0).unsqueeze(0)  # (1, 1, D, H, W)

        if verbose:
            unfolded_d = tsdf.unfold(2, block_size, 1)
            unfolded_dh = unfolded_d.unfold(3, block_size, 1)
            unfolded_dhw = unfolded_dh.unfold(4, block_size, 1)
            print("unfold 2: ", unfolded_d.shape)
            print("unfold 3: ", unfolded_dh.shape)
            print("unfold 4: ", unfolded_dhw.shape)
            windows = unfolded_dhw
        else:
            windows = tsdf.unfold(2, block_size, 1).unfold(3, block_size, 1).unfold(4, block_size, 1)

        blocks.append(windows.contiguous().view(-1, block_size**3))

    return torch.cat(blocks, dim=0)


def compute_klt_basis_torch(blocks: torch.Tensor, verbose: bool = True) -> tuple[torch.Tensor, torch.Tensor]:
    """Compute the KLT/PCA basis and mean vector for flattened TSDF blocks."""
    mean = blocks.mean(dim=0, keepdim=True)
    centered = blocks - mean
    _, singular_values, v_h = torch.linalg.svd(centered, full_matrices=False)

    if verbose:
        print("Singular values:", singular_values[:20])

    return v_h, mean


def get_nonoverlapping_blocks_torch(tsdf: torch.Tensor, block_size: int = 4) -> torch.Tensor:
    """Split a TSDF volume into non-overlapping blocks, padding the end if needed."""
    tsdf = tsdf.unsqueeze(0).unsqueeze(0)  # (1, 1, D, H, W)
    depth, height, width = tsdf.shape[2:]

    pad_d = (block_size - (depth % block_size)) % block_size
    pad_h = (block_size - (height % block_size)) % block_size
    pad_w = (block_size - (width % block_size)) % block_size
    tsdf = torch.nn.functional.pad(tsdf, (0, pad_w, 0, pad_h, 0, pad_d))

    blocks = tsdf.unfold(2, block_size, block_size)
    blocks = blocks.unfold(3, block_size, block_size)
    blocks = blocks.unfold(4, block_size, block_size)
    return blocks.contiguous().view(-1, block_size**3)


def compress_blocks_torch(
    blocks: torch.Tensor,
    klt_basis: torch.Tensor,
    mean: torch.Tensor,
    num_components: int = 64,
) -> torch.Tensor:
    """Project centered TSDF blocks onto the first KLT basis vectors."""
    centered = blocks - mean
    return centered @ klt_basis[:num_components].T


def reconstruct_blocks_torch(
    coeffs: torch.Tensor,
    klt_basis: torch.Tensor,
    mean: torch.Tensor,
    num_components: int = 64,
) -> torch.Tensor:
    """Reconstruct flattened TSDF blocks from KLT coefficients."""
    return coeffs @ klt_basis[:num_components] + mean


def reconstruct_volume_from_blocks_torch(
    blocks: torch.Tensor,
    volume_shape: tuple[int, int, int] = (256, 256, 256),
    block_size: int = 4,
) -> torch.Tensor:
    """Reassemble non-overlapping flattened blocks into a TSDF volume."""
    depth, height, width = volume_shape
    n_blocks_d = (depth + block_size - 1) // block_size
    n_blocks_h = (height + block_size - 1) // block_size
    n_blocks_w = (width + block_size - 1) // block_size

    blocks = blocks.view(n_blocks_d, n_blocks_h, n_blocks_w, block_size, block_size, block_size)
    blocks = blocks.permute(0, 3, 1, 4, 2, 5).contiguous()
    volume = blocks.view(n_blocks_d * block_size, n_blocks_h * block_size, n_blocks_w * block_size)
    return volume[:depth, :height, :width]


def quantize_coeffs(
    coeffs: torch.Tensor,
    eigenvalues: torch.Tensor,
    k_total: int = 256,
    max_iterations: int = 100,
    tol: float = 1e-4,
) -> tuple[torch.Tensor, list[torch.Tensor], dict[int, float]]:
    """Quantize KLT coefficients with per-dimension 1D K-means."""
    num_coeffs, num_dims = coeffs.shape
    device = coeffs.device

    if eigenvalues.shape[0] > num_dims:
        eigenvalues = eigenvalues[:num_dims]
    elif eigenvalues.shape[0] < num_dims:
        raise ValueError(f"Eigenvalues shape {eigenvalues.shape} does not match coeffs dimensions {num_dims}")

    eigenvalues = eigenvalues.clamp(min=0)
    variance_total = eigenvalues.sum()
    if variance_total == 0:
        raise ValueError("All eigenvalues are zero; cannot assign bins.")

    stds = torch.sqrt(eigenvalues)
    std_total = torch.sqrt(variance_total)
    bins_per_dim = torch.floor(k_total * stds / std_total).long()
    bins_per_dim = torch.clamp(bins_per_dim, min=1)

    fixed_dims: dict[int, float] = {}
    for dim in range(num_dims):
        if eigenvalues[dim] < 1e-6:
            bins_per_dim[dim] = 1
            fixed_dims[dim] = coeffs[:, dim].mean().item()

    bin_centers: list[torch.Tensor] = []
    quantized_indices = torch.zeros(num_coeffs, num_dims, dtype=torch.long, device=device)

    for dim in range(num_dims):
        if dim in fixed_dims:
            bin_centers.append(torch.tensor([fixed_dims[dim]], device=device))
            quantized_indices[:, dim] = 0
            continue

        coeffs_dim = coeffs[:, dim]
        num_bins = bins_per_dim[dim].item()
        if num_bins == 1:
            bin_centers.append(coeffs_dim.mean().reshape(1))
            quantized_indices[:, dim] = 0
            continue

        min_val, max_val = coeffs_dim.min(), coeffs_dim.max()
        centers = torch.linspace(min_val, max_val, num_bins, device=device)

        for _ in range(max_iterations):
            distances = (coeffs_dim[:, None] - centers[None, :]) ** 2
            assignments = torch.argmin(distances, dim=1)

            old_centers = centers.clone()
            for bin_idx in range(num_bins):
                mask = assignments == bin_idx
                if mask.sum() > 0:
                    centers[bin_idx] = coeffs_dim[mask].mean()
                else:
                    centers[bin_idx] = min_val + (max_val - min_val) * bin_idx / (num_bins - 1)

            if torch.norm(centers - old_centers) < tol:
                break

        distances = (coeffs_dim[:, None] - centers[None, :]) ** 2
        quantized_indices[:, dim] = torch.argmin(distances, dim=1)
        bin_centers.append(centers)

    return quantized_indices, bin_centers, fixed_dims


def _minimum_index_dtype(max_bins: int) -> torch.dtype:
    """Choose a compact torch dtype that can store indices up to ``max_bins - 1``."""
    if max_bins <= 256:
        return torch.uint8
    if max_bins <= 32768:
        return torch.int16
    return torch.int32


def save_quantized_coeffs(
    quantized_indices: torch.Tensor,
    bin_centers: Sequence[torch.Tensor],
    fixed_dims: dict[int, float],
    output_path: str,
) -> None:
    """Save quantized indices and metadata with zstd / npz compression."""
    max_bins = max(len(centers) for centers in bin_centers)
    quantized_indices = quantized_indices.to(_minimum_index_dtype(max_bins))

    indices_np = quantized_indices.cpu().numpy()
    with open(f"{output_path}_indices.zst", "wb") as file:
        file.write(zstd.compress(indices_np.tobytes()))

    metadata = {f"bin_centers_{idx}": centers.cpu().numpy() for idx, centers in enumerate(bin_centers)}
    metadata["fixed_dims"] = np.array(fixed_dims, dtype=object)
    np.savez_compressed(f"{output_path}_metadata.npz", **metadata)


def decompress_coeffs(
    quantized_indices: torch.Tensor,
    bin_centers: Sequence[torch.Tensor],
    fixed_dims: dict[int, float],
    num_dims: int,
) -> torch.Tensor:
    """Map quantized coefficient indices back to floating-point bin centers."""
    num_coeffs = quantized_indices.shape[0]
    reconstructed = torch.zeros(num_coeffs, num_dims, device=quantized_indices.device)

    for dim in range(num_dims):
        if dim in fixed_dims:
            reconstructed[:, dim] = fixed_dims[dim]
        else:
            reconstructed[:, dim] = bin_centers[dim][quantized_indices[:, dim]]

    return reconstructed


def ste_round(tensor: torch.Tensor) -> torch.Tensor:
    """Round with a straight-through estimator."""

    class _STEQuantize(torch.autograd.Function):
        @staticmethod
        def forward(ctx, x: torch.Tensor) -> torch.Tensor:  # noqa: D401 - PyTorch API signature.
            ctx.save_for_backward(x)
            return x.round()

        @staticmethod
        def backward(ctx, grad_outputs: torch.Tensor) -> torch.Tensor:  # noqa: D401 - PyTorch API signature.
            return grad_outputs

    return _STEQuantize.apply(tensor)


def diff_quantized_tensor(
    tensor: torch.Tensor,
    num_bits: int = 8,
    min_value: float = -1,
    max_value: float = 1,
    quant: bool = True,
) -> torch.Tensor:
    """Differentiably quantize a tensor with a straight-through estimator."""
    tensor = torch.clamp(tensor, min_value, max_value)
    if not quant:
        return tensor

    scale = (max_value - min_value) / (2**num_bits)
    return ste_round((tensor - min_value) / scale) * scale + min_value


def compute_bitrate(total_bits: float, num_frames: int, fps: int) -> float:
    """Compute bitrate in kbps for a fixed-frame-rate sequence."""
    duration_sec = num_frames / fps
    return total_bits / duration_sec / 1000


def clear_cuda_cache() -> None:
    """Release cached CUDA memory when CUDA is active."""
    if torch.cuda.is_available():
        torch.cuda.empty_cache()


def build_default_output_root(block_size: int, num_components: int) -> str:
    """Return the original experiment output path for the selected configuration."""
    return (
        "/media/frozzzen/DataDrive/ChromeDownloads/Mesh_dataset/combined_scaled/"
        f"klt_{block_size}_{num_components}"
    )


def train_klt_basis(
    tsdf_paths: Sequence[str],
    training_start: int,
    training_count: int,
    block_size: int,
    device: torch.device,
    verbose: bool,
) -> tuple[torch.Tensor, torch.Tensor]:
    """Load training frames and compute their KLT basis."""
    training_paths = tsdf_paths[training_start : training_start + training_count]
    if not training_paths:
        raise ValueError("No TSDF frames selected for KLT training.")

    training_tsdfs = [load_tsdf_tensor(path, device) for path in training_paths]
    training_blocks = extract_training_blocks_torch(training_tsdfs, block_size=block_size, verbose=verbose)
    klt_basis, mean_vec = compute_klt_basis_torch(training_blocks, verbose=verbose)

    del training_tsdfs, training_blocks
    clear_cuda_cache()
    return klt_basis, mean_vec


def compress_and_reconstruct_frames(
    tsdf_paths: Sequence[str],
    compressed_folder: str,
    reconstructed_folder: str,
    klt_basis: torch.Tensor,
    mean_vec: torch.Tensor,
    block_size: int,
    num_components: int,
    k_total: int,
    device: torch.device,
) -> tuple[float, list[float]]:
    """Compress TSDF frames, reconstruct meshes, and return bit/decode metrics."""
    total_bits = 0.0
    decode_times: list[float] = []

    for tsdf_path in tqdm(tsdf_paths, desc="Compressing frames"):
        target_tsdf = load_tsdf_tensor(tsdf_path, device)
        target_blocks = get_nonoverlapping_blocks_torch(target_tsdf, block_size=block_size)

        coeffs = compress_blocks_torch(target_blocks, klt_basis, mean_vec, num_components=num_components)
        eigenvalues = torch.linalg.eigvalsh(coeffs.T @ coeffs / (coeffs.shape[0] - 1))
        quantized_indices, bin_centers, fixed_dims = quantize_coeffs(coeffs, eigenvalues, k_total=k_total)

        frame_name = Path(tsdf_path).stem
        output_coeffs_path = os.path.join(compressed_folder, f"{frame_name}_quantized")
        save_quantized_coeffs(quantized_indices, bin_centers, fixed_dims, output_coeffs_path)

        total_bits += quantized_indices.numel() * np.log2(k_total)

        start_time = time.time()
        reconstructed = decompress_coeffs(quantized_indices, bin_centers, fixed_dims, num_dims=coeffs.shape[1])
        recon_blocks = reconstruct_blocks_torch(reconstructed, klt_basis, mean_vec, num_components=num_components)
        recon_vol = reconstruct_volume_from_blocks_torch(
            recon_blocks,
            volume_shape=(target_tsdf.shape[0], target_tsdf.shape[1], target_tsdf.shape[2]),
            block_size=block_size,
        )

        x_nx3, cube_fx8 = construct_voxel_grid(target_tsdf.shape[0] - 1, device)
        x_nx3 *= 2
        vertices, faces = dynamic_marching_cubes(x_nx3, cube_fx8, recon_vol.flatten())

        decode_time = time.time() - start_time
        decode_times.append(decode_time)

        mesh = trimesh.Trimesh(vertices=vertices.cpu().numpy(), faces=faces.cpu().numpy(), process=False)
        mesh_path = os.path.join(reconstructed_folder, f"mesh_{frame_name}.obj")
        mesh.export(mesh_path)
        print(mesh)

        del target_tsdf, target_blocks, coeffs, reconstructed, recon_blocks, recon_vol
        clear_cuda_cache()

    return total_bits, decode_times


def zip_compressed_folder(compressed_folder: str, output_root: str) -> str:
    """Create the entropy-coding simulation zip archive."""
    zip_path = os.path.join(output_root, "compressed_archive.zip")
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as zip_file:
        for root, _, files in os.walk(compressed_folder):
            for filename in files:
                file_path = os.path.join(root, filename)
                zip_file.write(file_path, arcname=os.path.relpath(file_path, compressed_folder))
    return zip_path


def print_compression_summary(
    num_frames: int,
    fps: int,
    total_bits: float,
    decode_times: Sequence[float],
    compressed_folder: str,
    reconstructed_folder: str,
    zip_path: str,
) -> None:
    """Print bitrate, decode speed, and output locations."""
    avg_decode_time = float(np.mean(decode_times)) if decode_times else 0.0
    fps_decode = 1.0 / avg_decode_time if avg_decode_time > 0 else 0.0
    bitrate_kbps = compute_bitrate(total_bits, num_frames, fps)
    zip_bits = os.path.getsize(zip_path) * 8
    zip_bitrate_kbps = compute_bitrate(zip_bits, num_frames, fps)

    print(f"\nProcessed {num_frames} frames at {fps} FPS")
    print(f"Theoretical bitrate (no entropy coding): {bitrate_kbps:.2f} kbps")
    print(f"Entropy-coded bitrate (.zip size): {zip_bitrate_kbps:.2f} kbps")
    print(f"Average decode time per frame: {avg_decode_time:.4f} s ({fps_decode:.2f} FPS)")
    print(f"Compressed coeffs saved in: {compressed_folder}")
    print(f"Zipped archive saved in: {zip_path}")
    print(f"Reconstructed meshes saved in: {reconstructed_folder}")


def fix_mesh_orientation_once(mesh_paths: Sequence[str], marker_file: str) -> None:
    """Invert reconstructed mesh face orientation once, matching the original script."""
    if os.path.exists(marker_file):
        return

    import pymeshlab

    print("Fixing orientation for KLT meshes...")
    for mesh_path in mesh_paths:
        mesh_set = pymeshlab.MeshSet()
        mesh_set.load_new_mesh(mesh_path)
        mesh_set.meshing_invert_face_orientation()
        mesh_set.save_current_mesh(mesh_path)

    with open(marker_file, "w", encoding="utf-8") as file:
        file.write("orientation fixed")


def evaluate_reconstructed_meshes(
    ground_truth_path: str,
    reconstructed_folder: str,
    num_frames: int,
    evaluation_frames: int,
    num_views: int,
) -> None:
    """Run PSNR and SSIM evaluation for reconstructed meshes."""
    import open3d as o3d

    from util import evaluate_meshes, evaluate_psnr, load_mesh_list

    out_dir = os.path.join(ground_truth_path, "SSIM")
    print(out_dir)
    viewpoints = [o3d.io.read_pinhole_camera_parameters(f"{out_dir}/view_{idx:02d}.json") for idx in range(num_views)]

    avg_d1, max_d1, min_d1, avg_d2, max_d2, min_d2 = evaluate_psnr(
        ground_truth_path,
        reconstructed_folder,
        evaluation_frames,
        mode="default",
    )

    print("KLT PSNR:")
    print(f"  D1 Avg: {avg_d1:.3f}, Max: {max_d1:.3f}, Min: {min_d1:.3f}")
    print(f"  D2 Avg: {avg_d2:.3f}, Max: {max_d2:.3f}, Min: {min_d2:.3f}\n")

    gt_meshes = load_mesh_list(ground_truth_path, "default")
    reconstructed_meshes = load_mesh_list(reconstructed_folder, "default")
    fix_mesh_orientation_once(
        reconstructed_meshes,
        marker_file=os.path.join(reconstructed_folder, "orientation_fixed.txt"),
    )

    ssim_depth = []
    ssim_color = []
    psnr_depth = []
    psnr_color = []

    for idx, (gt_file, rec_file) in enumerate(zip(gt_meshes[:evaluation_frames], reconstructed_meshes[:evaluation_frames])):
        if idx >= num_frames:
            break

        gt_mesh = o3d.io.read_triangle_mesh(gt_file)
        rec_mesh = o3d.io.read_triangle_mesh(rec_file)
        gt_mesh.compute_vertex_normals()
        rec_mesh.compute_vertex_normals()

        avg_ssim_depth, avg_ssim_color, avg_psnr_depth, avg_psnr_normal = evaluate_meshes(
            gt_mesh,
            rec_mesh,
            viewpoints,
            output_dir=os.path.join(reconstructed_folder, "SSIM", "renderings"),
        )
        ssim_depth.append(avg_ssim_depth)
        ssim_color.append(avg_ssim_color)
        psnr_depth.append(avg_psnr_depth)
        psnr_color.append(avg_psnr_normal)

    print("KLT SSIM/PSNR:")
    print(f"  SSIM Depth Avg: {np.mean(ssim_depth):.4f}")
    print(f"  SSIM Color Avg: {np.mean(ssim_color):.4f}")
    print(f"  PSNR Depth Avg: {np.mean(psnr_depth):.3f}")
    print(f"  PSNR Color Avg: {np.mean(psnr_color):.3f}\n")


def parse_args() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(description="Use KLT to compress TSDF volumes and reconstruct meshes.")
    parser.add_argument("--input-path", default=DEFAULT_INPUT_PATH, help="Directory containing TSDF .npz files.")
    parser.add_argument(
        "--output-root",
        default=None,
        help="Output directory. Defaults to the original klt_<block>_<components> path.",
    )
    parser.add_argument(
        "--ground-truth-path",
        default=DEFAULT_GROUND_TRUTH_PATH,
        help="Ground-truth mesh dataset path for evaluation.",
    )
    parser.add_argument("--num-components", type=int, default=DEFAULT_NUM_COMPONENTS, help="Number of KLT basis vectors to keep.")
    parser.add_argument("--block-size", type=int, default=DEFAULT_BLOCK_SIZE, help="Cubic TSDF block side length.")
    parser.add_argument(
        "--voxel-grid-res",
        type=int,
        default=DEFAULT_VOXEL_GRID_RES,
        help="Kept for experiment compatibility; inferred from TSDF shape during reconstruction.",
    )
    parser.add_argument("--k-total", type=int, default=DEFAULT_K_TOTAL, help="Total quantization-bin budget.")
    parser.add_argument("--fps", type=int, default=DEFAULT_FPS, help="Source frame rate for bitrate reporting.")
    parser.add_argument("--num-frames", type=int, default=DEFAULT_NUM_FRAMES, help="Number of TSDF frames to process.")
    parser.add_argument(
        "--training-start",
        type=int,
        default=DEFAULT_TRAINING_START,
        help="First sorted TSDF index used for KLT training.",
    )
    parser.add_argument(
        "--training-count",
        type=int,
        default=DEFAULT_TRAINING_COUNT,
        help="Number of TSDF frames used for KLT training.",
    )
    parser.add_argument(
        "--evaluation-frames",
        type=int,
        default=DEFAULT_EVALUATION_FRAMES,
        help="Number of reconstructed meshes to evaluate.",
    )
    parser.add_argument("--num-views", type=int, default=DEFAULT_NUM_VIEWS, help="Number of SSIM camera views to load.")
    parser.add_argument("--device", default=None, help="Torch device, for example 'cuda', 'cuda:0', or 'cpu'.")
    parser.add_argument("--skip-evaluation", action="store_true", help="Skip PSNR and SSIM mesh evaluation.")
    parser.add_argument("--quiet-training", action="store_true", help="Suppress training unfold-shape and singular-value logs.")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    load_runtime_dependencies()
    device = get_device(args.device)

    output_root = args.output_root or build_default_output_root(args.block_size, args.num_components)
    compressed_folder = os.path.join(output_root, "compressed")
    reconstructed_folder = os.path.join(output_root, "reconstructed_meshes")
    os.makedirs(compressed_folder, exist_ok=True)
    os.makedirs(reconstructed_folder, exist_ok=True)

    tsdf_paths = sorted(glob(os.path.join(args.input_path, "*.npz")))
    selected_paths = tsdf_paths[: args.num_frames]
    num_frames = len(selected_paths)
    if num_frames == 0:
        raise FileNotFoundError(f"No TSDF .npz files found in {args.input_path}")

    print(num_frames)
    print(f"Found {num_frames} TSDF frames in {args.input_path}")

    klt_basis, mean_vec = train_klt_basis(
        tsdf_paths,
        training_start=args.training_start,
        training_count=args.training_count,
        block_size=args.block_size,
        device=device,
        verbose=not args.quiet_training,
    )

    total_bits, decode_times = compress_and_reconstruct_frames(
        selected_paths,
        compressed_folder=compressed_folder,
        reconstructed_folder=reconstructed_folder,
        klt_basis=klt_basis,
        mean_vec=mean_vec,
        block_size=args.block_size,
        num_components=args.num_components,
        k_total=args.k_total,
        device=device,
    )

    zip_path = zip_compressed_folder(compressed_folder, output_root)
    print_compression_summary(
        num_frames=num_frames,
        fps=args.fps,
        total_bits=total_bits,
        decode_times=decode_times,
        compressed_folder=compressed_folder,
        reconstructed_folder=reconstructed_folder,
        zip_path=zip_path,
    )

    if not args.skip_evaluation:
        evaluate_reconstructed_meshes(
            ground_truth_path=args.ground_truth_path,
            reconstructed_folder=reconstructed_folder,
            num_frames=num_frames,
            evaluation_frames=args.evaluation_frames,
            num_views=args.num_views,
        )


if __name__ == "__main__":
    main()
