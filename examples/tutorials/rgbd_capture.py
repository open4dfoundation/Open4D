"""Load a calibrated RGB-D capture and reconstruct meshes into .o4d.

Run ``python examples/tutorials/rgbd_capture.py --capture PAIRS --calibration RIG
--output reconstructed.o4d`` with a real saved two-camera capture. No device is
opened and no data is downloaded. ``PAIRS`` contains pair_NNNNNNNNNNNN folders;
``RIG`` contains factory calibration and the measured depth-to-depth transform.
The capture loader rectifies depth and aligns color using that calibration.
TSDF requires optional OpenCV, zstandard and Open3D 0.19 dependencies via
``open4d[capture]``. ``--frames`` limits loaded pairs; ``--usd`` additionally
exports the reconstructed meshes with optional ``open4d[usd]``.
"""

import argparse
import json
from pathlib import Path

import open4d as o4d


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", type=Path, required=True)
    parser.add_argument("--calibration", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--frames", type=int, nargs="+", help="strictly increasing saved pair numbers")
    parser.add_argument("--usd", type=Path, help="optional additional .usdc output")
    args = parser.parse_args()
    if args.output.suffix.lower() != ".o4d":
        parser.error("--output must end in .o4d")
    if args.output.exists() or (args.usd is not None and args.usd.exists()):
        parser.error("choose unused output paths")
    capture = o4d.RGBDCapture.from_folder(args.capture, args.calibration, frames=args.frames)
    output = o4d.reconstruct(capture, args.output, method="tsdf", codec="npz")
    with o4d.load(output) as meshes:
        if args.usd is not None:
            o4d.save(meshes, args.usd)
        print(json.dumps({"source": str(args.capture), "frames": len(meshes),
                          "timestamps": meshes.timestamps, "path": str(output)}))


if __name__ == "__main__":
    main()
