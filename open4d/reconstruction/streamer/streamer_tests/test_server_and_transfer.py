"""Sending and receiving a bundle, against a real server on a real socket."""

from __future__ import annotations

import json
import time
import urllib.request

import pytest

from streamer import bundle, transfer
from streamer.monitor import Monitor
from streamer.server import serve

pytestmark = pytest.mark.cpu


def make_bundle(root, *, frames: int = 3):
    """A two-clip bundle: one geometry clip, one pixel clip."""
    written = {"gaussians": [], "pixels": []}
    for index in range(frames):
        ply = root / "subject-vega" / f"frame_{index:04d}.ply"
        ply.parent.mkdir(parents=True, exist_ok=True)
        ply.write_bytes(b"ply\n" + bytes(64))
        written["gaussians"].append(str(ply.relative_to(root)))

        jpg = root / "subject-captured" / f"frame_{index:04d}.jpg"
        jpg.parent.mkdir(parents=True, exist_ok=True)
        jpg.write_bytes(b"\xff\xd8\xff" + bytes(32))
        written["pixels"].append(str(jpg.relative_to(root)))

    bundle.write(
        root,
        title="test bundle",
        source="synthetic",
        clips=[
            bundle.Clip(
                name="subject-vega",
                representation="gaussians",
                scene="subject",
                method="vega",
                frames=written["gaussians"],
                counts=[8] * frames,
            ),
            bundle.Clip(
                name="subject-captured",
                representation="pixels",
                scene="subject",
                method="captured",
                camera=0,
                frames=written["pixels"],
            ),
        ],
    )
    return root


@pytest.fixture
def served(tmp_path):
    root = make_bundle(tmp_path / "bundle")
    server = serve(root, port=0, block=False)
    base = f"http://127.0.0.1:{server.server_address[1]}"
    try:
        yield root, base, server
    finally:
        server.shutdown()
        server.server_close()


def get(url: str) -> tuple[int, bytes, str]:
    with urllib.request.urlopen(url, timeout=10) as response:
        return response.status, response.read(), response.headers.get("Content-Type", "")


def eventually(predicate, timeout: float = 5.0):
    """Wait for a counter to land.

    The server records a response once it has completed, so a client that has
    read the body can briefly be ahead of the counters -- see `serve`. Polling
    here rather than sleeping keeps the test fast and non-flaky.
    """
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.01)
    raise AssertionError(f"condition never became true within {timeout}s")


# -------------------------------------------------------------- sending ---


def test_root_serves_the_client_page(served):
    _, base, _ = served
    status, body, content_type = get(base + "/")
    assert status == 200
    assert content_type.startswith("text/html")
    assert b"REPRESENTATIONS" in body


def test_manifest_is_served_as_json(served):
    _, base, _ = served
    status, body, content_type = get(base + "/view.json")
    assert status == 200
    assert content_type == "application/json"
    assert json.loads(body)["version"] == bundle.VERSION


def test_frames_get_the_content_type_the_registry_declares(served):
    """A .ply served as text/html fails as a parse error, which is the bug this prevents."""
    _, base, _ = served
    _, _, ply_type = get(base + "/subject-vega/frame_0000.ply")
    _, _, jpg_type = get(base + "/subject-captured/frame_0000.jpg")
    assert ply_type == "application/octet-stream"
    assert jpg_type == "image/jpeg"


def test_serving_a_directory_that_is_not_a_bundle_fails_loudly(tmp_path):
    with pytest.raises(FileNotFoundError, match="view.json"):
        serve(tmp_path, port=0, block=False)


# ------------------------------------------------------------ monitoring ---


def test_the_server_counts_what_it_sent(served):
    _, base, server = served
    get(base + "/subject-vega/frame_0000.ply")
    get(base + "/subject-vega/frame_0001.ply")
    rollup = eventually(
        lambda: server.monitor.snapshot()["by_clip"].get("subject-vega")
        if server.monitor.snapshot()["by_clip"].get("subject-vega", {}).get("requests")
        == 2
        else None
    )
    assert rollup == {"requests": 2, "bytes": 2 * 68}


def test_counters_are_exposed_over_http(served):
    _, base, _ = served
    get(base + "/subject-captured/frame_0000.jpg")
    body = eventually(
        lambda: (lambda payload: payload if "subject-captured" in json.loads(payload)["by_clip"] else None)(
            get(base + "/stats.json")[1]
        )
    )
    assert json.loads(body)["by_clip"]["subject-captured"]["requests"] == 1


def test_a_missing_frame_is_recorded_as_an_error(served):
    _, base, server = served
    with pytest.raises(urllib.error.HTTPError):
        get(base + "/subject-vega/frame_9999.ply")
    eventually(lambda: server.monitor.snapshot()["errors"] >= 1)


def test_two_servers_do_not_count_into_each_other(tmp_path):
    """The handler's monitor is a class attribute, so this is a real hazard."""
    first = serve(make_bundle(tmp_path / "a"), port=0, block=False)
    second = serve(make_bundle(tmp_path / "b"), port=0, block=False)
    try:
        get(f"http://127.0.0.1:{first.server_address[1]}/view.json")
        eventually(lambda: first.monitor.snapshot()["requests"] == 1)
        assert second.monitor.snapshot()["requests"] == 0
    finally:
        for server in (first, second):
            server.shutdown()
            server.server_close()


def test_a_caller_can_supply_its_own_monitor(tmp_path):
    shared = Monitor()
    server = serve(make_bundle(tmp_path / "c"), port=0, block=False, monitor=shared)
    try:
        get(f"http://127.0.0.1:{server.server_address[1]}/view.json")
        eventually(lambda: shared.snapshot()["requests"] == 1)
    finally:
        server.shutdown()
        server.server_close()


# ------------------------------------------------------------- receiving ---


def test_fetch_copies_the_whole_bundle(served, tmp_path):
    root, base, _ = served
    destination = tmp_path / "copy"
    result = transfer.fetch(base, destination)
    assert len(result.fetched) == 6
    assert result.skipped == ()
    assert result.bytes > 0
    original = json.loads((root / "view.json").read_text())
    copied = json.loads((destination / "view.json").read_text())
    assert [clip["name"] for clip in copied["clips"]] == [
        clip["name"] for clip in original["clips"]
    ]
    for clip in copied["clips"]:
        for path in clip["frames"]:
            assert (destination / path).read_bytes() == (root / path).read_bytes()


def test_fetch_is_resumable_by_running_it_again(served, tmp_path):
    _, base, _ = served
    destination = tmp_path / "copy"
    transfer.fetch(base, destination)
    again = transfer.fetch(base, destination)
    assert again.fetched == ()
    assert len(again.skipped) == 6
    assert again.bytes == 0


def test_a_truncated_frame_is_refetched(served, tmp_path):
    _, base, _ = served
    destination = tmp_path / "copy"
    transfer.fetch(base, destination)
    victim = destination / "subject-vega" / "frame_0000.ply"
    victim.write_bytes(b"short")
    again = transfer.fetch(base, destination)
    assert "subject-vega/frame_0000.ply" in again.fetched
    assert victim.stat().st_size == 68


def test_fetch_can_take_one_clip_of_many(served, tmp_path):
    _, base, _ = served
    destination = tmp_path / "copy"
    result = transfer.fetch(base, destination, only=["subject-vega"])
    assert len(result.fetched) == 3
    assert not (destination / "subject-captured").exists()
    # The written manifest is the filtered one, so the copy is a valid bundle
    # rather than one promising clips it does not have.
    copied = json.loads((destination / "view.json").read_text())
    assert [clip["name"] for clip in copied["clips"]] == ["subject-vega"]


def test_fetch_rejects_a_clip_the_bundle_does_not_have(served, tmp_path):
    _, base, _ = served
    with pytest.raises(KeyError, match="nonesuch"):
        transfer.fetch(base, tmp_path / "copy", only=["nonesuch"])


def test_fetch_reports_progress_and_can_be_measured(served, tmp_path):
    _, base, _ = served
    seen: list[tuple[str, int, int]] = []
    counters = Monitor()
    transfer.fetch(
        base,
        tmp_path / "copy",
        monitor=counters,
        progress=lambda path, done, total: seen.append((path, done, total)),
    )
    assert [done for _, done, _ in seen] == [1, 2, 3, 4, 5, 6]
    assert all(total == 6 for _, _, total in seen)
    assert counters.snapshot()["requests"] == 6


def test_a_fetched_bundle_can_be_served_again(served, tmp_path):
    """The round trip: what comes back is a bundle, not just some files."""
    _, base, _ = served
    destination = tmp_path / "copy"
    transfer.fetch(base, destination)
    server = serve(destination, port=0, block=False)
    try:
        status, body, _ = get(f"http://127.0.0.1:{server.server_address[1]}/view.json")
        assert status == 200
        assert len(json.loads(body)["clips"]) == 2
    finally:
        server.shutdown()
        server.server_close()
