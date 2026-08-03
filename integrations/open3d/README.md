# Open4D to Open3D adapter

This optional integration decodes Open4D v1 `.o4d` mesh, raw point-cloud, and
Draco point-cloud frames with Open4D's existing readers, then converts them to
standard Open3D `TriangleMesh` or `PointCloud` objects. It is an adapter, not
native `.o4d` support in Open3D core.

## Installation

From the Open4D repository root:

```bash
python -m pip install -e .
python -m pip install -e ".[open3d]"
```

Alternatively, install Open3D from `integrations/open3d/requirements.txt`.
Draco-compressed point clouds additionally require `python -m pip install -e
".[draco]"`. Open3D remains optional for the base Open4D installation.

## Python API

```python
from integrations.open3d import iter_frames, load_frame, sequence_info

print(sequence_info("sample.o4d"))
geometry = load_frame("sample.o4d", frame_index=20)

for geometry in iter_frames("sample.o4d", start=10, stop=20):
    process(geometry)
```

`load_frame` addresses the stored Open4D frame ID. `iter_frames` uses Python
slice semantics over the file's ordered frame list and decodes one frame at a
time.

## Viewer and PLY export

```bash
python integrations/open3d/example_viewer.py sample.o4d
python integrations/open3d/example_viewer.py sample.o4d --frame 20
python integrations/open3d/example_viewer.py sample.o4d --animate --fps 30
python integrations/open3d/example_viewer.py sample.o4d --frame 20 --export-ply frame20.ply
```

Animation reuses one Open3D visualization window and caps display rate; it
does not assume decoding can sustain the requested FPS.

## Attributes and limitations

- Mesh files provide vertices and triangle indices.
- Raw and Draco point-cloud files provide points and may provide per-point RGB
  colors. Integer RGB values are normalized from `[0, 255]` to Open3D's
  `[0, 1]` representation.
- The current Open4D v1 readers do not decode mesh colors or normals. The
  low-level `frame_to_open3d` converter preserves per-vertex colors and normals
  when supplied by a compatible frame-like object or mapping.
- Only triangle connectivity is supported; malformed arrays, non-integer or
  out-of-range indices, and non-finite attributes are rejected.
- No sample dataset is committed. Unit tests build tiny temporary `.o4d` files;
  viewer testing requires a local Open4D sample and a graphical environment.
