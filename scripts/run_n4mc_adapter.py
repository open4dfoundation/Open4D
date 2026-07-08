"""Smoke-run N4MC end-to-end through the open4d.modules adapter."""
import os
import sys
import numpy as np

sys.path.insert(0, "/home/ryan/Open4D")

import open4d as o4d
from open4d.core import MeshSequence


def prism_faces():
    return np.array(
        [[0, 1, 2], [3, 5, 4], [0, 3, 4], [0, 4, 1],
         [1, 4, 5], [1, 5, 2], [2, 5, 3], [2, 3, 0]], dtype=np.uint32)


def prism_vertices(offset):
    base = np.array(
        [[0, 0, 0], [1, 0, 0], [0.5, 1, 0],
         [0, 0, 1], [1, 0, 1], [0.5, 1, 1]], dtype=np.float64)
    return base + np.array([offset, 0, 0], dtype=np.float64)


# Build a 3-frame MeshSequence (a translating triangular prism).
seq = MeshSequence.from_frames(
    [prism_vertices(0.0), prism_vertices(0.2), prism_vertices(0.4)],
    prism_faces(),
    timestamps=[0.0, 0.5, 1.0],
    name="prism-demo",
)
print("input:", repr(seq))

codec = o4d.modules.get_codec("n4mc")
print("codec:", codec)
print(codec.available())

result = codec.compress(
    seq,
    workdir="/tmp/n4mc_adapter_test",
    n_epoch=100,          # need %100==0 to export rec meshes + latent codes
    voxel_grid_res=127,
    num_frames=3,
)

print("\n===== RESULT =====")
print(repr(result))
print("\n----- stage timings -----")
print(result.stage_table())
print("\n----- metrics -----")
for k, v in result.metrics.items():
    print(f"  {k}: {v}")
print("\n----- artifacts -----")
for k, v in sorted(result.artifacts.items()):
    sz = os.path.getsize(v) if os.path.exists(v) else -1
    print(f"  {k}: {v}  ({sz} bytes)")

# Load reconstruction back into a MeshSequence (round the loop closed).
recon = result.to_mesh_sequence()
print("\n----- reconstruction as MeshSequence -----")
print(repr(recon))
for f in recon:
    print("  ", repr(f))
print("\nDONE ok=%s" % result.ok)
