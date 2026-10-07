# ReRF

[ReRF](https://github.com/aoliao12138/ReRF) (CVPR 2023) as a streamable
compression method inside Open4D.

> Liao Wang, Qiang Hu, Qihan He, Ziyu Wang, Jingyi Yu, Tinne Tuytelaars,
> Lan Xu, Minye Wu. **"Neural Residual Radiance Fields for Streamably
> Free-Viewpoint Videos."** CVPR 2023.

## Runtime and outputs

ReRF stores a feature grid and shared neural network, with key frames followed
by motion and residual frames. Decoding and ray-marching require CUDA and Python
3.8: the entropy coder is distributed as a prebuilt CPython 3.8 binary without
sources.

Browser delivery supports server-rendered JPEGs, prepared camera rings, or
point clouds extracted from the density field. The browser cannot decode the
native bitstream.

`export.py --orbit N --elevations 0,25,-25` renders three camera rings at a
constant radius. The viewer selects the nearest prepared view. A continuously
free camera requires server rendering or point-cloud extraction.

## Reading the field out as geometry

```bash
python -m rerf_stream.geometry --config <run>/config.py \
    --compression-path <run>/rerf --out ~/rerf-points \
    --name g_basketball --scene basketball --threshold 0.35
```

Writes binary PLY frames with float positions and byte colours, importable with
`streamer.adopt` as a `points` clip. Point clouds support arbitrary browser
viewpoints, with lower fidelity than ray-marched images.

Voxel centres use `linspace(xyz_min, xyz_max, shape)` to match `lib/dvgo.py`.
Colour is evaluated by the view-dependent RGB network at one azimuth and baked.
Positions are exported in world coordinates; scoring converts them back to the
normalised coordinates used by `cams_*.json`.

`geometry.fidelity` records point-cloud and ray-march PSNR against the captured
image in each exported clip. Point-cloud scoring uses `geometry.rasterise`.

## Layout

```
upstream/              ReRF, byte-identical to its published tree
rerf_stream/
  env.py               make upstream importable (see the module docstring)
  cameras.py           viewpoints, near/far planes, captured images, PSNR
  bitstream.py         decode the bitstream frame by frame, ray-march a view
  mjpeg.py             push JPEG frames over multipart/x-mixed-replace
  serve.py             the live stream, and the rate ladder
  export.py            render prepared clips, at one quality or several
  geometry.py          read the density field out as a coloured point cloud
rerf_stream_tests/
```

`upstream/` is pinned at `510e607`. Non-code removals comprise `.git/`,
`ac_dc/` CMake intermediates, and a README image. Local adaptation is in
`rerf_stream/env.py`; see the root `THIRD_PARTY.md` for provenance.

## Environment

Python **3.8**, because `ac_dc/ncvv_ac_dc.cpython-38-*.so` has no sources and
cannot be rebuilt for a newer interpreter. Plus torch with CUDA, mmcv, bitarray,
Pillow and NumPy.

## Reading a bitstream

```python
from rerf_stream import BitstreamPlayer, captured_image, psnr

player = BitstreamPlayer(run / "config.py", run / "rerf", group_size=30)
for frame in player.play(loop=False):
    image = player.render(player.cameras()[0])
```

Residual decoding is sequential within each group. `play()` yields frames in
order; `loop=True` restarts at the key frame.

## Streaming it

```bash
python -m rerf_stream.serve \
    --config <run>/config.py --compression-path <run>/rerf --port 8802 --orbit
```

Then attach it to a bundle:

```python
from streamer import live
live.attach(bundle_dir, live.mjpeg(
    "http://127.0.0.1:8802/stream", name="rerf-live", origin="rendered"))
```

`origin="rendered"` identifies frames decoded and ray-marched during playback.

## The rate ladder

Live frame rate is limited by decoding and ray-marching. Lower resolution
increases frame rate as well as reducing frame size, so live bitrate falls less
than it does for prepared clips at fixed FPS. Rungs vary JPEG quality and image
resolution; changing native bitstream quality requires re-encoding from training
checkpoints.

Export the rungs into a bundle:

```bash
python -m rerf_stream.export --config <run>/config.py \
    --compression-path <run>/rerf --out ~/rerf-clips \
    --name g_thomas --scene thomas --depth --captured \
    --rungs high,medium,low
```

The highest rung is the default. Lower rungs are resampled from the same
ray-march to keep content consistent across quality switches.

## Reproducibility

CUDA reduction order makes repeated ray-marches non-deterministic. Compare
quality metrics rather than requiring byte-identical renders.

## Encoding

`upstream/codec/compress.py --model_path ... --quality ...` encodes trained
per-frame checkpoints. Existing bitstreams can be decoded without those
checkpoints; producing new bitstream quality levels requires the checkpoints or
retraining with `upstream/run.py`.

The former NeVo adaptation remains in Git history at `3d33655` under
`open4d/reconstruction/nevo/`.

## Licence

ReRF is released for non-commercial use only, and its code base derives from
DVGO. See `upstream/LICENSE`.
