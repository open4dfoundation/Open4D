# N4MC: Neural 4D Mesh Compression



## System Requirements

- OS: Ubuntu 24.04
- Python: 3.10
- CUDA **11.x or 12.x** 
- .NET 5.0 and 7.0
- Dependencies:
  - `numpy`
  - `torch`
  - `Open3D==0.19.0`

Two scripts run the supported end-to-end codec
workflow (run from `modules/N4MC`):

```bash
bash setup.sh    # create the conda env from environment.yml and verify
bash run.sh      # prepare + train + decode + evaluate + package
```

The result is written to `outputs/basketball/`: reconstructed OBJ files,
`evaluation.json`, training artifacts, and a portable `sequence.n4mc` archive.
Frame count and configuration are generated automatically.

To use your own sequence:

```bash
MESH_DIR=/path/to/frames OUTPUT_DIR=/path/to/output bash run.sh
```

Useful overrides are `FRAMES`, `VOXEL_RES`, `EPOCHS`, `SAMPLES`, `ARCHIVE`,
`FROM_STAGE`, `TO_STAGE`, `FORCE=1`, and `DRY_RUN=1`. The pipeline is resumable:
completed preprocessing, training, and decoding stages are reused unless forced.

## Decode and play an N4MC archive

An `.n4mc` file does not contain OBJ files. It is a ZIP package containing the
trained neural decoder, one latent feature grid per frame, the generated model
configuration, and mesh normalization metadata. To obtain OBJ files, extract
the package and run the N4MC decoder. From `open4d/modules/N4MC`:

```bash
mkdir -p outputs/basketball/unpacked
unzip -o outputs/basketball/sequence.n4mc \
  -d outputs/basketball/unpacked

python n4mc_source/decode.py \
  --config outputs/basketball/unpacked/config.txt \
  --checkpoint outputs/basketball/unpacked \
  --metadata outputs/basketball/unpacked/normalization.json \
  --output outputs/basketball/archive_decoded
```

## The full pipeline

## Step 1: Apply volume tracking on scaled meshes

Clone volume tracking code:
```
git clone https://gitlab.kiv.zcu.cz/jdvorak/arap-volume-tracking.git
```

Navigate to volume tracking folder:

```
cd ./arap-volume-tracking
```

Then you can run volume tracking and get centers by running this:

```
dotnet ./bin/Client.dll ./config/max/<config.xml>
```

## Step 2: Scale 4D mesh sequences

We first scale meshes into a cube with a range of  [−1, 1]^3. Use `data_processing.py` to do that.

```
cd ../n4mc_source
python data_processing.py
```

## Step 3: Convert scaled mesh sequences into TSDF-Def tensors

Now we create uniformed TSDF-Def tensors for all meshes

```
python optimize_tsdf_offset.py --data_path=<data_path> --save_path=<output_path> --num_frames=<number_of_frames> --voxel_grid_res <resolution>
```

`<data_path>`: path for input meshes

`<output_path>`: path for output TSDF-Def tensors

`<resolution>`: Target resolution for TSDF-Def tensors, should be real resolution-1, for example, 63 for 64, 127 for 128, 255 for 256

## Step 4: Train the auto-decoder

Run the training script `train_quant.py` .

```
python train_quant.py --config=configs/<config_file>
```

`config_file`: input configs, find them in folder `./configs` 

## Step 5: Train the interpolation transformer

After we get the trained auto-decoder, we can train the interpolation transformer by running:

```
python train_interpolation.py --config=./configs/<config_file> 
```

You'll need to change `autocodec_path` in configuration interpolation file and apply the correct path for generated volume centers from step 1.

## Step 6: Evaluation

Run the evaluation:

```
python ./evaluation.py
```

