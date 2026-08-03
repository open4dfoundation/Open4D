# Mesh Compression using Quantized Neural Displacement Fields

Step 1. Go to ssp_remesh folder and follow the instructions in it.

Step 2. Install Pytorch3D and its dependencies (https://github.com/facebookresearch/pytorch3d/blob/main/INSTALL.md)

Step 3. Install `dahuffman` and `tqdm` using `pip`

Step 4. Place any `.obj` file to be compressed in `objs_original/` folder. (Some meshes are already available)

Step 5. Run:

 ```python compress.py [mesh name] -ns [number of subdivisons] -cs [coarse mesh size] -hd [hidden dim size of INR] -nl [layers in INR]```

For Example:

 ```python compress.py pegasus -ns 3 -cs 7000 -hd 96 -nl 32```

## Disconnected meshes (basketball player)

The original SSP preprocessing can collapse small disconnected parts or project
them onto an unrelated surface. Use `build_dataset_open3d.py` for disconnected
meshes. It removes zero-area triangles, allocates the coarse-face budget across
all connected components, and simplifies, subdivides, and projects each component
independently. A JSON transform is saved beside the training pair so decoded
vertices can be restored to the input coordinate system.

Run the complete basketball sequence with:

```bash
conda activate pytorch
mkdir -p outputs/basketball_sequence_qndf
nohup python run_basketball_sequence.py \
  --source-dir ../tvmc/arap-volume-tracking/data/basketball_player \
  --gpus 0,1 > outputs/basketball_sequence_qndf/runner.log 2>&1 &
```

The sequence runner is resumable and records per-frame state in
`outputs/basketball_sequence_qndf/status.json`. Each completed frame retains its
training log, normalization transform, normalized reconstruction, and decoded
mesh restored to the original scale.
