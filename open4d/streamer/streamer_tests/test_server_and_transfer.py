"""Sending and receiving a bundle, against a real server on a real socket."""

from __future__ import annotations

import contextlib
import gzip
import json
import time
import urllib.request

import pytest

from streamer import bundle, transfer
from streamer.monitor import Monitor
from streamer.server import serve

pytestmark = pytest.mark.cpu


@pytest.mark.parametrize("path", ["../escaped", "/tmp/escaped", "C:/escaped", "dir/../../escaped", "dir\\escaped", "view.json", "frame.partial"])
def test_fetch_rejects_unsafe_paths_before_writing_manifest(tmp_path, monkeypatch, path):
    monkeypatch.setattr(transfer, "_get", lambda *args: json.dumps({"clips": [{"frames": [path]}]}).encode())
    with pytest.raises(ValueError, match="path"):
        transfer.fetch("https://example.invalid/", tmp_path / "copy")
    assert not (tmp_path / "copy" / "view.json").exists()


@pytest.mark.parametrize("partial", [False, True])
def test_fetch_rejects_symlink_targets(tmp_path, monkeypatch, partial):
    root = tmp_path / "copy"
    root.mkdir()
    outside = tmp_path / "outside"
    outside.write_bytes(b"keep")
    (root / ("frame.ply.partial" if partial else "frame.ply")).symlink_to(outside)
    monkeypatch.setattr(transfer, "_get", lambda *args: b'{"clips":[{"frames":["frame.ply"]}]}')
    with pytest.raises(ValueError, match="path"):
        transfer.fetch("https://example.invalid/", root)
    assert outside.read_bytes() == b"keep"


def test_fetch_paths_include_variants_and_packed_sequences():
    index = {"clips": [{"frames": ["logical.ply"], "sequence": {"url": "clip.seq"},
                        "variants": [{"frames": ["low.ply"]}, {"frames": ["low.ply"]}]}]}
    assert transfer.frame_paths(index) == ("clip.seq", "low.ply")


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


@contextlib.contextmanager
def serving(root):
    """A server on ``root``, for a test that builds its own bundle.

    The `served` fixture makes its own two-clip bundle; these need a big
    repetitive one, so they bring the directory and borrow the lifecycle.
    """
    server = serve(root, port=0, block=False)
    try:
        yield f"http://127.0.0.1:{server.server_address[1]}"
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


# ------------------------------------------------------------- the transport ---


def a_served_bundle(tmp_path, *, payload=b""):
    """A bundle with one frame of known content, and a running server."""
    from streamer import bundle
    from streamer.server import serve

    (tmp_path / "c").mkdir(parents=True, exist_ok=True)
    (tmp_path / "c" / "f.ply").write_bytes(payload or bytes(range(256)) * 40)
    bundle.write(tmp_path, title="t", source="s",
                 clips=[bundle.Clip(name="c", representation="mesh",
                                    frames=["c/f.ply"])])
    server = serve(tmp_path, port=0, block=False)
    return server, f"http://127.0.0.1:{server.server_address[1]}"


def test_the_server_speaks_http_1_1():
    """1.0 closes after every response: one handshake and one slow-start per
    frame, which caps the frame rate on any real link regardless of bandwidth.
    """
    from streamer.server import _Handler

    assert _Handler.protocol_version == "HTTP/1.1"


def test_one_connection_serves_several_requests(tmp_path):
    """The point of 1.1. Asserted at the socket, because a Connection header
    can say keep-alive while the server closes anyway."""
    import socket

    server, base = a_served_bundle(tmp_path)
    try:
        port = server.server_address[1]
        sock = socket.create_connection(("127.0.0.1", port), timeout=10)
        sock.settimeout(10)
        statuses = []
        for _ in range(3):
            sock.sendall(b"GET /view.json HTTP/1.1\r\nHost: x\r\n\r\n")
            head = b""
            while b"\r\n\r\n" not in head:
                head += sock.recv(1)
            statuses.append(head.split(b"\r\n", 1)[0])
            length = int(next(
                line.split(b":")[1] for line in head.split(b"\r\n")
                if line.lower().startswith(b"content-length")
            ))
            body = b""
            while len(body) < length:
                body += sock.recv(length - len(body))
        sock.close()
        assert all(b"200" in status for status in statuses)
        assert len(statuses) == 3
    finally:
        server.shutdown()
        server.server_close()


def test_range_support_is_advertised_on_frames(tmp_path):
    import urllib.request

    server, base = a_served_bundle(tmp_path)
    try:
        response = urllib.request.urlopen(f"{base}/c/f.ply", timeout=10)
        assert response.headers["Accept-Ranges"] == "bytes"
    finally:
        server.shutdown()
        server.server_close()


def test_a_byte_range_returns_exactly_those_bytes(tmp_path):
    import urllib.request

    content = bytes(range(256)) * 40
    server, base = a_served_bundle(tmp_path, payload=content)
    try:
        request = urllib.request.Request(f"{base}/c/f.ply")
        request.add_header("Range", "bytes=100-199")
        response = urllib.request.urlopen(request, timeout=10)
        body = response.read()
        assert response.status == 206
        assert body == content[100:200]
        assert response.headers["Content-Range"] == f"bytes 100-199/{len(content)}"
        assert response.headers["Content-Length"] == "100"
    finally:
        server.shutdown()
        server.server_close()


def test_an_open_ended_range_runs_to_the_end(tmp_path):
    import urllib.request

    content = bytes(range(256)) * 40
    server, base = a_served_bundle(tmp_path, payload=content)
    try:
        request = urllib.request.Request(f"{base}/c/f.ply")
        request.add_header("Range", f"bytes={len(content) - 10}-")
        response = urllib.request.urlopen(request, timeout=10)
        assert response.status == 206
        assert response.read() == content[-10:]
    finally:
        server.shutdown()
        server.server_close()


def test_a_range_past_the_end_is_refused(tmp_path):
    """416 rather than the whole file: a resuming client handed a full body
    would append a second copy to what it already had."""
    import urllib.error
    import urllib.request

    content = b"x" * 100
    server, base = a_served_bundle(tmp_path, payload=content)
    try:
        request = urllib.request.Request(f"{base}/c/f.ply")
        request.add_header("Range", "bytes=500-600")
        with pytest.raises(urllib.error.HTTPError) as raised:
            urllib.request.urlopen(request, timeout=10)
        assert raised.value.code == 416
        assert raised.value.headers["Content-Range"] == "bytes */100"
    finally:
        server.shutdown()
        server.server_close()


def test_the_live_proxy_closes_because_its_body_has_no_length(tmp_path):
    """Every other response is self-delimiting; a multipart stream cannot be,
    so under 1.1 it must be delimited by the close."""
    import inspect

    from streamer.server import _Handler

    source = inspect.getsource(_Handler._proxy_live)
    assert 'send_header("Connection", "close")' in source
    assert "close_connection = True" in source


def test_idle_connections_are_reaped(tmp_path):
    """Keep-alive means a client that stops talking holds a thread."""
    from streamer.server import _Handler

    assert _Handler.timeout and _Handler.timeout <= 120


# -------------------------------------------------------- parsing the header ---


@pytest.mark.parametrize("header,size,expected", [
    ("bytes=0-99", 1000, (0, 99)),
    ("bytes=500-", 1000, (500, 999)),
    ("bytes=-100", 1000, (900, 999)),
    ("bytes=0-5000", 1000, (0, 999)),        # clamped to the file
    ("bytes=-5000", 1000, (0, 999)),         # more than the file is the file
    ("bytes=999-999", 1000, (999, 999)),
])
def test_ranges_that_parse(header, size, expected):
    from streamer.server import _parse_range

    assert _parse_range(header, size) == expected


@pytest.mark.parametrize("header", [
    "", "items=0-9", "bytes=abc-def", "bytes=1000-", "bytes=2000-3000",
    "bytes=50-10", "bytes=-", "bytes=-0", "bytes=0-9,20-29",
])
def test_ranges_that_do_not(header):
    from streamer.server import _parse_range

    assert _parse_range(header, 1000) is None


# ------------------------------------------------------------ resuming a fetch ---


def test_a_partial_file_is_continued_not_restarted(tmp_path):
    """The payoff. A 100 MB frame interrupted at 60% should cost the remaining
    40%, not another 100%."""
    from streamer import transfer

    content = bytes(range(256)) * 200          # 51200 bytes
    server, base = a_served_bundle(tmp_path / "src", payload=content)
    try:
        destination = tmp_path / "dst"
        target = destination / "c" / "f.ply"
        target.parent.mkdir(parents=True, exist_ok=True)
        # Pretend an earlier attempt stopped 30000 bytes in.
        partial = target.with_name(target.name + ".partial")
        partial.write_bytes(content[:30000])

        written = transfer._download(f"{base}/c/f.ply", target, 10.0)
        assert target.read_bytes() == content
        assert written == len(content) - 30000
    finally:
        server.shutdown()
        server.server_close()


def test_a_resumed_fetch_reassembles_the_whole_file(tmp_path):
    from streamer import transfer

    content = bytes(range(256)) * 100
    server, base = a_served_bundle(tmp_path / "src", payload=content)
    try:
        destination = tmp_path / "dst"
        result = transfer.fetch(base, destination, timeout=10)
        assert (destination / "c" / "f.ply").read_bytes() == content
        assert result.bytes == len(content)
        # Again: nothing to do, so nothing crosses the wire.
        again = transfer.fetch(base, destination, timeout=10)
        assert again.bytes == 0
        assert again.skipped == ("c/f.ply",)
    finally:
        server.shutdown()
        server.server_close()


def test_a_server_that_ignores_the_range_restarts_the_file(tmp_path):
    """Rather than appending a second copy, which would be a corrupt file of
    exactly the size the completeness check accepts."""
    import http.server
    import socketserver
    import threading

    from streamer import transfer

    content = b"abcdefghij" * 100

    class Whole(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def do_GET(self):                      # noqa: N802
            self.send_response(200)            # 200, not 206: range ignored
            self.send_header("Content-Length", str(len(content)))
            self.end_headers()
            self.wfile.write(content)

        def log_message(self, *args):
            pass

    server = socketserver.TCPServer(("127.0.0.1", 0), Whole)
    threading.Thread(target=server.serve_forever, kwargs={"poll_interval": 0.02},
                     daemon=True).start()
    try:
        target = tmp_path / "f.bin"
        partial = target.with_name(target.name + ".partial")
        partial.write_bytes(b"STALE" * 10)
        url = f"http://127.0.0.1:{server.server_address[1]}/f.bin"
        transfer._download(url, target, 10.0)
        assert target.read_bytes() == content
    finally:
        server.shutdown()
        server.server_close()


def test_nagle_is_disabled():
    """Without this, keep-alive is slower than what it replaced.

    A response leaves as two writes, headers then body. Nagle holds the second
    until the first is acknowledged and the client's delayed-ACK timer sits on
    that for ~40 ms -- measured at 1.20 s for 30 frames against 0.01 s. Closing
    the connection used to hide it, because the FIN flushes, so the stall only
    appears once keep-alive works.
    """
    from streamer.server import _Handler

    assert _Handler.disable_nagle_algorithm is True


def test_a_keep_alive_batch_is_not_slower_than_closing(tmp_path):
    """The regression guard for the above, measured rather than asserted about.

    Generous threshold: this is checking for a 40 ms per-request stall, which
    is two orders of magnitude above the noise, not for a small regression.
    """
    import time
    import urllib.request

    server, base = a_served_bundle(tmp_path, payload=b"x" * 2048)
    try:
        opener = urllib.request.build_opener()
        started = time.monotonic()
        for _ in range(10):
            opener.open(f"{base}/c/f.ply", timeout=10).read()
        elapsed = time.monotonic() - started
        # Ten stalled requests would be ~0.4 s; ten healthy ones are ~0.01 s.
        assert elapsed < 0.2, f"{elapsed:.3f}s for 10 requests suggests a stall"
    finally:
        server.shutdown()
        server.server_close()


# ------------------------------------------------------------- compression ---
# The manifest names every frame of every clip, and an orbit export puts 216
# clips in a scene, so nine subjects come to 6.6 MB -- fetched before the page
# can draw anything. It is also hugely repetitive (the same notes on 216 clips,
# frame paths differing by four digits), which is what gzip eats: measured at
# 20.3x on the real bundle.


def _repetitive_bundle(tmp_path):
    """A bundle whose manifest is big and repetitive, like a real one."""
    note = (
        "free-viewpoint ReRF, ray-marched at 72 viewpoints around the capture "
        "ring on 3 rings, rendered ahead of time because the march needs a GPU"
    )
    clips = []
    for view in range(40):
        name = f"g-rerf-cam{view:03d}"
        frames = []
        for index in range(30):
            relative = f"{name}/frame_{index:04d}.jpg"
            target = tmp_path / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(b"\xff\xd8" + bytes(64))
            frames.append(relative)
        clips.append(bundle.Clip(
            name=name, representation="pixels", scene="s", method="rerf",
            camera=view, frames=frames, notes=[note, note],
        ))
    bundle.write(tmp_path, title="t", source="s", clips=clips)
    return tmp_path


def test_the_manifest_is_compressed_when_the_client_offers_it(tmp_path):
    root = _repetitive_bundle(tmp_path)
    with serving(root) as base:
        request = urllib.request.Request(
            f"{base}/view.json", headers={"Accept-Encoding": "gzip"})
        with urllib.request.urlopen(request) as response:
            wire = response.read()
            headers = dict(response.headers)

    assert headers.get("Content-Encoding") == "gzip"
    # Named whether or not this response is compressed: a cache holding the
    # identity form must not hand it to a client that asked for gzip.
    assert headers.get("Vary") == "Accept-Encoding"
    assert int(headers["Content-Length"]) == len(wire)
    identity = gzip.decompress(wire)
    assert len(identity) > 4 * len(wire), "repetitive JSON should compress hard"
    assert len(json.loads(identity)["clips"]) == 40


def test_a_client_that_does_not_offer_gzip_gets_the_plain_bytes(tmp_path):
    """`streamer.transfer` and urllib speak plain HTTP and would be handed
    bytes they will not decode."""
    root = _repetitive_bundle(tmp_path)
    with serving(root) as base:
        with urllib.request.urlopen(f"{base}/view.json") as response:
            body = response.read()
            headers = dict(response.headers)
    assert "Content-Encoding" not in headers
    assert json.loads(body)["title"] == "t"


def test_a_frame_container_is_never_compressed(tmp_path):
    """It is already compressed, so gzipping one spends CPU per request to save
    nothing -- and a compressed entity makes a Range request name bytes of
    something the client did not ask for, which is what the header prefetch
    relies on."""
    root = _repetitive_bundle(tmp_path)
    (root / "clip.seq").write_bytes(b"O4DSEQ\x00\x00" + bytes(4096))
    with serving(root) as base:
        request = urllib.request.Request(
            f"{base}/clip.seq", headers={"Accept-Encoding": "gzip"})
        with urllib.request.urlopen(request) as response:
            body = response.read()
            headers = dict(response.headers)
    assert "Content-Encoding" not in headers
    assert len(body) == 8 + 4096
    # Still resumable, which is the point of not compressing it.
    assert headers.get("Accept-Ranges") == "bytes"


def test_a_range_request_is_never_compressed(tmp_path):
    """A range names bytes of the entity as sent. Offering ranges over one form
    and serving another is how a resumed download reassembles garbage."""
    root = _repetitive_bundle(tmp_path)
    with serving(root) as base:
        request = urllib.request.Request(
            f"{base}/view.json",
            headers={"Accept-Encoding": "gzip", "Range": "bytes=0-31"})
        with urllib.request.urlopen(request) as response:
            status = response.status
            body = response.read()
            headers = dict(response.headers)
    assert status == 206
    assert "Content-Encoding" not in headers
    assert len(body) == 32
    # And the bytes are the identity form, so a caller can trust the offset.
    assert body.startswith(b'{\n  "version"')


def test_a_gzipped_response_does_not_advertise_byte_ranges(tmp_path):
    root = _repetitive_bundle(tmp_path)
    with serving(root) as base:
        request = urllib.request.Request(
            f"{base}/view.json", headers={"Accept-Encoding": "gzip"})
        with urllib.request.urlopen(request) as response:
            headers = dict(response.headers)
    assert headers.get("Content-Encoding") == "gzip"
    assert "Accept-Ranges" not in headers


def test_a_small_file_is_left_alone(tmp_path):
    """The gzip header and the trip through zlib cost more than they save."""
    root = tmp_path
    bundle.write(root, title="t", source="s", clips=[bundle.Clip(
        name="c", representation="pixels", scene="s", frames=["c/f.jpg"])])
    (root / "c").mkdir(exist_ok=True)
    (root / "c/f.jpg").write_bytes(b"\xff\xd8")
    (root / "tiny.json").write_bytes(b'{"a":1}')
    with serving(root) as base:
        request = urllib.request.Request(
            f"{base}/tiny.json", headers={"Accept-Encoding": "gzip"})
        with urllib.request.urlopen(request) as response:
            headers = dict(response.headers)
    assert "Content-Encoding" not in headers


def test_the_page_and_its_worker_are_both_compressed(tmp_path):
    """Two spellings of the JavaScript media type exist, and listing one left
    the worker going out whole while the page beside it was compressed."""
    root = _repetitive_bundle(tmp_path)
    with serving(root) as base:
        got = {}
        for path in ("/", "/client/worker.js"):
            request = urllib.request.Request(
                f"{base}{path}", headers={"Accept-Encoding": "gzip"})
            with urllib.request.urlopen(request) as response:
                got[path] = dict(response.headers).get("Content-Encoding")
    assert got == {"/": "gzip", "/client/worker.js": "gzip"}
