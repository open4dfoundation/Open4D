#!/usr/bin/env python3
"""Build a bundle, score its rungs, serve it, and play it over a bad link.

The whole of `streamer` in one pass, on Open4D's generated wave sequence.
Runnable from anywhere:

    python examples/streaming_demo.py

Needs three things beyond the base install: the optional `open4d-streamer`
package (``pip install -e open4d/streamer``), `DracoPy` for the Draco
rungs, and SciPy for the nearest-neighbour search `open4d.compare_sequences`
scores each rung with.

Output goes to ``examples/out/``, which the repository ignores.
"""

from __future__ import annotations

import json
import sys
import urllib.request
from pathlib import Path

from open4d.demo import mesh_sequence

try:
    import streamer
    from streamer import link, playback, policy, score
except ImportError:  # pragma: no cover - depends on the environment
    sys.exit("this example needs: pip install -e open4d/streamer")

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "examples/out/streaming-demo"
FPS = 10
RUNGS = ["ply", "draco", "draco@11"]


def build() -> dict:
    """Write the sequence as one clip at three qualities, each one scored.

    ``score=True`` measures every rung against the sequence it was written
    from with `open4d.compare_sequences`, and records it where `policy` reads
    it. Positions are what this bundle has, so it is geometry that is scored;
    `streamer.metrics` is the pixel counterpart for a captured reference.
    """
    with mesh_sequence(side=96, frames=30, fps=FPS) as sequence:
        with streamer.Bundle(OUT, title="Wave", source="open4d.demo", fps=FPS) as clips:
            clips.add(sequence, name="wave", scene="wave", rungs=RUNGS, score=True)
    return json.loads((OUT / "view.json").read_text())


def serve_and_count(clip: dict) -> dict:
    """Start the server, pull every frame over HTTP, read the counters."""
    server = streamer.serve(OUT, port=0, block=False, open_browser=False)
    try:
        base = f"http://127.0.0.1:{server.server_address[1]}/"
        for frame in clip["frames"]:
            urllib.request.urlopen(base + frame).read()
        return server.monitor.snapshot()
    finally:
        server.shutdown()


def main() -> None:
    clip = build()["clips"][0]
    print(f"bundle   {OUT.relative_to(ROOT)}/ — {len(clip['frames'])} frames, "
          f"{clip['representation']}, {len(clip['variants']) + 1} rungs\n")

    (ladder,) = policy.measured_rungs(OUT)
    print(f"{'rung':<10}{'Mbit/s':>9}{'quality dB':>12}")
    for rung in ladder:
        print(f"{rung.variant or clip['detail']['rung']:<10}"
              f"{rung.bits_per_second / 1e6:9.2f}"
              f"{rung.quality[score.METRIC]:12.1f}")

    counters = serve_and_count(clip)
    print(f"\nserved   {counters['requests']} requests, "
          f"{counters['bytes'] / 1e6:.2f} MB over HTTP")

    print("\nwhat a budget buys:")
    for budget in (2e6, 5e6, 20e6, 100e6):
        chosen = policy.choose([ladder], budget=budget, metric=score.METRIC)
        picked = ((chosen.choices[0].variant or clip["detail"]["rung"])
                  if chosen.choices
                  else f"nothing — dropped {chosen.dropped[0]}")
        print(f"  {budget / 1e6:6.0f} Mbit/s -> {picked}")

    print("\n30 s over a link that collapses 30 -> 2.5 Mbit/s at t=15:")
    clock = [0.0]
    constrained = link.Link(
        capacity=30e6, latency=0.03, clock=lambda: clock[0],
        trace=link.Trace(at=(0.0, 15.0), capacity=(30e6, 2.5e6), loop=False))
    report = playback.Playback(
        [ladder], constrained, fps=FPS, metric=score.METRIC,
        clock=lambda: clock[0]).run(30.0).as_dict()
    for key in ("stalled_seconds", "frozen_seconds", "switches", "mean_quality"):
        print(f"  {key:<17}{report[key]}")
    print(f"  {'queueing':<17}{report['link']['queueing_fraction']:.1%} of the delay")


if __name__ == "__main__":
    main()
