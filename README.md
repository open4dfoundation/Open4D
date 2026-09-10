# Open4D

A Python library for research codecs, reconstruction and streaming of 4D geometry.
Mesh sequences contain one 3D mesh per timestamp. Gaussian splats have their own
representation and research backends.

## Install

From this checkout:

```bash
python -m pip install -e .
```

The base package needs NumPy and supports Python 3.10 through 3.13. Install only
the extras you use:

```bash
python -m pip install -e '.[player]'    # interactive mesh viewer and GIF export
python -m pip install -e '.[open3d]'    # RGB-D reconstruction
python -m pip install -e '.[gaussians]' # read Gaussian PLY files
```

Research methods have additional setup below. Their source, native programs
and model weights are not bundled in the Python wheel.

## Try a sequence

No dataset is needed for this example:

```python
import open4d
from open4d.demo import mesh_sequence

sequence = mesh_sequence(frames=30)
open4d.visualize(sequence)
```

For your own OBJ or PLY frames, replace the second line with:

```python
sequence = open4d.load("my_frames", fps=30)
```

Frames are sorted by filename. Use zero-padded names such as `frame_0001.obj`.
Each frame has `frame_index`, `timestamp` in seconds, and `geometry`.
Mesh geometry contains `positions`, `triangles`, and optional colors, normals,
texture coordinates and custom attributes.

## Encode and decode

Choose a research codec explicitly. For example, after setting up V-DMC:

```python
encoded = open4d.encode(sequence, "wave.v4d", codec="vdmc")
decoded = open4d.decode(encoded)
open4d.visualize(decoded)
decoded.close()
```

`encode` also accepts an input folder. `decode` reads the codec from its
artifact. Close a decoded mesh sequence when finished, or use `with`:

```python
with open4d.decode("wave.v4d") as decoded:
    print(len(decoded), "frames")
```

The [notebook](examples/open4d_sequence_codec.ipynb) walks through these calls,
reconstruction and streaming in separate short cells.

| Codec | Input | Output | Backend setup |
| --- | --- | --- | --- |
| `vdmc` | Mesh sequence | `.v4d` | Build the V-DMC submodule and configure its encoder and decoder |
| `faster_vdmc` | Mesh sequence | `.v4d` | Build the faster V-DMC submodule and configure its encoder and decoder |
| `tvmc` | Mesh sequence | `.tvmc` directory | [TVMC setup](open4d/codecs/tvmc/README.md) |
| `tsmc` | Mesh sequence | `.tsmc` directory | [TSMC setup](open4d/codecs/tsmc/README.md) |
| `klt` | Mesh sequence converted to TSDF volumes | `.k4d` | Research source and `.[klt]` |
| `n4mc` | Mesh sequence converted to TSDF volumes | `.n4d` | Research source and `.[n4mc]` |
| `qndf`, `qndf-int8` | Mesh frames | `.q4d`, `.qi4d` | Research source and `.[qndf]` |
| `vega` | Gaussian splat frames | `.vega` directory | [Vega CUDA environment](open4d/reconstruction/vega/README.md) |

QNDF-int8 now writes version 2 artifacts. Version 1 artifacts must be encoded again.

KLT, N4MC and QNDF run in Python. TVMC, TSMC, V-DMC and Gaussian methods use
separate research runtimes. N4MC and QNDF currently process frames independently;
they remain available as research methods. Mesh codecs currently encode geometry
only and reject attributes they cannot preserve.

`open4d.available_codecs()` lists adapters, including those whose optional
backend is not installed. Draco and generic array compressors remain in the
research/benchmark code, outside the public codec choices.

For an installed wheel, point KLT, N4MC and QNDF at a source checkout:

```bash
export OPEN4D_RESEARCH_ROOT=/path/to/Open4D
```

V-DMC needs the paths to its built programs:

```bash
export OPEN4D_VDMC_ENCODER=/path/to/vmeshEncoder
export OPEN4D_VDMC_DECODER=/path/to/vmeshDecoder
```

Use `OPEN4D_FASTER_VDMC_ENCODER` and `OPEN4D_FASTER_VDMC_DECODER` for the faster
fork. Encoder and decoder configurations can be supplied with
`encoder_config=` and `decoder_config=`. TVMC and TSMC accept `backend=` for
the method's source directory and `python=` for its environment. Codec options
are ordinary keyword arguments to `encode` or `decode`.

## Reconstruct from depth images

```python
import numpy as np
import open4d

# A small synthetic camera looking at a flat surface one metre away.
depth = np.full((3, 48, 64), 1000, dtype=np.uint16)
sequence = open4d.reconstruct(depth, intrinsics=(60, 60, 31.5, 23.5))
open4d.visualize(sequence)
```

For real captures, `depth` has shape `(frames, height, width)` and is in
millimetres by default. Zero means missing depth. Pass `color=rgb` for aligned
RGB images with shape `(frames, height, width, 3)` and dtype `uint8`.
`intrinsics=(fx, fy, cx, cy)` must come from your camera calibration; the values
above belong only to the synthetic example. Use `depth_scale=1` for metres.

Moving cameras need `camera_poses=`: camera-to-world 4 by 4 matrices with
translation in metres. Several cameras can contribute to each frame. Each
timestamp is reconstructed separately so motion is preserved.

## Stream mesh frames

Run the receiver first in one Python process:

```python
from open4d import receive

with receive() as frames:
    for frame in frames:
        print(frame.frame_index, len(frame.geometry.positions), "vertices")
```

Then send from another:

```python
from open4d import stream
from open4d.demo import mesh_sequence

stream(mesh_sequence(frames=30))
```

This sends decoded mesh arrays over TCP, at their recorded frame timing. It is
not a compression method. Both calls default to this computer on port 7000.
Pass `host=` and `port=` for another address. Remote transport needs a trusted
network or SSH tunnel; this protocol has no authentication or encryption.
Use `realtime=False` to transfer a recorded sequence as fast as possible.

The camera capture and native reconstruction programs are in
[open4d/streaming](open4d/streaming/README.md), formerly `reconstruction/rgbd`.

## Gaussian splats

Read splat frames, then encode them with the local Vega adaptation:

```python
from open4d import load_gaussians, encode, decode

frames = [load_gaussians("frame_0000.ply"), load_gaussians("frame_0001.ply")]
encoded = encode(frames, "capture.vega", codec="vega")
decoded = decode(encoded)
```

A `GaussianSplats` frame contains `positions` `(N, 3)`, positive `scales`
`(N, 3)`, normalized `rotations` `(N, 4)` in wxyz order, `opacities` `(N,)`
between 0 and 1, and `spherical_harmonics` `(N, K, 3)`. The PLY reader converts
stored log scales and opacity logits to these values.

Vega uses a learned color model. Its decoded `NeuralGaussianFrame` retains
that model in `appearance`; call `frame.appearance.colors(directions)` to get
RGB values for camera-to-splat directions. The current adapter supports one
native group per run and rejects output requiring several color models.
It does not replace the learned colors with invented SH coefficients.

QUEEN and 3DGStream reconstruct splats from calibrated camera images. After
[setting up their runtime](open4d/reconstruction/gs_tools/README.md):

```python
run = open4d.reconstruct("my_scene", "queen_output", method="queen")
video = run.render()
```

`method="3dgstream"` selects 3DGStream. Each method requires its own input
layout and CUDA environment. `runtime=` selects the `gs_tools` directory and
`python=` its Python interpreter. Vega uses its own source directory through
`runtime=` or `OPEN4D_VEGA_ROOT`. `run.load_frame(0)` reads a saved dense PLY;
it does not decode a compressed temporal residual. 3DGStream rendering still
requires its native viewer. The Qt viewer and TCP stream currently take meshes.

## Other tools

- `open4d demo`, `open4d inspect` and `open4d view` provide command-line access.
- `open4d.io.write_sequence` exports mesh folders; `open4d.save` writes OpenUSD
  or explicitly selected codec artifacts. There is no default `.o4d` encoder.
- `open4d.compare_sequences` measures mesh error with the `.[metrics]` extra.
- [Viewer examples](examples/visualization/README.md) include GIF export and comparisons.
- [Contributor setup and tests](CONTRIBUTING.md) cover optional dependencies and packaging.

The general `.o4d` format is separate work. Existing codec-specific formats
remain in use. Publication is still blocked by the unresolved component rights
in [THIRD_PARTY.md](THIRD_PARTY.md); preparing the package does not resolve them.
