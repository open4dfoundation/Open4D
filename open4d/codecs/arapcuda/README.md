# arapcuda

GPU version of TSMC's ARAP volume tracker, as a Python package.

It places a fixed number of points (centers) inside the first mesh's volume and follows them through the rest of the sequence. The centers stay evenly spread and neighbouring centers move together rigidly.

## Install

Needs an NVIDIA GPU, CUDA 12 and CMake 3.24 or newer.

```bash
CUDACXX=/usr/local/cuda/bin/nvcc pip install .
```

## Use

```python
import arapcuda

tracker = arapcuda.Tracker(centers=2000)
for vertices, triangles in meshes:          # numpy (V, 3) and (T, 3)
    frame = tracker.add_frame(vertices, triangles)
    frame["centers"]      # (2000, 3) center positions
    frame["transforms"]   # (2000, 3, 4) rotation and translation since the previous frame (None for frame 0)
```

`arapcuda.track(meshes)` tracks the whole sequence at once and returns arrays of shape (F, N, 3) and (F, N, 3, 4).

See [examples/track_obj_folder.py](examples/track_obj_folder.py) for a complete script.

From the command line, this writes `centers_XXX.xyz` and `transforms_XXX.txt` for each frame:

```bash
python -m arapcuda MESH_DIR OUT_DIR
```

Meshes must be closed (watertight).

## Test

```bash
pip install pytest && pytest tests
```
