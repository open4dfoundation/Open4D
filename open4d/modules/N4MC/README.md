# N4MC: Neural 4D Mesh Compression



## Quick Start

**Ubuntu + NVIDIA GPU only.** Two scripts run the supported end-to-end codec
workflow (run from `modules/N4MC`):

```bash
bash setup.sh    # create the conda env from environment.yml + verify GPU torch
bash run.sh      # prepare + train + decode + evaluate + package
```

The result is written to `outputs/basketball/`: reconstructed OBJ files,
`evaluation.json`, training artifacts, and a portable `sequence.n4mc` archive.
Frame count and configuration are generated automatically.

Use your own sequence or run a quick GPU smoke test:

```bash
MESH_DIR=/path/to/frames OUTPUT_DIR=/path/to/output bash run.sh
SMOKE=1 FORCE=1 bash run.sh
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

For every latent grid, `decode.py` runs the trained decoder to predict an SDF
and deformation offsets, extracts the zero level set with dynamic marching
cubes, reverses the original normalization, and exports an OBJ mesh. The
decoder automatically uses CUDA when available and otherwise requests CPU.

`run.sh` already performs this decoding before it creates the archive, so its
normal reconstructed frames can be played directly:

```bash
python ../tools/mesh_player.py \
  --mesh-dir outputs/basketball/decoded \
  --pattern '*.obj' \
  --fps 10 \
  --loop
```

To play meshes decoded from the extracted archive instead, change `--mesh-dir`
to `outputs/basketball/archive_decoded`. Close the Open3D window to stop. On a
headless SSH session, copy the decoded OBJ directory to a desktop machine or
connect with X11 forwarding before starting the player.

The default supported pipeline uses `gen_tsdf_from_meshes.py`, which does not
need a compiler toolchain. The differentiable-rendering refinement and temporal
interpolation sections later in this document remain experimental research paths;
they require external renderer/tracking outputs and are not invoked by `run.sh`.

The manual equivalent of those scripts:

```bash
# 1. Create and activate the conda environment
conda env create -f environment.yml     # use `conda env update -f environment.yml` to refresh
conda activate pytorch
cd n4mc_source

# 2. Prepare data — build TSDF-Def tensors from a mesh sequence.
#    No dataset of your own? Use the bundled basketball_player meshes:
python gen_tsdf_from_meshes.py \
    --mesh_dir ../../tvmc/arap-volume-tracking/data/basketball_player \
    --save_path ./TSDF_128 --voxel_grid_res 127
#    -> writes ./TSDF_128/data/0000.npz ... (one .npz per frame). Set
#       `num_frames` in configs/configs_128.txt to the number of frames produced
#       (the basketball sequence has 10).

# 3. Train the auto-decoder (main entry point)
python train_quant.py --config=../configs/configs_128.txt
```

`train_quant.py` reads the TSDF-Def tensors referenced by the config
(`data_path: ./TSDF_128`, i.e. `TSDF_128/data/0000.npz`, ...). Each `.npz` holds
`sdf` `(R+1,R+1,R+1,1)` and `offset` `(R+1,R+1,R+1,3)` grids (R = `voxel_grid_res`).

### About `gen_tsdf_from_meshes.py`

This helper produces a valid TSDF-Def dataset from any folder of `.obj`/`.ply`
frames using `point_cloud_utils` only. It reproduces Step 2 (normalize each mesh
into the unit cube) + the *initial* SDF of Step 3, so it needs **neither
`nvdiffrast` nor a CUDA toolkit** — handy on machines without `nvcc`. It skips
only Step 3's differentiable-rendering refinement of the SDF/offset (`offset`
starts at zeros); the format is identical and the auto-decoder trains normally.
For the full, refined tensors, run the `optimize_tsdf_offset.py` pipeline below
instead (requires `nvdiffrast` + `render.py`).

**Notes**

- The environment is CUDA 12.4 based (`torch==2.6.0+cu124`). `environment.yml`
  pulls `torch-scatter` from the PyG wheel index and `kaolin` from NVIDIA's via
  the `--find-links` lines at the top of its `pip:` block — no manual steps needed.
- `nvdiffrast` (Step 3), `pytorch3d` (Step 5) and `flash-attn` are left commented
  out in `environment.yml` because they must be compiled against a local CUDA
  toolkit (`nvcc`). Install them only if you need those steps, after installing a
  matching CUDA toolkit:

  ```bash
  pip install git+https://github.com/NVlabs/nvdiffrast.git   # Step 3
  pip install pytorch3d                                       # Step 5
  ```

The full pipeline (data scaling, volume tracking, TSDF conversion, interpolation
transformer, evaluation) is documented in the steps below.

## System Requirements

- OS: Ubuntu 24.04
- Python: 3.10
- CUDA **11.x or 12.x** 
- .NET 5.0 and 7.0
- Dependencies:
  - `numpy`
  - `torch`
  - `Open3D==0.19.0`

## Step 0: Set up environment

Install conda environment:

```
 conda env create -f ./n4mc/environment.yml
```

Install .NET 5.0 and 7.0:

```
wget https://dot.net/v1/dotnet-install.sh -O dotnet-install.sh
./dotnet-install.sh --version 7.0.202 
./dotnet-install.sh --channel 7.0
./dotnet-install.sh --channel 7.0 --runtime aspnetcore
./dotnet-install.sh --version 5.0.408
./dotnet-install.sh --channel 5.0 --runtime aspnetcore
```

Set up dotnet path if needed

```Bash
export DOTNET_ROOT=$HOME/.dotnet
export PATH=$HOME/.dotnet:$PATH
```

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

