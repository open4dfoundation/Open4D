# Open4D

A Python library for research codecs, reconstruction and streaming of 4D geometry.
Mesh sequences contain one 3D mesh per timestamp. Gaussian splats have their own
representation and research backends.

## Install

The PyPI release is being prepared. Until the
[release ledger](https://github.com/open4dfoundation/Open4D/blob/main/THIRD_PARTY.md)
is cleared, install a source checkout with `python -m pip install -e .`.
After publication:

```bash
python -m pip install open4d
```

The base package needs NumPy and supports Python 3.10 through 3.13. Install only
the extras you use:

```bash
python -m pip install 'open4d[player]'    # interactive mesh viewer and GIF export
python -m pip install 'open4d[usd]'       # OpenUSD (.usdc) files
python -m pip install 'open4d[metrics]'   # compare_sequences
python -m pip install 'open4d[open3d]'    # RGB-D reconstruction (Open3D 0.19.x, Python 3.12 or older)
python -m pip install 'open4d[gaussians]' # read Gaussian PLY files
```

For development, install a checkout with `python -m pip install -e '.[dev]'`.

Research methods have additional setup below. Their source, native programs
and model weights are not bundled in the Python wheel. Hardware, environments
and per-module requirements are in [docs/requirements.md](https://github.com/open4dfoundation/Open4D/blob/main/docs/requirements.md).

RGB-D reconstruction requires Open3D 0.19.x. The legacy TSDF integrator in
Open3D 0.20 rescales already-metric float depth and can return empty meshes;
the extra selects the supported version and reconstruction rejects an
incompatible manually installed runtime before processing frames.

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
encoded = open4d.encode(sequence, "wave.o4d", codec="vdmc")
decoded = open4d.decode(encoded)
open4d.visualize(decoded)
decoded.close()
```

`encode` also accepts an input folder. `decode` reads the codec from its
artifact. Close a decoded mesh sequence when finished, or use `with`:

```python
with open4d.decode("wave.o4d") as decoded:
    print(len(decoded), "frames")
```

The [notebook](https://github.com/open4dfoundation/Open4D/blob/main/examples/open4d_sequence_codec.ipynb) walks through these calls,
reconstruction and streaming in separate short cells.
Short `.o4d` examples:

- [Preserve compressed state through USDC](https://github.com/open4dfoundation/Open4D/blob/main/examples/o4d/01_container_and_usdc.ipynb)
- [The eight mesh codecs](https://github.com/open4dfoundation/Open4D/blob/main/examples/o4d/02_mesh_codecs.ipynb)
- [Vega, QUEEN and 3DGStream](https://github.com/open4dfoundation/Open4D/blob/main/examples/o4d/03_gaussian_codecs.ipynb)
- [ReRF](https://github.com/open4dfoundation/Open4D/blob/main/examples/o4d/04_rerf.ipynb)

| Codec | Input | Output | Backend setup |
| --- | --- | --- | --- |
| `vdmc` | Mesh sequence | `.o4d` | Build the V-DMC submodule and configure its encoder and decoder |
| `faster_vdmc` | Mesh sequence | `.o4d` | Build the faster V-DMC submodule and configure its encoder and decoder |
| `tvmc` | Mesh sequence | `.o4d` | [TVMC setup](https://github.com/open4dfoundation/Open4D/blob/main/open4d/codecs/tvmc/README.md) |
| `tsmc` | Mesh sequence | `.o4d` | [TSMC setup](https://github.com/open4dfoundation/Open4D/blob/main/open4d/codecs/tsmc/README.md) |
| `klt` | Mesh sequence converted to TSDF volumes | `.o4d` | Research source and `.[klt]` |
| `n4mc` | Mesh sequence converted to TSDF volumes | `.o4d` | Research source and `.[n4mc]` |
| `qndf`, `qndf-int8` | Mesh frames | `.o4d` | Research source and `.[qndf]` |
| `vega` | Gaussian splat frames or native run | `.o4d` | [Vega CUDA environment](https://github.com/open4dfoundation/Open4D/blob/main/open4d/reconstruction/vega/README.md) |
| `queen`, `3dgstream`, `rerf` | Native temporal research output | `.o4d`, with native USDC interchange | Method-specific CUDA runtime for evaluation |

All twelve public codecs write standalone `.o4d` files whose profile identifies
the codec. Existing native outputs can be packed without recompression. Standard
PLY, OBJ and USD import/export remain available.

Mesh codecs decode to mesh sequences for ordinary USDC geometry export.
To preserve a compressed artifact exactly, construct `open4d.NativeSequence`
and save it to USDC using the custom `O4D` prim, as shown in the
[container notebook](https://github.com/open4dfoundation/Open4D/blob/main/examples/o4d/01_container_and_usdc.ipynb). This works with
every O4D profile. Gaussian and field methods load as `NativeSequence` and
evaluate their native models explicitly. N4MC shares a model across independent
frame latents; QNDF has independent frame models.
[O4D](https://github.com/open4dfoundation/Open4D/blob/main/docs/api.md#o4d-format) is a custom container for native codec payloads,
including V-DMC encoder bitstreams; the container itself is not MPEG V-DMC
interchange. Convert older artifacts with `open4d.migrate_legacy`.

KLT, N4MC and QNDF run in Python. TVMC, TSMC, V-DMC and Gaussian methods use
separate research runtimes. N4MC and QNDF currently process frames independently;
they remain available as research methods. Mesh codecs currently encode geometry
only and reject attributes they cannot preserve.

N4MC's automatic device selection uses CPU when the installed Apple Metal/MPS
runtime lacks `ConvTranspose3D`. An explicit unsupported `device="mps"` request
raises a diagnostic before training; use `device="cpu"` or `device="auto"`.

`open4d.available_codecs()` lists adapters, including those whose optional
backend is not installed. Draco and generic array compressors remain in the
research/benchmark code, outside the public codec choices.

For an installed wheel, point KLT, N4MC and QNDF at a source checkout:

```bash
export OPEN4D_RESEARCH_ROOT=/path/to/Open4D
```

V-DMC needs the paths to its built programs:

```bash
export OPEN4D_VDMC_ENCODER=/path/to/o4dEncoder
export OPEN4D_VDMC_DECODER=/path/to/o4dDecoder
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

Saved two-camera captures load with `capture = open4d.load_rgbd_capture(pairs,
calibration)`, then `open4d.reconstruct(capture, refine_poses=True, device="cuda")`;
see [open4d/reconstruction/rgbd](https://github.com/open4dfoundation/Open4D/blob/main/open4d/reconstruction/rgbd/README.md).

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
from open4d import send
from open4d.demo import mesh_sequence

send(mesh_sequence(frames=30))
```

To keep what arrives, record it and save it like any other sequence:

```python
import open4d

with open4d.receive() as receiver:
    recording = receiver.record(duration=10)  # or max_frames=, or until the sender ends
    print(receiver.stats.fps, receiver.stats.bits_per_second)
open4d.save(recording, "capture.usdc")
```

`receiver.close()` from another thread stops a receiver that is waiting.

This sends decoded mesh arrays over TCP, at their recorded frame timing. It is
not a compression method. Both calls default to this computer on port 47004.
Pass `host=` and `port=` for another address. Remote transport needs a trusted
network or SSH tunnel; this protocol has no authentication or encryption.
Use `realtime=False` to transfer a recorded sequence as fast as possible.

The camera capture and native reconstruction programs are in
[open4d/reconstruction/rgbd](https://github.com/open4dfoundation/Open4D/blob/main/open4d/reconstruction/rgbd/README.md).

## Compare methods in a user study

[open4d/streamer/study](https://github.com/open4dfoundation/Open4D/blob/main/open4d/streamer/study/README.md) is a browser app that runs one
participant through several streaming methods — Ours, ViVo, NAVA and Vega —
under the same network trace, from the same start view, along the same camera
path, and asks them to rate each clip. The trace can be uploaded in the browser;
the results export as CSV.

```bash
cd open4d/streamer/study/system/WebClient && npm install && node build.js
cd ../.. && PYTHONPATH=/path/to/4DVideoStreaming PYTHON_BIN=<env-python> scripts/run_web_demo.sh
# then open http://<host>:3000/web/
```

It needs a checkout of the 4DVideoStreaming research repo for the ladder solver
and the baseline servers.

## Gaussian splats

Read splat frames, then encode them with the local Vega adaptation:

```python
from open4d import load_gaussians, encode, decode

frames = [load_gaussians("frame_0000.ply"), load_gaussians("frame_0001.ply")]
encoded = encode(frames, "capture.o4d", codec="vega")
native = decode(encoded)  # no CUDA needed to inspect native state
decoded = native.decode()  # requires the Vega runtime
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
[setting up their runtime](https://github.com/open4dfoundation/Open4D/blob/main/open4d/reconstruction/gs_tools/README.md):

```python
run = open4d.reconstruct("my_scene", "queen_output", method="queen")
video = run.render()
```

`method="3dgstream"` selects 3DGStream. Each method requires its own input
layout and CUDA environment. `runtime=` selects the `gs_tools` directory and
`python=` its Python interpreter. Vega uses its own source directory through
`runtime=` or `OPEN4D_VEGA_ROOT`. `run.load_frame(0)` reads a saved dense PLY;
it does not decode a compressed temporal residual. A 3DGStream frame includes the
Gaussians its second stage added. 3DGStream rendering still requires its native
viewer. The Qt viewer and TCP stream currently take meshes.

Without `config=`, QUEEN uses upstream's `dynerf.yaml`, saved as
`queen_config.yaml`, with MiDaS depth priors disabled. Enable them with
`depth_priors=True` and the separate interpreter shown below.
3DGStream defaults to the paper's per-frame schedule in
`gs_tools/configs/3dgstream/paper.json`; arguments that Open4D passes, and
`options=`, take precedence over a config file.

### ORBIT captures

Both methods accept an ORBIT object folder or a scene read with `load_orbit`.
Converted inputs are saved in `output/input`:

```python
scene = open4d.load_orbit("ORBIT_datasets_gaussian", "basketball")
print(len(scene), [camera.view_id for camera in scene.cameras])
run = open4d.reconstruct(scene, "basketball_queen", method="queen", frames=10)
```

`frames=` takes a count or a contiguous range, `max_width=` downscales wider
images (1600 pixels by default), and `test_views=` lists the cameras held out
for evaluation (the first by default; `()` trains on every camera). Initial points
fill the object's bounds. On a black background, `initial_points="carve"` uses
the first frame's visual hull instead: the object itself comes out more
accurate, but the hull's excess volume adds background haze and Gaussians.
Cameras must be fixed, undistorted,
with square pixels and a centred principal point. Reading images needs the
`gaussians` extra.

MiDaS requires `timm==0.6.13`. Pass an interpreter with its requirements installed
to compute and cache depth priors:

```python
run = open4d.reconstruct(scene, "basketball_queen", method="queen",
                         depth_priors=True, depth_python="/path/to/midas-env/bin/python")
```

QUEEN's depth initialisation also fills uncovered black backgrounds with points;
`reconstruct` warns for black-background ORBIT captures.

## Other tools

- `open4d demo`, `open4d inspect` and `open4d view` provide command-line access.
  `open4d inspect capture.o4d` reads the codec, timing and payload sizes of
  any `.o4d` without its codec backend; `--decode` also reports geometry.
- `open4d.io.write_sequence` exports mesh folders; `open4d.save` writes OpenUSD
  or `.o4d` with an explicitly selected codec.
- `open4d.compare_sequences("input_frames/", "capture.o4d")` measures mesh
  error between sequences or paths with the `.[metrics]` extra; see
  [comparing sequences](https://github.com/open4dfoundation/Open4D/blob/main/docs/api.md#comparing-sequences).
- [Viewer examples](https://github.com/open4dfoundation/Open4D/blob/main/examples/visualization/README.md) include GIF export and comparisons.
- [Contributor setup and tests](https://github.com/open4dfoundation/Open4D/blob/main/CONTRIBUTING.md) cover optional dependencies and packaging.

Publication is still blocked by the unresolved component rights
in [THIRD_PARTY.md](https://github.com/open4dfoundation/Open4D/blob/main/THIRD_PARTY.md); preparing the package does not resolve them.
