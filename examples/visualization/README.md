# Open4D examples

Two programs on one loader:

| | |
| --- | --- |
| `visualize_sequence.py` | Play one 4D sequence |
| `compare_sequences.py` | Put a decoded sequence beside its reference and colour it by error |

Both read a single sequence file or an imported folder of per-frame meshes, and
either one run with no arguments lists every format it accepts.

## Quick start

```bash
python -m pip install -e '.[player]'          # PyQt6, pyqtgraph, Pillow, SciPy

# play a sequence
python examples/visualization/visualize_sequence.py my_capture/ --info   # does it load?
python examples/visualization/visualize_sequence.py my_capture/          # play it

# compare a codec's output against the reference
python examples/visualization/compare_sequences.py ref/ decoded/ --info  # numbers only
python examples/visualization/compare_sequences.py ref/ decoded/         # side by side
```

`--info` needs no window, no GL and no display, so it works over ssh. Run it
first: it catches a bad path, a frame-count mismatch, or misaligned frames in a
second, before you wait for a window.

Try it on the 10 basketball frames the TVMC codec vendors:

```bash
python examples/visualization/visualize_sequence.py \
    open4d/codecs/tvmc/arap-volume-tracking/data/basketball_player \
    --up y --fps 10 --azimuth 180
```

That capture faces away at the default azimuth, hence `--azimuth 180`. There is
no universal front, so nothing infers one — find the angle for your subject once
and reuse it. No decoded counterpart is vendored, so to try the comparison, run a
codec over those frames and point the program at both folders.

In either window: drag to orbit, scroll to zoom, drag the slider to scrub, space
to pause, left/right to step, `q` to quit. In the comparison both panes orbit
together. `--save out.gif` renders offscreen through the same renderer, so a
saved GIF looks like what you saw.

## Comparing

Left pane the reference as geometry, right pane the decoded mesh coloured by its
distance from it, under one shared camera — two independently posed views tell you
nothing.

A decoded mesh has its own vertex count and connectivity, so error is a
nearest-neighbour distance, not a per-vertex difference:

| Metric | |
| --- | --- |
| `--metric point` | to the nearest reference vertex (C2C). The default |
| `--metric plane` | that offset projected onto the reference normal (C2P), so error sliding *along* the surface is not counted. The MPEG definition |

Both are one-sided, so every figure is reported in both directions and the
symmetric one is the worse of the two — a codec that deletes a limb scores well
one way round. PSNR uses the reference bounding-box diagonal as its peak, fixed
for the whole sequence so frames stay comparable.

The colour scale is likewise fixed for the whole sequence, at the 99th percentile
of every measured distance by default. Rescaling per frame would make each still
prettier and the animation a lie. Values above the top take the top colour, and
the colourbar labels that end `≥`.

### Reading the numbers

```
  frame      rms d→r      rms r→d      sym rms    hausdorff   sym psnr
      0    0.0181082    0.0183695    0.0183695    0.0315318      61.15
```

| Column | |
| --- | --- |
| `rms d→r` | decoded vertices to the reference surface — how wrong the output is |
| `rms r→d` | the reverse. The one that catches *deleted* geometry, which has nothing near it |
| `sym rms`, `hausdorff`, `sym psnr` | the worse of the two directions. Report these |

Distances are in your source units; the tool does not know whether that is
metres. `--csv out.csv` writes these plus vertex and triangle counts, one row per
frame, for a paper or a regression run.

### When it looks wrong

| Symptom | Cause |
| --- | --- |
| Huge error, near-identical on every frame | Frames misaligned, or the two sequences are in different coordinate frames |
| Error about the size of the subject | The codec re-centred or rescaled. **Nothing is registered or aligned** — no ICP, and `--up` rotates both together so it cannot fix a mismatch *between* them |
| Subject lying on its side | Wrong `--up` |
| `rms d→r` tiny but `rms r→d` large | The codec deleted geometry. Working as intended |
| Error map all bright, or all dark | Colour scale too low or too high: `--percentile 100`, or set `--max-error` |
| Two codecs' figures not comparable | Pass the *same* `--max-error` to both |
| `inf` PSNR | The sequences are identical |
| `nan` PSNR | Degenerate reference with no bounding box, so no scale to normalise against |

Frames pair by ordinal position, and unequal lengths compare up to the shorter
one and say so.

## Sources

| Source | Needs |
| --- | --- |
| Folder of `.obj` or `.ply` frames | nothing |
| Folder of `.stl` `.off` `.glb` `.gltf` frames | `.[tools]` |
| One USD file (`.usd` `.usda` `.usdc` `.usdz`) | `.[usd]` |
| Raw MPEG V-DMC bitstream (`.vmesh`) | a compatible native V-DMC decoder |
| One mesh file | as above |

Frames are ordered by **the last number in the filename**, so `frame_2.obj` comes
before `frame_10.obj` — but `frame_003_qp9.obj` sorts on 9, not 3. A codec that
puts a parameter last will silently misalign every frame and look far worse than
it is; rename before comparing. A frame with no faces is drawn as a point cloud.

### Raw V-DMC bitstreams

The viewer reads a `.vmesh` produced by the MPEG V-DMC reference codec (or a
compatible implementation) directly. Point it at the native decoder first:

```bash
export OPEN4D_VDMC_DECODER=/path/to/vmesh-decoder
python examples/visualization/visualize_sequence.py capture.vmesh --info
python examples/visualization/visualize_sequence.py capture.vmesh
```

If that bitstream needs an external decoder configuration, also set
`OPEN4D_VDMC_DECODER_CONFIG=/path/to/decoder.cfg`. Raw bitstreams do not carry
Open4D's timestamps, metadata, or original coordinate bounds, so they default
to 30 fps and display the coordinates emitted by the decoder. Pass `--fps N`
to supply the real capture rate. The current adapter imports decoded surface
geometry only; it does not attach decoded texture images to the mesh.

The native decoder materializes its OBJ output in a temporary directory. The
viewer then parses frames on demand with its bounded cache and removes the
temporary files when the sequence closes. This path is read-only: Open4D still
uses `.v4d` for self-describing V-DMC output and does not write raw `.vmesh`.

## Flags

Shared: `--info` `--stride N` `--fps` `--up {x,y,z}` `--save out.gif`
`--width` `--height` `--point-size` `--wireframe` `--color` `--ambient`
`--background` `--distance` `--elevation` `--azimuth` `--no-metrics`.

| `visualize_sequence.py` | |
| --- | --- |
| `--pack-usd out.usdc` | Also write an OpenUSD container |

| `compare_sequences.py` | |
| --- | --- |
| `--metric {point,plane}` | Which distance to measure |
| `--csv out.csv` | Write the per-frame table |
| `--max-error` `--percentile` | Where the top of the colour scale sits |
| `--error-shading` | How far the light may darken the error colours, 0–1 |

`--up` is the one to reach for first; `visualize_sequence.py` warns when the
subject looks like it is lying across the view. Error colours are shaded only
slightly by default: the ramp is monotone in lightness, so brightness already
carries the magnitude and at full shading a dark patch is ambiguous between deep
shadow and large error. That is also why the comparison background is dark.

Single-sequence playback decodes on demand with a bounded cache. Comparison
still measures both complete sequences up front, so reach for `--stride N`
above a few hundred frames there.

## In your own code

```python
import open4d

with open4d.load("capture.usdc") as sequence:
    print(len(sequence), sequence.duration, sequence.fps)
    open4d.visualize(sequence)

open4d.visualize("capture.o4d")

# Raw V-DMC; decoder_config is optional and environment variables work too.
with open4d.load(
    "capture.vmesh",
    fps=30,
    options={
        "decoder": "/path/to/vmesh-decoder",
        "decoder_config": "/path/to/decoder.cfg",
    },
) as sequence:
    print(len(sequence))
```

Loading and playback are lazy — frames decode on access. Frame directories and
single mesh files remain supported as ingestion paths. Measuring needs no viewer.

## The OpenUSD container

`--pack-usd out.usdc` writes the whole sequence into one compressed USD file:
geometry as time samples, plus frame rate, up axis, frame count, and the key-frame
index, with per-frame `open4d:*` streams alongside. Connectivity is stored once
when it never changes and time-sampled where it does — those frames are the key
frames.

## Files

| File | |
| --- | --- |
| `visualize_sequence.py` | The single-sequence program |
| `compare_sequences.py` | The comparison program |
| `frame_sources.py` | Thin CLI inspection helpers over `open4d.io` |
| `open4d.io._mesh` | `.obj` and `.ply`, trimesh fallback |
| `formats_usd.py` | Compatibility wrappers over the public USD backend |
| `open4d.visualization` | Public viewer, renderer-neutral frames, rotation, shading |
| `mesh_metrics.py` | Nearest-neighbour search, point-to-point/plane, RMS and PSNR |
| `compare_frames.py` | Frame pairing, per-frame error, error colours |
| `colormaps.py` | The sequential ramp and the colourbar gradient |
| `viewer_compare_qt.py` | The two-pane synchronized viewer |
| `_common.py` | Path setup and dependency checks |
| `tests/` | `pytest examples/visualization/tests` |
