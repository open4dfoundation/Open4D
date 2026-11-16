"""ARAP volume tracking on the GPU.

Places a fixed number of centers inside the volume of the first mesh and
follows them through the sequence, keeping them evenly spread (Lloyd) while
moving neighbouring centers as rigidly as possible (ARAP). This is the TSMC
tracker with the whole loop on the GPU.

    import arapcuda

    tracker = arapcuda.Tracker(centers=2000)
    for vertices, triangles in meshes:         # (V, 3) floats, (T, 3) ints
        frame = tracker.add_frame(vertices, triangles)
        frame["centers"]     # (2000, 3) float32, same order in every frame
        frame["transforms"]  # (2000, 3, 4) float32 [R | t] from the previous frame; None for frame 0

    centers, transforms = arapcuda.track(meshes)   # whole sequence at once

Meshes should be closed (watertight): the volume is found by ray parity.
"""
import numpy as _np

from . import _core
from ._core import device_count

__all__ = ["Tracker", "track", "device_count", "read_obj", "write_xyz", "write_transforms"]


class Tracker:
    """Tracks one sequence, a frame at a time.

    Options (defaults match the TSMC encoder in Open4D):
      centers=2000             number of tracked centers
      grid_resolution=512      voxels along the longest side of each mesh's bounding box
      gradient_threshold=1e-4  a frame is done once no center moves farther than this per iteration
      smooth_sigma=0.125       distance at which spatial affinity falls to 1/2
      smooth_sigma2=0.125      transform difference at which affinity falls to 1/2
      arap_cutoff=0.001        neighbours with a lower affinity are ignored
      apply_lloyd=1, apply_smooth=1   weights of the even-spread and rigidity terms
      presmooth_iterations=10, max_iterations=1000, init_iterations=100
      seed=0                   seed of the initial random placement
      device=0                 CUDA device index
    """

    def __init__(self, **options):
        params = _core.Params()
        for name, value in options.items():
            if name.startswith("_") or not hasattr(params, name):
                raise TypeError(f"unknown option {name!r}")
            setattr(params, name, value)
        self._tracker = _core.Tracker(params)

    def add_frame(self, vertices, triangles):
        """Tracks the next frame. Returns a dict with centers, transforms, iterations, seconds."""
        return self._tracker.add_frame(vertices, triangles)

    @property
    def frame_count(self):
        return self._tracker.frame_count


def track(meshes, **options):
    """Tracks a sequence of (vertices, triangles) meshes.

    Returns centers (F, N, 3) and transforms (F, N, 3, 4); frame 0's transforms are the identity.
    """
    tracker = Tracker(**options)
    centers, transforms = [], []
    for vertices, triangles in meshes:
        frame = tracker.add_frame(vertices, triangles)
        centers.append(frame["centers"])
        if frame["transforms"] is None:
            identity = _np.zeros((len(frame["centers"]), 3, 4), _np.float32)
            identity[:, :, :3] = _np.eye(3, dtype=_np.float32)
            transforms.append(identity)
        else:
            transforms.append(frame["transforms"])
    return _np.stack(centers), _np.stack(transforms)


def read_obj(path):
    """Vertices (V, 3) and triangles (T, 3) of an OBJ file; polygons are split into fans."""
    vertices, triangles = [], []
    with open(path) as f:
        for line in f:
            if line.startswith("v "):
                vertices.append(line.split()[1:4])
            elif line.startswith("f "):
                idx = [int(p.split("/")[0]) for p in line.split()[1:]]
                idx = [i - 1 if i > 0 else len(vertices) + i for i in idx]
                triangles.extend([idx[0], idx[k], idx[k + 1]] for k in range(1, len(idx) - 1))
    return _np.asarray(vertices, _np.float32), _np.asarray(triangles, _np.int32).reshape(-1, 3)


def write_xyz(path, centers):
    """One "x y z" line per center (the TSMC tracker's .xyz output)."""
    _np.savetxt(path, _np.asarray(centers), fmt="%.9g")


def write_transforms(path, transforms):
    """One line per center: R row major, then t (the TSMC tracker's transform_*.txt)."""
    t = _np.asarray(transforms)
    _np.savetxt(path, _np.concatenate([t[:, :, :3].reshape(-1, 9), t[:, :, 3]], axis=1), fmt="%.9g")
