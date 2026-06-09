# KLT TSDF Compression Baseline

This baseline compresses TSDF volumes with a Karhunen-Loeve Transform (KLT / PCA) block codec, reconstructs TSDF volumes from quantized coefficients, extracts meshes with marching cubes, and reports bitrate plus optional mesh quality metrics.

The implementation is in [`google_klt.py`](google_klt.py).

## Method Summary

For each TSDF sequence, the baseline:

1. Loads `.npz` TSDF frames from `--input-path`. Each file must contain an `sdf` array.
2. Trains a KLT basis from overlapping TSDF blocks selected by `--training-start` and `--training-count`.
3. Splits each target TSDF into non-overlapping cubic blocks.
4. Projects each block onto the first `--num-components` KLT basis vectors.
5. Quantizes KLT coefficients with per-dimension 1D K-means.
6. Saves compressed coefficient indices and quantization metadata.
7. Reconstructs TSDF blocks and volumes from the quantized coefficients.
8. Runs dynamic marching cubes to export reconstructed `.obj` meshes.
9. Creates a `.zip` archive of the compressed files for entropy-coded bitrate reporting.
10. Optionally evaluates reconstructed meshes with PSNR and SSIM utilities from `util.py`.

## Requirements

Install the project environment before running the baseline. The script requires the core project dependencies plus:

- `numpy`
- `torch`
- `trimesh`
- `tqdm`
- `zstd`
- `open3d` for evaluation
- `pymeshlab` for the one-time reconstructed mesh orientation fix during evaluation

The compression path imports `fmc.py` from this repository for voxel-grid construction and dynamic marching cubes.

## Input Layout

`--input-path` should point to a directory of TSDF `.npz` files:

```text
/path/to/TSDF/
  frame_000000.npz
  frame_000001.npz
  frame_000002.npz
  ...
```

Each `.npz` file is expected to contain:

```python
sdf: array-like TSDF volume with shape [D, H, W] or a squeezable equivalent
```

For evaluation, `--ground-truth-path` should point to the dataset root expected by the repository's `load_mesh_list`, `evaluate_psnr`, and `evaluate_meshes` helpers. The SSIM camera parameters are read from:

```text
<ground-truth-path>/SSIM/view_00.json
<ground-truth-path>/SSIM/view_01.json
...
```

## Usage

Show available options:

```bash
python klt.py --help
```

Run the default experiment:

```bash
python klt.py
```

Run on a custom TSDF directory and output directory:

```bash
python klt.py \
  --input-path /path/to/TSDF \
  --output-root /path/to/output/klt_8_128 \
  --ground-truth-path /path/to/dataset/root
```

Run compression and reconstruction without PSNR / SSIM evaluation:

```bash
python klt.py \
  --input-path /path/to/TSDF \
  --output-root /path/to/output/klt_smoke_test \
  --num-frames 5 \
  --skip-evaluation
```

Force CPU execution:

```bash
python klt.py --device cpu --skip-evaluation
```

Change the compression configuration:

```bash
python klt.py \
  --block-size 8 \
  --num-components 64 \
  --k-total 8192 \
  --num-frames 100
```

## Important Arguments

| Argument | Default | Description |
| --- | --- | --- |
| `--input-path` | original TSDF path | Directory containing input `.npz` TSDF frames. |
| `--output-root` | `klt_<block-size>_<num-components>` under the original dataset root | Root directory for compressed files, reconstructed meshes, and archive. |
| `--ground-truth-path` | original dataset root | Dataset root used by evaluation utilities. |
| `--num-components` | `128` | Number of KLT basis vectors retained for each block. |
| `--block-size` | `8` | Cubic block side length. A block contains `block_size^3` TSDF samples. |
| `--k-total` | `16384` | Total quantization-bin budget distributed across coefficient dimensions. |
| `--num-frames` | `100` | Number of sorted TSDF frames to process. |
| `--training-start` | `1` | First sorted TSDF index used for KLT training. |
| `--training-count` | `1` | Number of TSDF frames used to train the KLT basis. |
| `--evaluation-frames` | `10` | Number of reconstructed meshes used for PSNR / SSIM evaluation. |
| `--num-views` | `4` | Number of SSIM camera parameter files to load. |
| `--device` | auto | Torch device. Uses CUDA when available unless explicitly set. |
| `--skip-evaluation` | off | Skip mesh PSNR / SSIM evaluation. |
| `--quiet-training` | off | Suppress training block-shape and singular-value logs. |

## Outputs

For an output root such as `/path/to/output/klt_8_128`, the script writes:

```text
/path/to/output/klt_8_128/
  compressed/
    <frame>_quantized_indices.zst
    <frame>_quantized_metadata.npz
    ...
  reconstructed_meshes/
    mesh_<frame>.obj
    orientation_fixed.txt          # created after evaluation orientation fix
    SSIM/renderings/               # created by evaluation utilities
  compressed_archive.zip
```

Compressed coefficient files:

- `*_indices.zst` stores quantized coefficient indices as raw bytes compressed with zstd.
- `*_metadata.npz` stores per-dimension bin centers and fixed-dimension metadata.

Reconstructed meshes:

- `mesh_<frame>.obj` is exported with `trimesh` after dynamic marching cubes.

Summary metrics printed at the end include:

- theoretical bitrate before entropy coding
- zip-size bitrate as an entropy-coding proxy
- average decode time per frame
- reconstructed mesh output location
- optional D1 / D2 PSNR and rendered SSIM / PSNR metrics

## Reproducing the Original Baseline

Use the default settings when the dataset is available at the original hardcoded location:

```bash
python klt.py
```

Equivalent explicit command:

```bash
python klt.py \
  --input-path . \
  --ground-truth-path . \
  --output-root . \
  --num-components 128 \
  --block-size 8 \
  --k-total 16384 \
  --num-frames 100 \
  --training-start 1 \
  --training-count 1 \
  --fps 30
```

## Notes and Limitations

- The baseline is deterministic in structure, but K-means convergence and GPU floating-point behavior may vary slightly across hardware and PyTorch versions.
- `--voxel-grid-res` is retained for experiment compatibility, but reconstruction currently infers the voxel grid from each TSDF volume shape.
- Evaluation requires the repository-specific dataset layout and camera files under `<ground-truth-path>/SSIM/`.
- The first evaluation run may modify reconstructed meshes by inverting face orientation and writing `orientation_fixed.txt`. Later runs skip this step when the marker exists.
- The compressed `.zst` index files store raw index bytes; the metadata file is required to decode the coefficients.
- For quick smoke tests, use `--num-frames 1 --skip-evaluation`.

## Troubleshooting

If `python klt.py --help` works but running compression fails with `ModuleNotFoundError`, install the missing runtime dependency in the active Python environment.

If no frames are found, check that `--input-path` contains `.npz` files directly under that directory.

If evaluation fails, rerun with `--skip-evaluation` to verify the compression and reconstruction path independently.

If CUDA runs out of memory, reduce `--num-components`, reduce `--block-size`, train from fewer frames, or run with `--device cpu` for a slower but lower-pressure smoke test.
