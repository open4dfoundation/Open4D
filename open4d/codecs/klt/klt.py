"""KLT (Karhunen-Loeve Transform) baseline for TSDF mesh-sequence compression.

Extracted from N4MC into a self-contained Open4D module. The pipeline learns a
KLT basis from a small set of training TSDF volumes, projects non-overlapping
voxel blocks of each target frame onto a truncated basis, quantizes the
coefficients with eigenvalue-weighted 1D k-means, entropy-codes them, and
reconstructs meshes via marching cubes.

Example
-------
    python klt.py \
        --input_path /data/TSDF \
        --output_path outputs/klt_run \
        --num_components 128 --block_size 8 --voxel_grid_res 127 \
        --k_total 16384 --training_frames 1 2 --num_frames 100

Add ``--evaluate --gt_path /data/combined_scaled`` to run D1/D2 PSNR and
SSIM against the ground-truth meshes (requires the helper viewpoint files).
"""

import argparse
from collections.abc import Mapping
from io import BytesIO
import math
import os
import time
import zipfile
from glob import glob

import numpy as np
import torch
import trimesh
import zstd
import zstandard
from tqdm import tqdm

try:
    from .fmc import dynamic_marching_cubes, construct_voxel_grid
except ImportError:  # Direct ``python klt.py`` execution.
    from fmc import dynamic_marching_cubes, construct_voxel_grid


device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
_MAX_VOXELS = 256 ** 3
_MAX_INDEX_BYTES = 128 * 1024 * 1024
_MAX_CONTEXT_BYTES = 16 * 1024 * 1024
_MAX_METADATA_BYTES = 16 * 1024 * 1024


def validate_decoder_context(context):
    if (not isinstance(context, Mapping) or set(context) != {
            "version", "basis", "mean", "block_size", "num_components"}
            or type(context["version"]) is not int or context["version"] != 1):
        raise ValueError("invalid KLT decoder context/version")
    block, count = context["block_size"], context["num_components"]
    if (type(block) is not int or not 1 <= block <= 16
            or type(count) is not int or not 1 <= count <= min(block ** 3, 512)):
        raise ValueError("KLT decoder dimensions outside limits")
    for name, shape in (("basis", (count, block ** 3)), ("mean", (1, block ** 3))):
        value = context[name]
        if (not isinstance(value, torch.Tensor) or value.layout != torch.strided
                or value.dtype != torch.float32 or tuple(value.shape) != shape
                or not torch.isfinite(value).all()):
            raise ValueError(f"invalid KLT decoder {name}")


def _quantizer_metadata(path):
    """Read bounded, non-object NPY members before NumPy allocates their arrays."""
    if os.path.getsize(path) > _MAX_METADATA_BYTES:
        raise ValueError("KLT quantizer file outside limits")
    result = {}
    with zipfile.ZipFile(path) as archive:
        members = archive.infolist()
        if (len(members) > 517 or sum(member.file_size for member in members) > _MAX_METADATA_BYTES
                or len({member.filename for member in members}) != len(members)):
            raise ValueError("KLT quantizer metadata outside limits")
        for member in members:
            name = member.filename
            if not name.endswith(".npy") or "/" in name or "\\" in name:
                raise ValueError("invalid KLT quantizer member")
            data = archive.read(member)
            stream = BytesIO(data)
            version = np.lib.format.read_magic(stream)
            readers = {(1, 0): np.lib.format.read_array_header_1_0,
                       (2, 0): np.lib.format.read_array_header_2_0}
            if version not in readers:
                raise ValueError("unsupported KLT NPY version")
            shape, _, dtype = readers[version](stream, max_header_size=4096)
            if (dtype.hasobject or dtype.kind not in "iufUS"
                    or math.prod(shape) * dtype.itemsize != len(data) - stream.tell()):
                raise ValueError("invalid KLT NPY payload size/type")
            result[name[:-4]] = np.load(BytesIO(data), allow_pickle=False, max_header_size=4096)
    return result


def extract_training_blocks_torch(tsdf_volumes, block_size=4):
    """
    Extract overlapping blocks by sliding-window.
    Input: list of TSDF tensors, each shape (D, H, W)
    """
    blocks = []
    for tsdf in tsdf_volumes:
        tsdf = tsdf.unsqueeze(0).unsqueeze(0)  # shape: (1,1,D,H,W)
        windows = tsdf.unfold(2, block_size, 1).unfold(3, block_size, 1).unfold(4, block_size, 1)
        # shape: (1,1,D',H',W',B,B,B)
        windows = windows.contiguous().view(-1, block_size**3)
        blocks.append(windows)
    return torch.cat(blocks, dim=0)


def compute_klt_basis_torch(blocks):
    mean = blocks.mean(dim=0, keepdim=True)
    centered = blocks - mean
    U, S, Vh = torch.linalg.svd(centered, full_matrices=False)
    print("Singular values:", S[:20])
    return Vh, mean  # P, mu


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
        K_h = torch.ones_like(eigenvalues, dtype=torch.long)
    else:
        stds = torch.sqrt(eigenvalues)
        std_total = torch.sqrt(V_total)
        K_h = torch.floor(K_total * stds / std_total).long().clamp(min=1)

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


def save_quantized_coeffs(
    quantized_indices, bin_centers, fixed_dims, output_path, volume_shape
):
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
        indices_dtype = np.uint8
    elif max_K <= 65536:
        indices_dtype = np.uint16
    else:
        indices_dtype = np.uint32

    # Save indices with zstd compression
    indices_np = quantized_indices.cpu().numpy().astype(indices_dtype)
    with open(f"{output_path}_indices.zst", "wb") as f:
        f.write(zstd.compress(indices_np.tobytes()))

    # Save bin centers and fixed dims, storing each bin_centers[h] separately
    metadata = {
        f"bin_centers_{h}": centers.cpu().numpy() for h, centers in enumerate(bin_centers)
    }
    metadata["fixed_indices"] = np.array(sorted(fixed_dims), dtype=np.int64)
    metadata["fixed_values"] = np.array(
        [fixed_dims[index] for index in sorted(fixed_dims)], dtype=np.float32
    )
    metadata["indices_shape"] = np.array(indices_np.shape, dtype=np.int64)
    metadata["indices_dtype"] = np.array(indices_np.dtype.str)
    metadata["volume_shape"] = np.array(volume_shape, dtype=np.int64)
    np.savez_compressed(f"{output_path}_metadata.npz", **metadata)


def load_quantized_coeffs(output_path, target_device=device, *, decoder=None):
    metadata = _quantizer_metadata(f"{output_path}_metadata.npz")
    for name, dimensions in (("indices_shape", 2), ("volume_shape", 3)):
        value = metadata.get(name)
        if (not isinstance(value, np.ndarray) or value.dtype != np.int64
                or value.shape != (dimensions,) or np.any(value <= 0)):
            raise ValueError(f"invalid KLT {name}")
    shape = tuple(int(value) for value in metadata["indices_shape"])
    volume_shape = tuple(int(value) for value in metadata["volume_shape"])
    if (not 1 <= shape[1] <= 512 or len(set(volume_shape)) != 1
            or not 2 <= volume_shape[0] <= 256 or math.prod(volume_shape) > _MAX_VOXELS):
        raise ValueError("KLT frame dimensions outside limits")
    value = metadata.get("indices_dtype")
    if not isinstance(value, np.ndarray) or value.shape != () or value.dtype.kind not in "US":
        raise ValueError("invalid KLT index dtype")
    dtype = np.dtype(str(value))
    if dtype not in (np.dtype("uint8"), np.dtype("uint16"), np.dtype("uint32")):
        raise ValueError("unsupported KLT index dtype")
    expected = math.prod(shape) * dtype.itemsize
    if expected > _MAX_INDEX_BYTES:
        raise ValueError("KLT coefficient storage outside limits")
    if decoder is not None:
        block = decoder["block_size"]
        blocks = math.prod((axis + block - 1) // block for axis in volume_shape)
        if shape != (blocks, decoder["num_components"]):
            raise ValueError("KLT coefficient dimensions disagree with decoder/grid")
    required = {"indices_shape", "indices_dtype", "volume_shape", "fixed_indices", "fixed_values",
                *(f"bin_centers_{h}" for h in range(shape[1]))}
    if set(metadata) != required:
        raise ValueError("unexpected KLT quantizer metadata")
    arrays = [metadata[f"bin_centers_{h}"] for h in range(shape[1])]
    if any(value.dtype != np.float32 or value.ndim != 1 or not 0 < len(value) <= 65536
           or not np.isfinite(value).all() for value in arrays):
        raise ValueError("invalid KLT quantizer centers")
    fixed, fixed_values = metadata["fixed_indices"], metadata["fixed_values"]
    if (fixed.dtype != np.int64 or fixed.ndim != 1 or len(fixed) > shape[1]
            or np.any(fixed < 0) or np.any(fixed >= shape[1]) or len(set(fixed.tolist())) != len(fixed)
            or fixed_values.dtype != np.float32 or fixed_values.shape != fixed.shape
            or not np.isfinite(fixed_values).all()):
        raise ValueError("invalid KLT fixed coefficients")
    with open(f"{output_path}_indices.zst", "rb") as stream:
        data = stream.read(_MAX_INDEX_BYTES + 1)
    if len(data) > _MAX_INDEX_BYTES:
        raise ValueError("KLT compressed coefficients outside limits")
    try:
        declared = zstandard.frame_content_size(data)
        if declared not in (expected, zstandard.CONTENTSIZE_UNKNOWN):
            raise ValueError("KLT coefficient frame size disagrees with quantizer")
        decoded = zstandard.ZstdDecompressor(max_window_size=_MAX_INDEX_BYTES // 1024).decompress(
            data, max_output_size=expected, allow_extra_data=False
        )
    except zstandard.ZstdError as error:
        raise ValueError(f"invalid KLT coefficient stream: {error}") from error
    if len(decoded) != expected:
        raise ValueError("KLT coefficient output size disagrees with quantizer")
    values = np.frombuffer(decoded, dtype=dtype).reshape(shape)
    if any(np.any(values[:, h] >= len(centers)) for h, centers in enumerate(arrays)):
        raise ValueError("KLT coefficient index outside quantizer")
    centers = [torch.from_numpy(value.copy()).to(target_device) for value in arrays]
    fixed_dims = dict(zip(fixed.tolist(), fixed_values.tolist()))
    return torch.from_numpy(values.astype(np.int64)).to(target_device), centers, fixed_dims, volume_shape


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


def diff_quantized_tensor(input, num_bits=8, min=-1, max=1, quant=True):
    input = torch.clamp(input, min, max)
    quant = STEQuantize.apply
    scale = (max - min) / (2 ** num_bits)
    quanted_tensor = quant((input - min) / scale) * scale + min
    return quanted_tensor


def load_tsdf_tensor(path):
    tsdf = np.load(path)["sdf"]
    tsdf = np.squeeze(tsdf)
    return torch.tensor(tsdf, dtype=torch.float32).to(device)


def compute_bitrate(total_bits, num_frames, fps):
    duration_sec = num_frames / fps
    return total_bits / duration_sec / 1000  # kbps


def decode_compressed(input_path, output_path, target_device=None):
    """Decode saved KLT coefficients without access to encoder-side tensors."""
    target_device = torch.device(target_device or device)
    input_path, output_path = os.fspath(input_path), os.fspath(output_path)
    context_path = os.path.join(input_path, "decoder_context.pt")
    if os.path.getsize(context_path) > _MAX_CONTEXT_BYTES:
        raise ValueError("KLT decoder context outside limits")
    decoder = torch.load(
        context_path,
        map_location=target_device,
        weights_only=True,
    )
    validate_decoder_context(decoder)
    os.makedirs(output_path, exist_ok=True)
    metadata_paths = sorted(glob(os.path.join(input_path, "*_quantized_metadata.npz")))
    if not metadata_paths:
        raise FileNotFoundError(f"No quantized KLT frames found in {input_path}")
    outputs = []
    for metadata_path in metadata_paths:
        prefix = metadata_path.removesuffix("_metadata.npz")
        indices, centers, fixed, volume_shape = load_quantized_coeffs(
            prefix, target_device, decoder=decoder
        )
        coefficients = decompress_coeffs(
            indices, centers, fixed, H=indices.shape[1]
        )
        blocks = reconstruct_blocks_torch(
            coefficients, decoder["basis"].to(target_device),
            decoder["mean"].to(target_device), decoder["num_components"],
        )
        volume = reconstruct_volume_from_blocks_torch(
            blocks, volume_shape, decoder["block_size"]
        )
        grid, cubes = construct_voxel_grid(volume_shape[0] - 1, target_device)
        vertices, faces = dynamic_marching_cubes(
            grid * 2.2, cubes, volume.flatten()
        )
        frame_name = os.path.basename(prefix).removesuffix("_quantized")
        destination = os.path.join(output_path, f"mesh_{frame_name}.obj")
        trimesh.Trimesh(
            vertices=vertices.cpu().numpy(), faces=faces.cpu().numpy(), process=False
        ).export(destination)
        outputs.append(destination)
    return outputs


def run_compression(args, *, verify_decode=True):
    """Learn a KLT basis, then compress + reconstruct each frame."""
    compressed_folder = os.path.join(args.output_path, "compressed")
    reconstructed_folder = os.path.join(args.output_path, "reconstructed_meshes")
    os.makedirs(compressed_folder, exist_ok=True)
    if verify_decode:
        os.makedirs(reconstructed_folder, exist_ok=True)

    tsdf_paths = sorted(glob(os.path.join(args.input_path, "*.npz")))
    if not tsdf_paths:
        raise FileNotFoundError(f"No .npz TSDF frames found in {args.input_path}")
    num_frames = min(args.num_frames, len(tsdf_paths))
    print(f"Found {len(tsdf_paths)} TSDF frames in {args.input_path}; using {num_frames}")

    # ==== Compute KLT basis (from selected training frames) ====
    training_tsdfs = [load_tsdf_tensor(tsdf_paths[i]) for i in args.training_frames]
    training_blocks = extract_training_blocks_torch(training_tsdfs, block_size=args.block_size)
    klt_basis, mean_vec = compute_klt_basis_torch(training_blocks)
    components = min(args.num_components, len(klt_basis))
    context_path = os.path.join(compressed_folder, "decoder_context.pt")
    torch.save({
        "version": 1,
        "basis": klt_basis[:components].cpu(),
        "mean": mean_vec.cpu(),
        "block_size": args.block_size,
        "num_components": components,
    }, context_path)
    decoder = (
        torch.load(context_path, map_location=device, weights_only=True)
        if verify_decode else None
    )
    del training_tsdfs, training_blocks
    if torch.cuda.is_available():
        torch.cuda.empty_cache()

    total_bits = 0
    decode_times = []

    for tsdf_path in tqdm(tsdf_paths[:num_frames], desc="Compressing frames"):
        target_tsdf = load_tsdf_tensor(tsdf_path)
        target_blocks = get_nonoverlapping_blocks_torch(target_tsdf, block_size=args.block_size)

        # 1) Compress
        coeffs = compress_blocks_torch(target_blocks, klt_basis, mean_vec,
                                       num_components=args.num_components)

        # 2) Quantize + save
        eigenvalues = coeffs.var(dim=0, unbiased=coeffs.shape[0] > 1)
        quantized_indices, bin_centers, fixed_dims = quantize_coeffs(
            coeffs, eigenvalues, K_total=args.k_total)

        frame_name = os.path.splitext(os.path.basename(tsdf_path))[0]
        output_coeffs_path = os.path.join(compressed_folder, f"{frame_name}_quantized")
        save_quantized_coeffs(
            quantized_indices, bin_centers, fixed_dims, output_coeffs_path,
            target_tsdf.shape,
        )

        # Theoretical bits (before entropy coding)
        total_bits += quantized_indices.numel() * np.log2(args.k_total)

        if not verify_decode:
            continue

        # 3) Decode / reconstruct timing
        start_time = time.time()
        decoded_indices, decoded_centers, decoded_fixed, volume_shape = (
            load_quantized_coeffs(output_coeffs_path)
        )
        reconstructed = decompress_coeffs(
            decoded_indices, decoded_centers, decoded_fixed, H=decoded_indices.shape[1]
        )
        recon_blocks = reconstruct_blocks_torch(
            reconstructed, decoder["basis"], decoder["mean"],
            num_components=decoder["num_components"],
        )
        recon_vol = reconstruct_volume_from_blocks_torch(
            recon_blocks,
            volume_shape=volume_shape,
            block_size=decoder["block_size"],
        )

        # 4) Marching cubes and save mesh
        x_nx3, cube_fx8 = construct_voxel_grid(target_tsdf.shape[0] - 1, device)
        x_nx3 *= 2.2
        vertices, faces = dynamic_marching_cubes(x_nx3, cube_fx8, recon_vol.flatten())

        decode_times.append(time.time() - start_time)

        mesh_np = trimesh.Trimesh(vertices=vertices.cpu().numpy(),
                                  faces=faces.cpu().numpy(), process=False)
        mesh_np.export(os.path.join(reconstructed_folder, f"mesh_{frame_name}.obj"))

        del target_tsdf, target_blocks, coeffs, reconstructed, recon_blocks, recon_vol
        if torch.cuda.is_available():
            torch.cuda.empty_cache()

    bitrate_kbps = compute_bitrate(total_bits, num_frames, args.fps)

    # Entropy-coding simulation (.zip) for a realistic bitrate
    zip_path = os.path.join(args.output_path, "compressed_archive.zip")
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as zipf:
        for root, _, files in os.walk(compressed_folder):
            for file in files:
                file_path = os.path.join(root, file)
                zipf.write(file_path, arcname=os.path.relpath(file_path, compressed_folder))
    zip_bitrate_kbps = compute_bitrate(os.path.getsize(zip_path) * 8, num_frames, args.fps)

    print(f"\nProcessed {num_frames} frames at {args.fps} FPS")
    print(f"Theoretical bitrate (no entropy coding): {bitrate_kbps:.2f} kbps")
    print(f"Entropy-coded bitrate (.zip size): {zip_bitrate_kbps:.2f} kbps")
    if decode_times:
        avg_decode_time = float(np.mean(decode_times))
        print(f"Average decode time per frame: {avg_decode_time:.4f} s ({1.0 / avg_decode_time:.2f} FPS)")
    print(f"Compressed coeffs: {compressed_folder}")
    print(f"Reconstructed meshes: {reconstructed_folder}")
    return reconstructed_folder


def run_evaluation(args, reconstructed_folder):
    """Optional D1/D2 PSNR + SSIM evaluation against ground-truth meshes."""
    import open3d as o3d
    import pymeshlab
    from util import evaluate_meshes, evaluate_psnr, load_mesh_list

    out_dir = os.path.join(args.gt_path, "SSIM")
    num_views = 4
    viewpoints = [o3d.io.read_pinhole_camera_parameters(f"{out_dir}/view_{i:02d}.json")
                  for i in range(num_views)]

    (avg_d1, max_d1, min_d1, avg_d2, max_d2, min_d2) = evaluate_psnr(
        args.gt_path, reconstructed_folder, args.num_frames, mode="default")
    print("KLT PSNR:")
    print(f"  D1 Avg: {avg_d1:.3f}, Max: {max_d1:.3f}, Min: {min_d1:.3f}")
    print(f"  D2 Avg: {avg_d2:.3f}, Max: {max_d2:.3f}, Min: {min_d2:.3f}\n")

    gt_meshes = load_mesh_list(args.gt_path, "default")
    rec_meshes = load_mesh_list(reconstructed_folder, "default")

    # Fix orientation for reconstructed meshes (only first time)
    marker_file = os.path.join(reconstructed_folder, "orientation_fixed.txt")
    if not os.path.exists(marker_file):
        print("Fixing orientation for KLT meshes...")
        for rec_file in rec_meshes:
            ms = pymeshlab.MeshSet()
            ms.load_new_mesh(rec_file)
            ms.meshing_invert_face_orientation()
            ms.save_current_mesh(rec_file)
        with open(marker_file, "w") as f:
            f.write("orientation fixed")

    ssim_depth, ssim_color, psnr_depth, psnr_color = [], [], [], []
    for gt_file, rec_file in zip(gt_meshes[:args.num_frames], rec_meshes[:args.num_frames]):
        gt_mesh = o3d.io.read_triangle_mesh(gt_file)
        rec_mesh = o3d.io.read_triangle_mesh(rec_file)
        gt_mesh.compute_vertex_normals()
        rec_mesh.compute_vertex_normals()
        d, c, pd, pc = evaluate_meshes(
            gt_mesh, rec_mesh, viewpoints,
            output_dir=os.path.join(reconstructed_folder, "SSIM", "renderings"))
        ssim_depth.append(d)
        ssim_color.append(c)
        psnr_depth.append(pd)
        psnr_color.append(pc)

    print("KLT SSIM/PSNR:")
    print(f"  SSIM Depth Avg: {np.mean(ssim_depth):.4f}")
    print(f"  SSIM Color Avg: {np.mean(ssim_color):.4f}")
    print(f"  PSNR Depth Avg: {np.mean(psnr_depth):.3f}")
    print(f"  PSNR Color Avg: {np.mean(psnr_color):.3f}\n")


def parse_args():
    parser = argparse.ArgumentParser(description="KLT baseline for TSDF mesh compression")
    parser.add_argument("--input_path", type=str, required=True,
                        help="Directory of per-frame TSDF .npz files")
    parser.add_argument("--output_path", type=str, required=True,
                        help="Output directory for compressed coeffs and reconstructed meshes")
    parser.add_argument("--num_components", type=int, default=128,
                        help="Number of KLT basis vectors to keep")
    parser.add_argument("--block_size", type=int, default=8,
                        help="Voxel block edge length (block contains block_size^3 TSDFs)")
    parser.add_argument("--voxel_grid_res", type=int, default=127,
                        help="TSDF voxel-grid resolution")
    parser.add_argument("--k_total", type=int, default=2048 * 8,
                        help="Total quantization bins distributed across coefficient dims")
    parser.add_argument("--fps", type=int, default=30, help="Sequence frame rate for bitrate")
    parser.add_argument("--num_frames", type=int, default=100,
                        help="Number of frames to compress")
    parser.add_argument("--training_frames", type=int, nargs="+", default=[1],
                        help="Frame indices used to learn the KLT basis (few, GPU-memory bound)")
    parser.add_argument("--evaluate", action="store_true",
                        help="Run D1/D2 PSNR + SSIM after compression")
    parser.add_argument("--gt_path", type=str, default=None,
                        help="Ground-truth dataset root (required with --evaluate)")
    return parser.parse_args()


def main():
    args = parse_args()
    reconstructed_folder = run_compression(args)
    if args.evaluate:
        if not args.gt_path:
            raise ValueError("--evaluate requires --gt_path")
        run_evaluation(args, reconstructed_folder)


if __name__ == "__main__":
    main()
