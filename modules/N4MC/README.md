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

