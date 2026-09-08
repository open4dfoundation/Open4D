# ReRF

[ReRF](https://github.com/aoliao12138/ReRF) (CVPR 2023) as a streamable
compression method inside Open4D.

> Liao Wang, Qiang Hu, Qihan He, Ziyu Wang, Jingyi Yu, Tinne Tuytelaars,
> Lan Xu, Minye Wu. **"Neural Residual Radiance Fields for Streamably
> Free-Viewpoint Videos."** CVPR 2023.

## What this is

ReRF represents a moving scene as a 3D grid of feature vectors plus one small
neural network shared by the whole video. A pixel is produced by marching a ray
through the grid and asking the network for colour and density at each sample.
Compression is a key frame followed by residual frames, where each residual is
a motion field plus a leftover that is compressed the way a JPEG is.

Two consequences shape everything here:

- **Its bitstream cannot be decoded in a browser.** The entropy coder ships
  only as a prebuilt CPython 3.8 binary with no published sources. So ReRF is
  decoded where the GPU is, and what reaches a viewer is either pixels or
  geometry read out of the field — never the bitstream itself.

  This is *not* a limit on the viewpoint, and an earlier version of this file
  said it was, which was wrong. ReRF is free-viewpoint — it is in the paper's
  title, and `upstream/rerf_render.py --render_360` walks a ring around the
  subject, advancing the frame as the camera moves. `export.py --orbit N`
  drives exactly that. The constraint is *where* the pixels are made: the march
  runs in PyTorch over hand-written CUDA kernels (`upstream/lib/cuda/`), so a
  free camera means a GPU on the far end of a live connection, or — for
  on-demand delivery — rendering the ring in advance and picking the nearest
  view. `streamer`'s viewer does the second.

  `--orbit N --elevations 0,25,-25` renders N views on each of three rings and
  writes them as one rig, so a drag moves in azimuth *and* elevation. The tilt
  is spherical rather than a lift: the cameras stay at the ring's radius from
  the subject, because lifting them would put them `sqrt(r²+h²)` away and the
  subject would shrink on the tilted rings — which, in a viewer that snaps
  between rings, would read as the reconstruction changing size. Measured on
  `g_basketball`: all three rings at radius 2.0000, elevations exactly 0 and
  ±25°, every camera aiming at the centre to within 1e-6°.

  Cost is a render per ring: 72 views × 30 frames is 181 s and 109 MB, so three
  rings is about 9 minutes and 330 MB. Cheap enough that density is a choice
  rather than a constraint — which is the point, since it is all offline.

  What Vega has and ReRF does not is geometry *as its native output*. ReRF can
  still be **read out** as geometry, which I also got wrong at first: upstream's
  `tools/vis_volume.py` thresholds the density grid and takes the occupied
  voxels as a coloured point cloud. `rerf_stream/geometry.py` does the same
  from a decoded bitstream frame rather than from a training checkpoint, so it
  works for every frame a receiver has — see below.
- **Its bottleneck is compute, not bandwidth.** At full resolution a frame costs
  ~25 ms to entropy-decode and ~90 ms to ray-march: about 8 fps, and 2.6 Mbit/s
  of JPEG out. The link is never the constraint; the ray-march is.

## Reading the field out as geometry

```bash
python -m rerf_stream.geometry --config <run>/config.py \
    --compression-path <run>/rerf --out ~/rerf-points \
    --name g_basketball --scene basketball --threshold 0.35
```

Writes one point-cloud clip — binary PLY per frame, float x/y/z and uchar
colour — which `streamer.adopt` takes as a `points` clip. Because it is
geometry, the browser rasterises any viewpoint: a genuinely free camera, no
pre-rendered ring and nothing to snap to.

Two details differ from upstream's tool on purpose. Voxel centres come from
`linspace(xyz_min, xyz_max, shape)`, the convention `lib/dvgo.py` samples
against; `vis_volume.py` uses `xyz / shape * (max - min) + min`, half a voxel
off, which is fine for a viewer and not for geometry that must line up with
another method's. And colour comes from the rgb network, because `k0` here is
12 feature channels rather than RGB — that network is view-dependent, so the
colour is baked at one azimuth and frozen, the same compromise the Vega
`.splat` export makes.

Measured on `g_basketball`:

| | points | MB/frame | vs photograph |
| --- | --- | --- | --- |
| threshold 0.20 | 146,057 | 2.19 | 31.01 dB |
| **threshold 0.35** | **110,813** | **1.66** | **31.27 dB** |
| threshold 0.50 | 77,710 | 1.17 | 29.96 dB |
| the ray-march itself | — | 0.05 (JPEG) | 45.53 dB |

So the conversion costs about 14 dB. Thresholding a continuous density field
into occupied-or-not discards the soft edges a volume render integrates over,
and no threshold buys them back — 0.35 is simply the best of the three. The
figures are from projecting the points into training camera 0 with a nearest-z
point rasteriser, which is *this* rasteriser and not the browser's, so treat
them as the cost of the representation rather than of any particular renderer.

Placement is exact, which is the part that had to be checked: against Vega's
own frame 0, per-axis bounding-box overlap/union is 0.985 / 0.998 / 0.986, and
2 of 63,305 Gaussians fall outside the ReRF box (mean opacity 0.031 — two faint
floaters).

Extraction is nearly free next to the render: 30 frames in **1 second**, where
ray-marching one 72-view ring of the same 30 frames takes 181 s.

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

`upstream/` is a clone of the repository above at `510e607`, with four things
deleted, none of them code: `.git/`, `ac_dc/`'s CMake intermediates (11 MB of
object files and cache recording paths on the machine that built it), and a
3.5 MB README image. That takes it from 18 MB to 3.0 MB. Every remaining file
is upstream's, unchanged; anything that would otherwise be a patch lives in
`rerf_stream/env.py`.

## Environment

Python **3.8**, because `ac_dc/ncvv_ac_dc.cpython-38-*.so` has no sources and
cannot be rebuilt for a newer interpreter. Plus torch with CUDA, mmcv, bitarray,
Pillow, numpy. On this machine that is the `nevo` conda environment.

## Reading a bitstream

```python
from rerf_stream import BitstreamPlayer, captured_image, psnr

player = BitstreamPlayer(run / "config.py", run / "rerf", group_size=30)
for frame in player.play(loop=False):
    image = player.render(player.cameras()[0])
```

The decode is **sequential** by construction: a residual frame means nothing
until every frame since the group's key frame has been decoded in order. So
`play()` yields in order and, with `loop=True`, restarts at the key frame
rather than seeking — which is what a looping client does.

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

`origin="rendered"` is a claim the code has to earn: every frame served was
entropy-decoded and ray-marched while the viewer was watching.

## The rate ladder

Measured on `g_basketball`, one RTX 4090, via `--report-ladder`:

| rung | resolution | JPEG q | fps | kB/frame | Mbit/s |
| --- | --- | --- | --- | --- | --- |
| high | 1280×960 | 92 | 8.1 | 40.8 | 2.6 |
| medium | 640×480 | 88 | 20.4 | 12.8 | 2.1 |
| low | 320×240 | 80 | 25.9 | 4.1 | 0.9 |

Note what that table says, because it is the opposite of what adaptive
streaming usually assumes. Dropping from high to medium cuts bytes per frame by
3.2× but bitrate only by 1.2×, because the frame rate more than doubles. **Live,
a rung mostly buys frame rate, not bandwidth** — the ray-march is the
constraint, not the link, so adapting this method is trading GPU time. Even at
its best it emits 2.6 Mbit/s, which streams over anything.

A *prepared* clip is different: it plays at the bundle's fixed fps, so there the
same rung cuts bitrate by the full 3.4× (see the table below). Same content,
two different scarcities, depending on whether the frames are being made now.

**For a server-rendered method the ladder is pixels, not features.** What
travels is JPEG, so what a receiver can be offered is resolution and JPEG
quality — both available on a bitstream that already exists. Changing the
*bitstream's* quality would mean re-encoding, which needs the training
checkpoints (see below).

The same rungs can be written into a bundle as a clip's quality levels, so
something downstream can choose between them:

```bash
python -m rerf_stream.export --config <run>/config.py \
    --compression-path <run>/rerf --out ~/rerf-clips \
    --name g_thomas --scene thomas --depth --captured \
    --rungs high,medium,low
```

The highest rung becomes the clip's default rendition — a reader that knows
nothing about rungs then sees the method at its best — and the others become
variants beside it. Scored with `python -m streamer.metrics`, on `g_thomas`:

| rung | resolution | kB/frame | Mbit/s @30 | PSNR | SSIM |
| --- | --- | --- | --- | --- | --- |
| default | 1280×960 q92 | 33.5 | 8.04 | 45.91 | 0.9902 |
| medium | 640×480 q88 | 9.9 | 2.38 | 43.72 | 0.9853 |
| low | 320×240 q80 | 3.0 | 0.73 | 40.45 | 0.9723 |

A lower rung is **resampled from the same ray-march**, not marched again at a
lower resolution. Re-marching would sample the volume differently and give a
slightly different picture — fine as an image, wrong as a rendition, because
two renditions have to be the same content for a switch between them to be a
rate change rather than a visible cut. It is also free: the march is the
expensive step at ~90 ms, and a resize is not.

## Reproducibility

Re-rendering the same frame at the same camera is **reproducible but not
bit-identical**: the ray-march sums along each ray with CUDA reductions, whose
accumulation order is not fixed. Measured by re-exporting `g_basketball`, 73 of
480 frames differed, by at most 2 levels of 255 on 18–185 of 3.7 million
samples — 0.003%, which moves PSNR by far less than 0.01 dB. So "diff the two
exports" is not a valid check that nothing changed; compare the numbers
`streamer.metrics` reports instead.

## What is missing, and why

**Encoding.** `upstream/codec/compress.py` turns a trained sequence into a
bitstream, and its `--quality` argument is the ideal rate knob: one trained
model, as many rungs as you like, real rate–distortion. It needs
`--model_path`, the per-frame training checkpoints — and those were deleted
once the bitstreams existed. There are none left on this machine. Producing a
bitstream-level ladder therefore requires retraining first
(`upstream/run.py`), which is hours of GPU time per object.

The bitstreams that do exist are complete and play: 30 frames, 16 MB, encoded
at `--pca --pca_chs 7,13 --quality 99,98`.

**The NeVo layer.** This directory previously also implemented
[NeVo](https://doi.org/10.1145/3636534) (MobiCom 2025) — neural-visibility
scoring, voxel filtering, an importance CDF and a trace-driven arrival
simulator. It was removed deliberately: NeVo has no released code, so it was a
reimplementation of a paper rather than a usable artifact; its measured results
depended on the training checkpoints that no longer exist; and none of it is
needed to stream ReRF. It is in git history at `3d33655` under
`open4d/reconstruction/nevo/` if any of it is wanted back.

## Licence

ReRF is released for non-commercial use only, and its code base derives from
DVGO. See `upstream/LICENSE`.
