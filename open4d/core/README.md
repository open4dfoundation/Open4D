# Core Data Structures

The core package provides NumPy-backed `TriangleMesh`, temporal `Frame`, lazy
`Sequence`, and `FrameProvider` abstractions. See
[`docs/sequence-design.md`](../../docs/sequence-design.md) for architecture,
mutability, topology, and migration guidance.

## The dtype a codec should emit

`TriangleMesh` coerces what it is given, so a codec may hand it any floating
positions and any integer indices. What comes back out is always:

| Field | Stored as |
| --- | --- |
| `positions`, `normals`, `texture_coordinates` | `float32` |
| `triangles` | `uint32` |
| `colors` | `float32` in `[0, 1]` |
| attributes | `float32` / `int32` / `bool` by kind |

Emitting `float32` positions and `uint32` indices directly keeps construction
zero-copy. Colors may be `uint8` in `[0, 255]` or float in `[0, 1]`; both are
accepted and stored identically, so nothing downstream needs to ask which.

The names and rationale live in [`dtypes.py`](dtypes.py).
