# Open4D examples

`visualize_sequence.py` loads a 4D sequence, says what's in it, and plays it in
our own PyQt6 viewer.

```bash
python -m pip install -e '.[player]'

python examples/visualization/visualize_sequence.py my_capture/ --info    # check it loads
python examples/visualization/visualize_sequence.py my_capture/           # play it
python examples/visualization/visualize_sequence.py my_capture/ --save out.gif
```

Run it with no arguments to see every format it accepts.

In the window: drag to orbit, scroll to zoom, drag the frame slider to scrub,
space to pause, left/right to step, `q` to quit.

The viewer is built on PyQt6 and pyqtgraph — the same stack as `open4d.player`,
and no Open3D, which matters because Open3D publishes no wheels for Python 3.13.
The window and `--save` share one renderer, so a saved GIF looks like what you
saw. Lighting is a fixed directional light baked into vertex colours, so the
shading stays put as you orbit.

## Sources

A folder holding one mesh file per frame, or a single file holding the whole
sequence.

| Source | Needs |
| --- | --- |
| Folder of `.obj` or `.ply` frames | nothing |
| Folder of `.usd` frames | `.[usd]` |
| Folder of `.stl` `.off` `.glb` `.gltf` frames | `.[tools]` |
| One USD file (`.usd` `.usda` `.usdc` `.usdz`) | `.[usd]` |
| One mesh file | as above |

Frames are ordered by the last number in the filename, so `frame_2.obj` comes
before `frame_10.obj`. A frame with no faces is drawn as a point cloud.

## Flags

| Flag | What it does |
| --- | --- |
| `--info` | Report and stop, without decoding geometry |
| `--up {x,y,z}` | Which of your axes points up. Wrong guess renders the subject on its side |
| `--stride N` | Keep every Nth frame |
| `--fps` | Override the rate. A USD file uses its own stage rate; a folder gets 30 |
| `--save out.gif` | Render offscreen to an animated GIF |
| `--color` `--ambient` `--background` | Appearance |
| `--distance` `--elevation` `--azimuth` | Camera |
| `--width` `--height` `--point-size` `--wireframe` | View |
| `--pack-usd out.usdc` | Also write an OpenUSD container |

`--up` is the one to reach for first. The script warns when the subject looks
like it is lying across the view rather than standing up.

## In your own code

```python
import sys; sys.path.insert(0, "examples/visualization")
from frame_sources import open_sequence

with open_sequence("my_capture/") as sequence:   # fps= overrides
    print(len(sequence), sequence.duration, sequence.fps)

    mesh = sequence[0].geometry   # open4d.TriangleMesh
    mesh.positions                # (N, 3) float32
    mesh.triangles                # (M, 3) uint32
    mesh.colors                   # (N, 3) or None
```

Frames are decoded on access, so a long sequence never has to fit in memory.

To add a format, add one entry to `FRAME_READERS` or `SEQUENCE_OPENERS` in
`frame_sources.py`.

## The OpenUSD container

`--pack-usd out.usdc` writes the sequence into one compressed USD file: the
geometry as time samples, plus the frame rate, up axis, frame count, and
key-frame index. Per-frame streams (`open4d:frameIndex`, `open4d:timestamp`,
`open4d:keyFrame`, `open4d:vertexCount`, `open4d:triangleCount`) ride alongside.

Connectivity is stored once when it never changes, and time-sampled where it
does. The frames where it changes are the key frames.

## Test data

The TVMC codec ships 10 frames of a basketball player, useful for checking the
program runs:

```bash
python examples/visualization/visualize_sequence.py \
    open4d/codecs/tvmc/arap-volume-tracking/data/basketball_player \
    --up y --fps 10 --yaw -650 --save basketball.gif
```

## Files

| File | |
| --- | --- |
| `visualize_sequence.py` | The program |
| `frame_sources.py` | Format registry and `open_sequence()` |
| `formats_mesh.py` | `.obj` and `.ply`, trimesh fallback |
| `formats_usd.py` | USD container read and write |
| `render_frames.py` | Renderer-neutral frames, up-axis rotation, shading |
| `viewer_qt.py` | The PyQt6 + pyqtgraph viewer |
| `_common.py` | Path setup and dependency checks |
