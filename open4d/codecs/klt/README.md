# KLT

KLT (Karhunen–Loève Transform) baseline for time-varying mesh compression via
TSDF volumes. This is the classical linear-transform baseline used to benchmark
the neural codecs in Open4D (N4MC, QNDF). It was promoted out of `N4MC` into a
self-contained module.

## Method

1. **Learn a basis.** Overlapping `block_size³` voxel blocks are extracted from a
   small set of training TSDF frames and a KLT (PCA) basis `P` plus mean `μ` is
   computed via SVD.
2. **Compress.** Each target frame is split into *non-overlapping* blocks,
   centered, and projected onto the first `num_components` basis vectors.
3. **Quantize.** Coefficients are quantized with eigenvalue-weighted 1D k-means
   (Lloyd's algorithm), so higher-variance dimensions receive more bins. Indices
   are stored with `zstd`; a `.zip` pass simulates entropy coding for bitrate.
4. **Reconstruct.** Coefficients are dequantized, back-projected to blocks, the
   volume is reassembled, and a mesh is extracted with marching cubes.

> Only a couple of training frames fit on a 24 GB GPU at resolution 128–256,
> because `extract_training_blocks_torch` materializes every overlapping block.

## Layout

```
KLT/
├── klt.py           # main CLI: learn basis → compress → reconstruct (+ optional eval)
├── klt_open3d.py    # Open3D marching-cubes / visualization variant
├── fmc.py           # marching-cubes helpers (copied from N4MC)
├── util.py          # evaluation + metric helpers (copied from N4MC)
├── metrics.py       # D1/D2 PSNR (copied from N4MC)
├── requirements.txt
└── README.md
```

## Setup

```bash
python -m venv .venv && source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -r requirements.txt
```

`kaolin`, `point_cloud_utils`, and `pymeshlab` are only needed for the optional
`--evaluate` path; the compression pipeline itself needs only torch, numpy,
trimesh, and zstd.

## Usage

Input is a folder of per-frame TSDF `.npz` files, each holding an `sdf` array
(produced by N4MC's `optimize_tsdf_offset.py`).

```bash
python klt.py \
    --input_path /path/to/TSDF \
    --output_path outputs/klt_run \
    --num_components 128 --block_size 8 --voxel_grid_res 127 \
    --k_total 16384 --training_frames 1 --num_frames 100
```

Reference operating points (block_size 8): `num_components 16 → ~2.4 Mbps`,
`32 → ~4 Mbps`, `128 → ~7 Mbps`.

Add evaluation against ground-truth meshes (needs the `SSIM/view_0*.json`
viewpoint files under the dataset root):

```bash
python klt.py ... --evaluate --gt_path /path/to/combined_scaled
```

## Notes

- Do not commit datasets, TSDF volumes, reconstructed meshes, or `outputs/`.
- Helpers are copied (not imported) from N4MC so the module runs independently;
  keep them in sync if the N4MC originals change.
