#!/usr/bin/env python3
"""Build a bundle, score its rungs, serve it, and play it over a bad link.

The whole of `streamer` in one pass, on the ten basketball frames the TVMC
codec vendors. Runnable from anywhere:

    python examples/streaming_demo.py

Needs three things beyond the base install: the optional `open4d-streamer`
package (``pip install -e open4d/streamer``), `open4d[draco]` for the Draco
rungs, and SciPy for the nearest-neighbour search the quality column uses.

Output goes to ``examples/out/``, which the repository ignores.
"""

from __future__ import annotations

import json
import math
import sys
import urllib.request
from pathlib import Path

import numpy as np
import open4d

try:
    import streamer
    from streamer import link, playback, policy
except ImportError:  # pragma: no cover - depends on the environment
    sys.exit("this example needs: pip install -e open4d/streamer")

from scipy.spatial import cKDTree

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "open4d/codecs/tvmc/arap-volume-tracking/data/basketball_player"
OUT = ROOT / "examples/out/streaming-demo"
FPS = 10
RUNGS = ["ply", "draco", "draco@11"]


def build() -> dict:
    """Write the sequence as one clip at three qualities."""
    with open4d.load(SOURCE, fps=FPS) as sequence:
        with streamer.Bundle(OUT, title="Basketball", source=SOURCE, fps=FPS) as clips:
            clips.add(sequence, name="player", scene="basketball", rungs=RUNGS)
    return json.loads((OUT / "view.json").read_text())


def reference_frames() -> list[np.ndarray]:
    with open4d.load(SOURCE, fps=FPS) as sequence:
        return [np.asarray(sequence[i].geometry.positions, dtype=np.float64)
                for i in range(len(sequence))]


def rung_frames(frames: list[str]) -> list[np.ndarray]:
    """Vertex positions per frame, whichever format the rung is in."""
    if frames[0].endswith(".drc"):
        import DracoPy
        return [np.asarray(DracoPy.decode((OUT / f).read_bytes()).points,
                           dtype=np.float64) for f in frames]
    # No `fps=`: write_sequence left a manifest here, and open4d refuses an
    # override of timing a source already declares.
    with open4d.load(OUT / Path(frames[0]).parent) as sequence:
        return [np.asarray(sequence[i].geometry.positions, dtype=np.float64)
                for i in range(len(sequence))]


def quality_db(reference: list[np.ndarray], decoded: list[np.ndarray]) -> float:
    """RMS vertex deviation as dB below the model's diagonal.

    `streamer.metrics` scores *pixels* against a captured reference, and says
    so for a mesh clip. Positions are what this bundle has, so this scores
    those. Nearest-neighbour rather than index-wise: Draco merges duplicate
    vertices, so the two sets are not the same length.
    """
    span = float(np.linalg.norm(np.ptp(np.vstack(reference), axis=0)))
    squared, count = 0.0, 0
    for ref, got in zip(reference, decoded):
        distance, _ = cKDTree(ref).query(got, k=1)
        squared += float(np.sum(distance ** 2))
        count += len(got)
    rms = math.sqrt(squared / count)
    return 20 * math.log10(span / rms) if rms else float("inf")


def ladder_of(clip: dict) -> list[policy.Rung]:
    """Every rung, with its measured bitrate and its measured quality."""
    reference = reference_frames()
    seconds = len(clip["frames"]) / FPS
    renditions = [(clip["detail"]["rung"], clip["frames"])]
    renditions += [(v["name"], v["frames"]) for v in clip["variants"]]

    rungs = []
    for name, frames in renditions:
        size = sum((OUT / f).stat().st_size for f in frames)
        score = quality_db(reference, rung_frames(frames))
        rungs.append(policy.Rung("player", name, size * 8 / seconds,
                                 {"psnr": min(score, 99.0)}))
    return sorted(rungs, key=lambda rung: rung.bits_per_second)


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

    ladder = ladder_of(clip)
    print(f"{'rung':<10}{'Mbit/s':>9}{'quality dB':>12}")
    for rung in ladder:
        print(f"{rung.variant:<10}{rung.bits_per_second / 1e6:9.2f}"
              f"{rung.quality['psnr']:12.1f}")

    counters = serve_and_count(clip)
    print(f"\nserved   {counters['requests']} requests, "
          f"{counters['bytes'] / 1e6:.2f} MB over HTTP")

    print("\nwhat a budget buys:")
    for budget in (2e6, 5e6, 20e6, 100e6):
        chosen = policy.choose([ladder], budget=budget)
        picked = (chosen.choices[0].variant if chosen.choices
                  else f"nothing — dropped {chosen.dropped[0]}")
        print(f"  {budget / 1e6:6.0f} Mbit/s -> {picked}")

    print("\n30 s over a link that collapses 30 -> 2.5 Mbit/s at t=15:")
    clock = [0.0]
    constrained = link.Link(
        capacity=30e6, latency=0.03, clock=lambda: clock[0],
        trace=link.Trace(at=(0.0, 15.0), capacity=(30e6, 2.5e6), loop=False))
    report = playback.Playback(
        [ladder], constrained, fps=FPS, clock=lambda: clock[0]).run(30.0).as_dict()
    for key in ("stalled_seconds", "frozen_seconds", "switches", "mean_quality"):
        print(f"  {key:<17}{report[key]}")
    print(f"  {'queueing':<17}{report['link']['queueing_fraction']:.1%} of the delay")


if __name__ == "__main__":
    main()
