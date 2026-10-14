"""Send and receive a complete decoded mesh sequence on loopback TCP.

Run ``python examples/tutorials/tcp_stream.py`` with the NumPy core.
The synthetic MIT wave uses an ephemeral local port, no remote endpoint, and
no GPU. Both sender and receiver are bounded; timestamps and geometry are
verified. ``--output recording.o4d`` optionally keeps the received sequence.
TCP carries decoded triangle-mesh arrays, not compressed codec bytes or native
Gaussian/field state. Browser streaming is a separate optional HTTP workflow.
"""

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path

import numpy as np
import open4d as o4d
from open4d.demo import mesh_sequence


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="unused .o4d destination for the recording")
    args = parser.parse_args()
    if args.output is not None and (args.output.suffix.lower() != ".o4d" or args.output.exists()):
        parser.error("--output must be an unused .o4d path")
    with mesh_sequence(side=8, frames=4) as source:
        with o4d.receive(port=0, timeout=5) as receiver, ThreadPoolExecutor() as pool:
            sender = pool.submit(o4d.send, source, *receiver.address, realtime=False, timeout=5)
            with receiver.record(max_frames=4) as recording:
                assert sender.result(timeout=5) == 4
                assert recording.timestamps == source.timestamps
                for before, after in zip(source, recording):
                    np.testing.assert_array_equal(before.geometry.positions, after.geometry.positions)
                if args.output is not None:
                    o4d.encode(recording, args.output, codec="npz")
                stats = receiver.stats
                print(json.dumps({"synthetic": True, "frames": len(recording),
                                  "payload_bytes": stats.payload_bytes, "wire_bytes": stats.wire_bytes,
                                  "output": None if args.output is None else str(args.output)}))


if __name__ == "__main__":
    main()
