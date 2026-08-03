# Open4D to Open3D adapter

Converts decoded Open4D geometry into standard Open3D `TriangleMesh` or
`PointCloud` objects. It is an adapter, not a loader — decoding is somebody
else's job, and this turns the result into Open3D types.

## Installation

From the Open4D repository root:

```bash
python -m pip install -e .
python -m pip install -e ".[open3d]"
```

Open3D publishes no wheels for Python 3.13, so this integration needs 3.12 or
older. Open3D remains optional for the base Open4D installation.

## Python API

```python
from integrations.open3d import frame_to_open3d

geometry = frame_to_open3d(frame)
```

`frame` can be an `open4d.core.Frame`'s geometry, a mapping, a tuple, or any
object exposing `vertices`/`points`, `faces`/`triangles`,
`colors`/`vertex_colors`, and `normals`/`vertex_normals`. A frame with triangles
becomes a `TriangleMesh`; a frame without them becomes a `PointCloud`.

## Attributes and limitations

- Per-vertex colors and normals are preserved when the frame supplies them.
  Integer RGB values are normalized from `[0, 255]` to Open3D's `[0, 1]`.
- Only triangle connectivity is supported. Malformed arrays, non-integer or
  out-of-range indices, and non-finite attributes are rejected.
- Texture coordinates and materials are not carried across.
