# Implicit-mesh-compression

Reimplementation of TSDF mesh compression.

Fork from https://github.com/rsy6318/NeCGS, environment specified, bug fixed.


## Get TSDF and offset (TSDF-Def)

The original code from NeCGS is for compressing multiple objects.

They put meshes in a folder:

- Mesh_path
  - 0000.obj
  - 0001.obj
    ......

Run the following command to optimize each TSDF-Def volume:

`python optimize_tsdf_offset.py --data_path=<input meshes path> --save_path=<output path> --num_frames=10`

This will generate 2 folders:

* data
  * TSDF
* meshes

`.npz` files in `./data` folder includes TSDF and offset for the mesh. For convenience, I stored TSDF solely in `./data/TSDF`

Note that all the objects are scaled into a unit cube with a range of [-1, 1]. For scene mesh compression, we do not need to do that, we can set the resolution to a unit cube with a range of [-x, x].



## Use KLT to compress TSDFs

change `path` in `klt.py`, change which frames you want to use as training TSDFs (Google used 50 frames, but during my experiments, it will run out of GPU memory (RTX 4090, 24GB) when using over 2 frames), might because of the resolution (256). `extract_training_blocks_torch` function will extract overlapping voxel blocks with a resolution.

All extracted overlapping voxel blocks are used to compute KLT basis and a mean vector (P, μ).

Then specify target TSDF, `get_nonoverlapping_blocks_torch` returns non-overlapping voxel blocks.

`compress_blocks_torch` use a certain number of KLT basis to compute the coefficients for the target blocks.


Then the TSDFs and the corresponding mesh will be reconstructed.



## Compact Neural Representation

Change the data_path in configs/configs.txt, then run the following command,

`python train_quant.py --config=configs/configs.txt`

default `voxel_grid_res: 127`  `embed_hwd: 4` if you want to change voxel resolution, remember to change embedded features' resolultion.

(How to use 2 4090s simultaneously?)

The outout will be stored in `./log`. The meshes will be compressed into embedded features and a neural network decoder.

## New TSDF Codec Baseline

The preferred path for the clean restart now lives in modular packages:

- `data/`: TSDF dataset loading and validation
- `models/`: fresh 3D codec, quantization, and entropy proxy
- `losses/`: reconstruction, narrow-band, sign-aware, and SSIM losses
- `training/`: train and validation entry points
- `evaluation/`: TSDF reconstruction, marching cubes, and mesh metrics

Default configs:

- [`configs/train_tsdf.yaml`](configs/train_tsdf.yaml)
- [`configs/eval_tsdf.yaml`](configs/eval_tsdf.yaml)

Commands:

```bash
python -m training.train --config configs/train_tsdf.yaml
python -m training.validate --config configs/eval_tsdf.yaml --checkpoint outputs/<run>/best.pt
python -m evaluation.reconstruct --config configs/eval_tsdf.yaml --checkpoint outputs/<run>/best.pt
```

Important hyperparameters:

- `model.embed_dim`: latent channel count
- `model.embed_hwd`: latent spatial size before quantization, for example `4`
- `model.latent_channels`: bottleneck width
- `loss.lambda_rate`: rate weight in the RD objective
- `loss.narrow_band_threshold`: zero-level-set emphasis band
- `loss.lambda_sign`: sign-consistency penalty
- `loss.lambda_ssim`: structural similarity penalty
- `optimizer.lr`: learning rate
- `data.batch_size`: training batch size

Latent reuse:

- the compressed embedded feature is `quantized_latent`
- it is produced after the encoder and quantizer, before the decoder
- validation and reconstruction now save both `*_quantized_latent.npy` and `*_latent_pack.npz`
- `*_latent_pack.npz` includes `latent`, `quantized_latent`, `original_shape`, `padded_shape`, and `bottleneck_shape`

## Basketball sequence

The maintained basketball configuration trains on eight frames, validates on
one, and holds out one test frame. Reconstruction then evaluates all ten frames
and the restore helper maps the decoded meshes back to the source coordinates.

The complete managed workflow is:

```bash
python scripts/run_basketball_sequence.py
```

Its status and final manifest are written to
`outputs/basketball_sequence_n4mc/status.json` and `summary.json`.

```bash
python -m training.train --config configs/train_basketball.yaml
python -m evaluation.reconstruct \
  --config configs/eval_basketball.yaml \
  --checkpoint outputs/<basketball-run>/best.pt \
  --split test \
  --output-dir outputs/basketball_sequence_n4mc/normalized
python -m evaluation.restore_sequence \
  --input-dir outputs/basketball_sequence_n4mc/normalized \
  --normalization datasets/basketball_normalized/normalization.npz \
  --output-dir outputs/basketball_sequence_n4mc/original_scale
```
