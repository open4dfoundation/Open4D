"""Counters, including under the threading the server actually uses."""

from __future__ import annotations

import threading

import pytest

from streamer.monitor import Monitor, Transfer, clip_of

pytestmark = pytest.mark.cpu


def test_totals_accumulate():
    monitor = Monitor()
    monitor.record("/basketball/frame_0000.ply", 200, 1000, 0.1)
    monitor.record("/basketball/frame_0001.ply", 200, 2000, 0.2)
    snapshot = monitor.snapshot()
    assert snapshot["requests"] == 2
    assert snapshot["bytes"] == 3000
    assert snapshot["errors"] == 0


def test_errors_are_counted_separately_but_still_counted():
    monitor = Monitor()
    monitor.record("/missing.ply", 404, 0, 0.01)
    snapshot = monitor.snapshot()
    assert snapshot["requests"] == 1
    assert snapshot["errors"] == 1


def test_bytes_roll_up_per_clip_heaviest_first():
    monitor = Monitor()
    monitor.record("/light/frame_0000.jpg", 200, 10, 0.01)
    monitor.record("/heavy/frame_0000.ply", 200, 5000, 0.5)
    monitor.record("/heavy/frame_0001.ply", 200, 5000, 0.5)
    by_clip = monitor.snapshot()["by_clip"]
    assert list(by_clip) == ["heavy", "light"]
    assert by_clip["heavy"] == {"requests": 2, "bytes": 10000}


@pytest.mark.parametrize(
    ("path", "expected"),
    [
        ("/basketball/frame_0000.ply", "basketball"),
        ("basketball/frame_0000.ply", "basketball"),
        ("/view.json", ""),
        ("/", ""),
        ("/clip/frame.ply?t=12", "clip"),
    ],
)
def test_clip_of_reads_the_first_path_segment(path, expected):
    assert clip_of(path) == expected


def test_recent_tail_is_bounded():
    monitor = Monitor(tail=3)
    for index in range(10):
        monitor.record(f"/c/frame_{index}.ply", 200, index, 0.01)
    recent = monitor.snapshot()["recent"]
    assert len(recent) == 3
    assert [item["bytes"] for item in recent] == [7, 8, 9]


def test_tail_can_be_switched_off():
    monitor = Monitor(tail=0)
    monitor.record("/c/f.ply", 200, 1, 0.01)
    assert monitor.snapshot()["recent"] == []
    assert monitor.snapshot()["requests"] == 1


def test_negative_tail_is_rejected():
    with pytest.raises(ValueError, match="nonnegative"):
        Monitor(tail=-1)


def test_throughput_is_none_before_anything_is_timed():
    assert Transfer("/a", 200, 100, 0.0).bytes_per_second is None
    assert Transfer("/a", 200, 100, 0.5).bytes_per_second == 200.0


def test_snapshot_is_json_ready():
    import json

    monitor = Monitor()
    monitor.record("/c/f.ply", 200, 5, 0.02)
    json.dumps(monitor.snapshot())


def test_reset_zeroes_everything():
    monitor = Monitor()
    monitor.record("/c/f.ply", 200, 5, 0.02)
    monitor.reset()
    snapshot = monitor.snapshot()
    assert snapshot["requests"] == 0
    assert snapshot["bytes"] == 0
    assert snapshot["by_clip"] == {}
    assert snapshot["recent"] == []


def test_concurrent_recording_loses_nothing():
    """The server is threaded, so this is the condition that matters."""
    monitor = Monitor()
    threads = [
        threading.Thread(
            target=lambda: [
                monitor.record("/c/f.ply", 200, 1, 0.001) for _ in range(200)
            ]
        )
        for _ in range(8)
    ]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    snapshot = monitor.snapshot()
    assert snapshot["requests"] == 1600
    assert snapshot["bytes"] == 1600
