"""Compare a decoded frame with its baseline in a shared-camera viewer.

Run ``python examples/tutorials/compare_visualization.py --baseline mesh.o4d
--decoded mesh.o4d`` for real caller-provided inputs with ``open4d[player]``.
``--texture atlas.png`` explicitly loads a user-selected RGB image: OBJ UVs are
preserved but arbitrary material paths are never followed. ``--seconds 3``
automatically closes the window for a bounded runtime check. For numerical
mesh error, install ``open4d[metrics]`` and pass ``--metrics``. GaussianCloud
frames are drawn as covariance splats with depth-sorted alpha blending.
"""

import argparse
import json
from pathlib import Path
import time

import numpy as np
import open4d as o4d


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--decoded", type=Path, required=True)
    parser.add_argument("--frame", type=int, default=0)
    parser.add_argument("--texture", type=Path)
    parser.add_argument("--seconds", type=float)
    parser.add_argument("--metrics", action="store_true")
    args = parser.parse_args()
    if args.seconds is not None and not 0 < args.seconds <= 3600:
        parser.error("--seconds must be greater than zero and at most 3600")
    texture = None
    if args.texture is not None:
        from PIL import Image
        with Image.open(args.texture) as image:
            texture = np.asarray(image.convert("RGB"))
    with o4d.load(args.baseline) as reference, o4d.load(args.decoded) as decoded:
        if args.metrics:
            comparison = o4d.compare_sequences(reference, decoded)
            print(json.dumps({"symmetric_rms": comparison.symmetric_rms,
                              "hausdorff": comparison.hausdorff}), flush=True)
        with o4d.Visualizer(title="Baseline and decoded", width=960, height=540) as viewer:
            viewer.add_geometry(reference.frame(args.frame), baseline=True, texture=texture)
            viewer.add_geometry(decoded.frame(args.frame), texture=texture)
            stop = None if args.seconds is None else time.monotonic() + args.seconds
            while viewer.poll_events() and (stop is None or time.monotonic() < stop):
                viewer.render()
                time.sleep(0.01)


if __name__ == "__main__":
    main()
