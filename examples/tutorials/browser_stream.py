"""Export a mesh bundle and play it through the optional browser streamer.

Run ``python examples/tutorials/browser_stream.py --output /tmp/o4d-browser``
after installing the separate ``open4d-streamer`` package. PLY delivery avoids
a native Draco dependency. The synthetic MIT wave is exported once, then served
on loopback; printed HTTP rates measure delivery, not live codec processing.
``--seconds 3`` provides a bounded HTTP check and stops the server automatically.
``--source mesh.o4d`` uses an explicit real input instead; ``--rung`` selects
PLY, Draco, a codec/format ladder, or supported Gaussian delivery rungs.
"""

import argparse
from contextlib import nullcontext
import json
from pathlib import Path
import time
import urllib.request

import open4d as o4d
from open4d.demo import mesh_sequence


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="new bundle directory")
    parser.add_argument("--source", type=Path)
    parser.add_argument("--name", default="example")
    parser.add_argument("--rung", action="append", help="repeat to define a quality ladder; default ply")
    parser.add_argument("--seconds", type=float, help="bounded check without opening a browser")
    args = parser.parse_args()
    if args.output.exists():
        parser.error("choose a new --output directory")
    if args.seconds is not None and not 0 < args.seconds <= 3600:
        parser.error("--seconds must be greater than zero and at most 3600")
    context = nullcontext(args.source) if args.source is not None else mesh_sequence(side=8, frames=4)
    with context as source:
        server = o4d.stream(source, out_dir=args.output, name=args.name, rungs=args.rung or ["ply"],
                            host="127.0.0.1", port=0, open_browser=args.seconds is None, block=False)
        try:
            url = f"http://127.0.0.1:{server.server_address[1]}/"
            with urllib.request.urlopen(url + "view.json", timeout=5) as response:
                manifest = json.load(response)
            first_frame = manifest["clips"][0]["frames"][0]
            with urllib.request.urlopen(url + first_frame, timeout=5) as response:
                delivered = response.read()
            assert delivered, "the first frame was not delivered"
            print(json.dumps({"synthetic": args.source is None, "url": url,
                              "clips": len(manifest["clips"]), "first_frame_bytes": len(delivered),
                              "bundle": str(args.output)}), flush=True)
            if args.seconds is None:
                print("Press Ctrl+C to stop this loopback server.", flush=True)
                while True:
                    time.sleep(0.5)
            else:
                time.sleep(args.seconds)
        except KeyboardInterrupt:
            pass
        finally:
            server.shutdown()
            server.server_close()


if __name__ == "__main__":
    main()
