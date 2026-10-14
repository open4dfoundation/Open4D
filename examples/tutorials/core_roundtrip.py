"""Round-trip mesh, point-cloud and Gaussian sequences with the NumPy core.

Run ``python examples/tutorials/core_roundtrip.py --output /tmp/o4d-core``.
Choose a new output directory. No dataset, model, native codec or GPU is needed.
The mesh wave, point samples and Gaussian ring are explicitly synthetic MIT
examples. ``--usd`` also exports the mesh through optional ``open4d[usd]``.
The .o4d files retain decoded geometry, metadata and timestamps; they are not
trained neural-codec outputs.
"""

import argparse
import json
from pathlib import Path

import numpy as np
import open4d as o4d


def roundtrip(sequence, path):
    """Encode a caller-owned sequence and verify the independently opened data."""
    o4d.encode(sequence, path, codec="npz")
    with o4d.decode(path) as restored:
        assert restored.timestamps == sequence.timestamps
        assert len(restored) == len(sequence)
        for source, decoded in zip(sequence, restored):
            assert source.geometry.representation == decoded.geometry.representation
            for name in ("positions", "triangles", "colors", "scales", "rotations", "opacities"):
                before = getattr(source.geometry, name, None)
                after = getattr(decoded.geometry, name, None)
                if before is not None:
                    np.testing.assert_array_equal(before, after)
        return {"representation": restored.frame(0).geometry.representation.value,
                "frames": len(restored), "path": str(path), "bytes": path.stat().st_size}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="new output directory")
    parser.add_argument("--usd", action="store_true", help="also export mesh.usdc; needs usd-core")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    rows = []
    with o4d.data.SyntheticMeshSequence(cache_dir=args.output / "cache").load() as meshes:
        rows.append(roundtrip(meshes, args.output / "mesh.o4d"))
        if args.usd:
            o4d.save(meshes, args.output / "mesh.usdc")
        point_frames = [o4d.Frame(frame.frame_index, frame.timestamp,
                       o4d.PointCloud(positions=frame.geometry.positions.copy())) for frame in meshes]
    with o4d.Sequence(o4d.MemoryFrameProvider(point_frames,
                      metadata={"synthetic": True, "license": "MIT", "source": "wave-grid samples"})) as points:
        rows.append(roundtrip(points, args.output / "points.o4d"))
    with o4d.data.SyntheticGaussianSequence(cache_dir=args.output / "cache").load() as gaussians:
        rows.append(roundtrip(gaussians, args.output / "gaussians.o4d"))
    print(json.dumps({"synthetic": True, "codecs": [info.id for info in o4d.available_codecs()],
                      "artifacts": rows}, indent=2))


if __name__ == "__main__":
    main()
