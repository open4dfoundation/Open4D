# IO

Readers and writers for static and time-varying geometry:
PLY, OBJ, GLTF, and Open4D-native formats.

Raw mesh `.o4d` files can also be opened through the shared lazy data model:

```python
from open4d import open_o4d_mesh_sequence

with open_o4d_mesh_sequence("sequence.o4d") as sequence:
    frame = sequence.frame(0)
    vertices = frame.geometry.positions
```

The existing `O4DMeshReader` tuple API remains available for compatibility.
