# Mesh Compression using Quantized Neural Displacement Fields

Step 1. Fetch the pinned libigl submodule and build `ssp_remesh`, following
[`ssp_remesh/README.md`](ssp_remesh/README.md):

```bash
git submodule update --init --recursive open4d/codecs/qndf/ssp_remesh/libigl
```

Step 2. Install the repository-wide environment. QNDF reads and writes OBJ
through `open4d.torch_ops`, which replaced this codec's earlier PyTorch3D
dependency, so there is no compiled mesh extension to install:

```bash
conda env create -f ../../../environment.yml   # or: conda activate open4d
pip install -e ../../..
```

Step 3. No additional Python install is required; `dahuffman` and `tqdm` are in
the shared codec environment.

Step 4. Create `objs_original/` and place the `.obj` files to be compressed in
it. QNDF does not ship its own input meshes — this codec's `.gitignore` excludes
`*.obj` — so source your own or point `--source-dir` at another repository
fixture, such as the TVMC basketball sequence used below.

Step 5. Run:

 ```python compress.py [mesh name] -ns [number of subdivisons] -cs [coarse mesh size] -hd [hidden dim size of INR] -nl [layers in INR]```

For Example:

 ```python compress.py pegasus -ns 3 -cs 7000 -hd 96 -nl 32```

The encoder retains the self-contained decoder input as `best_model.pth` in
`--output-dir` (or the current directory) and logs it to MLflow. Pass that file
to `decode.py`; `--keep-artifacts` additionally retains reconstruction files.

## Disconnected meshes (basketball player)

The original SSP preprocessing can collapse small disconnected parts or project
them onto an unrelated surface. Use `build_dataset_open3d.py` for disconnected
meshes. It removes zero-area triangles, allocates the coarse-face budget across
all connected components, and simplifies, subdivides, and projects each component
independently. A JSON transform is saved beside the training pair so decoded
vertices can be restored to the input coordinate system.

Run the complete basketball sequence with:

```bash
conda activate open4d
mkdir -p outputs/basketball_sequence_qndf
nohup python run_basketball_sequence.py \
  --source-dir ../tvmc/arap-volume-tracking/data/basketball_player \
  --gpus 0,1 > outputs/basketball_sequence_qndf/runner.log 2>&1 &
```

The sequence runner is resumable and records per-frame state in
`outputs/basketball_sequence_qndf/status.json`. Each completed frame retains its
training log, normalization transform, normalized reconstruction, and decoded
mesh restored to the original scale.
