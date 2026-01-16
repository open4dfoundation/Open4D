# **TVMC: Time-Varying Mesh Compression Using Volume-Tracked Reference Meshes**

Guodong Chen, Filip Hácha, Libor Váša, Mallesham Dasari

![alt text](https://github.com/frozzzen3/TVMC/blob/main/images/TVMC-workflow.png?raw=true)

This repository contains the official authors implementation associated with the paper **"TVMC: Time-Varying Mesh Compression Using Volume-Tracked Reference Meshes"**, which can be found [here](https://github.com/frozzzen3/TVMC/blob/main/files/TVMC_accepted.pdf), accepted by [2025 ACM MMSys](https://2025.acmmmsys.org/).

## BibTex

```
@inproceedings{Chen2025TVMC,
  author    = {Chen, Guodong and H{\'a}cha, Filip and V{\'a}{\v{s}}a, Libor and Dasari, Mallesham},
  title     = {TVMC: Time-Varying Mesh Compression Using Volume-Tracked Reference Meshes},
  booktitle = {ACM Multimedia Systems Conference 2025 (MMSys '25)},
  year      = {2025},
  address   = {Stellenbosch, South Africa},
  doi       = {10.1145/3712676.3714440}
}
```

## System Requirements

- **Operating System:** Windows 11 or Ubuntu 20.04
- **Python:** 3.8
- Dependencies:
  - `numpy`
  - `open3d==0.18.0`
  - `scikit-learn`
  - `scipy`
  - `trimesh==4.1.0`

Clone this project:

```
git clone https://github.com/SINRG-Lab/TVMC.git
```

## Running with Docker

Follow these steps to build and run the Docker image:

### Step 1: Build the Docker Image

To begin, build the Docker image from the provided Dockerfile:

```
docker build -t tvmc-linux .
```

### Step 2: Run the Docker Image

After building the image, run the Docker container with the following command:

```
docker run --rm -it tvmc-linux
```

### Step 3: Run the Pipeline Script

Once inside the Docker container, grant execute permissions to the `run_pipeline.sh` script and execute it:

```
conda activate open3d_env
chmod +x run_pipeline.sh
sudo ./run_pipeline.sh
```

The pipeline will start, and the required tasks will be executed sequentially.



---

## Running TVMC on Your Own Machine

If you want to run TVMC on your own machine using your own dataset, here’s how you can set it up. We've tested this on Windows 11 and Ubuntu 20.04.

(Provide the detailed steps here for running the pipeline outside of Docker)

## Step 0:

Install .Net 7.0

For Linux:

```
wget https://packages.microsoft.com/config/ubuntu/20.04/packages-microsoft-prod.deb -O packages-microsoft-prod.deb
sudo dpkg -i packages-microsoft-prod.deb
rm packages-microsoft-prod.deb
sudo apt-get update && \  sudo apt-get install -y dotnet-sdk-7.0
sudo apt-get update && \  sudo apt-get install -y aspnetcore-runtime-7.0
```

```
wget https://repo.anaconda.com/miniconda/Miniconda3-latest-Linux-x86_64.sh
bash Miniconda3-latest-Linux-x86_64.sh

conda create -n open3d_env python==3.8 numpy open3d==0.18.0 scikit-learn scipy trimesh==4.1.0 -c conda-forge
echo 'export PATH="$HOME/miniconda3/bin:$PATH"' 

~/.bashrc source ~/.bashrc
conda init
source ~/.bashrc
conda activate open3d_env
```



For windows you can use Visual Studio to install .NET 7.0 and anaconda to create python environment.



## Step 1: As-Rigid-As-Possible (ARAP) Volume Tracking

ARAP volume tracking is written in C# and .NET 7.0

Navigate to the root directory:

```
cd ./arap-volume-tracking
```

Build the project:

```
dotnet build -c release
```

Run the tracking process:

```
dotnet ./bin/Client.dll ./config/max/config-basketball-max.xml
```

Volume tracking results are saved in the `<outDir>` folder:

- `.xyz` files: coordinates of volume centers
- `.txt` files: transformations of centers between frames

### Global Optimization (Optional)

Global optimization refines volume centers by removing abnormal volume centers and adjusting positions for the remains to reduce distortions. 

```
dotnet ./bin/Client.dll ./config/impr/config-basketball-impr.xml
```

Results are stored in `<outDir>/impr`.

Additional time-varying mesh sequences are available in `./arap-volume-tracking/data`. Configuration files are in `./arap-volume-tracking/config`. Different tracking modes (`./iir`, `./impr`, `./max`) use distinct configurations. More details [here](https://github.com/frozzzen3/TVMC/blob/main/arap-volume-tracking/README.md).

To run ARAP volume tracking on a custom dataset:

```
dotnet ./bin/Client.dll <config_file.xml>
```

## Step 2: Generate Reference Centers Using Multi-Dimensional Scaling (MDS)

Navigate to TVMC root:

```
cd ../TVMC
```

Run the MDS script:

```
python ./get_reference_center.py --dataset basketball_player --num_frames 10 --num_centers 1995 --centers_dir ../arap-volume-tracking/data/basketball-output-max-2000/impr
```

The number of centers (`--num_centers`) must match the global optimization results. Output is stored in `./arap-volume-tracking/data/basketball-output-max-2000/impr/reference/reference_centers_aligned.xyz`.

If the number of volume centers is large, experiment with different `random_state` values for better results.

## Step 3: Compute Transformation Dual Quaternions

Then, we compute the transformations for each center, mapping their original positions to the reference space, along with their inverses. These transformations are then used to deform the mesh surface based on the movement of volume centers.

```
python ./get_transformation.py --dataset basketball_player --num_frames 10 --num_centers 1995 --centers_dir ../arap-volume-tracking/data/basketball-output-max-2000/impr --firstIndex 11 --lastIndex 20
```

## Step 4: Create Volume-Tracked, Self-Contact-Free Reference Mesh

For Linux, switch to .NET 5.0.

```
sudo apt-get install -y dotnet-sdk-5.0
sudo apt-get install -y aspnetcore-runtime-5.0
```

Navigate to the `tvm-editing` directory and build:

```
cd ../tvm-editing
dotnet new globaljson --sdk-version 5.0.408 
dotnet build TVMEditor.sln --configuration Release --no-incremental
```

(For Windows the writer used .NET 8.0 and there is no need to install .NET 5.0 and run `dotnet new globaljson --sdk-version 5.0.408 `. If you encounter error regarding .NET version, try to install the correct version on your machine.)

Run the mesh deformation:

for Windows:

```
TVMEditor.Test/bin/Release/net5.0/TVMEditor.Test.exe basketball 1 11 20 "./TVMEditor.Test/bin/Release/net5.0/Data/basketball_player_1995/" "./TVMEditor.Test/bin/Release/net5.0/output/basketball_player_1995/"
```

for Linux:

```
TVMEditor.Test/bin/Release/net5.0/TVMEditor.Test basketball 1 11 20 "./TVMEditor.Test/bin/Release/net5.0/Data/basketball_player_1995/" "./TVMEditor.Test/bin/Release/net5.0/output/basketball_player_1995/"
```

Navigate to TVMC root again:

```
cd ../TVMC
```

Extract the reference mesh:

```
python ./extract_reference_mesh.py --dataset basketball_player --num_frames 10 --num_centers 1995 --inputDir ../tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/basketball_player_1995/output/ --outputDir ../tvm-editing/TVMEditor.Test/bin/Release/net5.0/Data/basketball_player_1995/reference_mesh/ --firstIndex 11 --lastIndex 20 --key 9
```

## Step 5: Deform Reference Mesh to Each Mesh in the Group

Navigate to the `tvm-editing` directory,

```
cd ../tvm-editing
```

Then run:

```
TVMEditor.Test/bin/Release/net5.0/TVMEditor.Test.exe basketball 2 11 20 "./TVMEditor.Test/bin/Release/net5.0/Data/basketball_player_1995" "./TVMEditor.Test/bin/Release/net5.0/output/basketball_player_1995/"
```

For Linux:

```
TVMEditor.Test/bin/Release/net5.0/TVMEditor.Test basketball 2 11 20 "./TVMEditor.Test/bin/Release/net5.0/Data/basketball_player_1995" "./TVMEditor.Test/bin/Release/net5.0/output/basketball_player_1995/"
```



## Step 6: Compute Displacement Fields

Navigate to ./TVMC again:

```
cd ../TVMC
```

```
python ./get_displacements.py --dataset basketball_player --num_frames 10 --num_centers 1995 --target_mesh_path ../arap-volume-tracking/data/basketball_player --firstIndex 11 --lastIndex 20
```

The displacement fields are stored as `.ply` files. For compression, Draco is used to encode both the reference mesh and displacements.



Tips: So far, we've got everything we need for a group of time-varying mesh compression (A self-contact-free reference mesh and displacement fields). You can use any other compression methods to deal with them. For example, you may use video coding to compress displacements to get even better compression performance.

## Step 7: Compression and Evaluation

Navigate to TVMC root:

```
cd ..
```

Clone and build Draco:

```
git clone https://github.com/google/draco.git
cd ./draco
mkdir build
cd build
```

On Windows:

```
cmake ../ -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

On Linux:

```
cmake ../
make
```

Draco paths (please change based on your project):

- Encoder: `./draco/build/Release/draco_encoder.exe` / `./draco/build/draco_encoder` 
- Decoder: `./draco/build/Release/draco_decoder.exe` / `./draco/build/draco_decoder` 

Navigate to TVMC:

```
cd ../../TVMC
```

Run the evaluation:

```
python ./evaluation.py --dataset basketball_player --num_frames 10 --num_centers 1995 --firstIndex 11 --lastIndex 20 --fileNamePrefix basketball_player_fr0 --encoderPath ../draco/build/Release/draco_encoder.exe --decoderPath ../draco/build/Release/draco_decoder.exe --qp 10 --outputPath ./basketball_player_outputs
```

For Linux:

```
python ./evaluation.py --dataset basketball_player --num_frames 10 --num_centers 1995 --firstIndex 11 --lastIndex 20 --fileNamePrefix basketball_player_fr0 --encoderPath ../draco/build/draco_encoder --decoderPath ../draco/build/draco_decoder --qp 10 --outputPath ./basketball_player_outputs
```



## Generate figures

We provide scripts to generate the figures presented in the paper based on the collected results.

- **If you have went through above steps for all four datasets**, execute:

  ```
  python ./objective_results_all.py
  ```

  This script uses the newly generated results to produce Rate-Distortion (RD) performance and Cumulative Distribution Function (CDF) figures.

  (in the Docker image you may need to store generated figures locally using `docker cp`)

- **If you followed the recommended commands above** (which generate results only for the *Basketball Player* dataset), 

  ```
  python ./objective_results_basic.py
  ```

  This version primarily uses mostly the original data from the paper to replicate the key figures.

This provides a straightforward way to replicate the results.
