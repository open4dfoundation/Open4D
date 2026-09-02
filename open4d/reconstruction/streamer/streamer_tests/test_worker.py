"""The decode worker: its message protocol, and that it transfers rather than copies.

Parsing is the one expensive synchronous step in playback -- 16.5 ms for a 3DGS
PLY, 32.1 ms for a 439k-Gaussian `.splat`, 17.8 ms for a Draco mesh -- and on
the main thread each of those is a missed frame for *every* pane, because there
is only one main thread. So the codecs moved into a worker.

The parsers themselves are covered by `test_client_parsers.py` and
`test_draco.py`, which now read `worker.js` because that is where they live.
What is new and only tested here is the boundary: the message shape, the error
path, and that buffers cross it by transfer.
"""

from __future__ import annotations

import json
import re
import shutil
import subprocess
import textwrap
from pathlib import Path

import pytest

from streamer.client import viewer_path

pytestmark = pytest.mark.cpu

NODE = shutil.which("node")
requires_node = pytest.mark.skipif(NODE is None, reason="node is not installed")

CLIENT = viewer_path().parent
WORKER = CLIENT / "worker.js"


def worker_source() -> str:
    return WORKER.read_text()


def worker_code() -> str:
    """The worker with comments stripped.

    Needed because the worker's own documentation talks *about* the main thread
    -- it explains that parsing there costs a `requestAnimationFrame` frame --
    and a test grepping the raw text would flag that prose as a DOM reference.
    Assert on code, not on what the code says about itself.
    """
    source = re.sub(r"/\*.*?\*/", "", worker_source(), flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", source)


def run(body: str, tmp_path: Path, name: str = "w.mjs") -> object:
    """Run ``body`` with worker.js loaded under a stubbed worker global scope."""
    script = tmp_path / name
    script.write_text(
        textwrap.dedent(
            f"""
            import {{ readFileSync }} from "node:fs";
            import path from "node:path";
            const CLIENT = {str(CLIENT)!r};
            globalThis.self = globalThis;
            globalThis.importScripts = (url) => {{
              globalThis.self.DracoDecoderModule = require(path.join(CLIENT, url));
            }};
            globalThis.fetch = async (url) => ({{
              arrayBuffer: async () => {{
                const b = readFileSync(path.join(CLIENT, url));
                return b.buffer.slice(b.byteOffset, b.byteOffset + b.byteLength);
              }},
            }});
            globalThis.require = (await import("node:module")).createRequire(
              import.meta.url);
            const source = readFileSync({str(WORKER)!r}, "utf8");
            // Evaluated in this scope so `self.onmessage` lands on our stub, which
            // is exactly how a browser loads it.
            new Function(source).call(globalThis);
            """
        )
        + textwrap.dedent(body)
    )
    finished = subprocess.run(
        [NODE, str(script)], capture_output=True, text=True, timeout=180
    )
    if finished.returncode:
        raise AssertionError(finished.stderr)
    return json.loads(finished.stdout)


# ------------------------------------------------------------- the protocol ---


def test_the_worker_installs_a_message_handler():
    """Structural, and cheap: without it every decode hangs with no error."""
    source = worker_source()
    assert "self.onmessage" in source
    assert "self.postMessage" in source


def test_the_worker_holds_no_dom_or_rendering_code():
    """It has no DOM, and a reference to one is a crash on first use."""
    code = worker_code()
    for forbidden in ("document.", "window.", "getContext", "requestAnimationFrame"):
        assert forbidden not in code, forbidden


def test_the_worker_carries_no_pixel_codec():
    """An <img> needs the main thread; a browser already decodes one off it."""
    code = worker_code()
    start = code.index("const CODECS")
    table = code[start : code.index("\n};", start)]
    assert "pixels" not in table
    assert "decodeImage" not in code


@requires_node
def test_a_frame_comes_back_parsed(tmp_path):
    ply = next(
        (Path(__file__).resolve().parents[3] / "codecs/tvmc").rglob("*.obj"), None
    )
    if ply is None:
        pytest.skip("no mesh source present")
    # A minimal mesh PLY, so this test needs no exporter and no dataset.
    import struct

    header = (
        "ply\nformat binary_little_endian 1.0\nelement vertex 3\n"
        "property float x\nproperty float y\nproperty float z\n"
        "element face 1\nproperty list uchar int vertex_indices\nend_header\n"
    )
    body = b"".join(
        struct.pack("<fff", *p) for p in [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
    ) + struct.pack("<BIII", 3, 0, 1, 2)
    frame = tmp_path / "frame_000000.ply"
    frame.write_bytes(header.encode() + body)

    result = run(
        f"""
        const b = readFileSync({str(frame)!r});
        const ab = b.buffer.slice(b.byteOffset, b.byteOffset + b.byteLength);
        const out = await new Promise((resolve) => {{
          globalThis.self.postMessage = (message) => resolve(message);
          globalThis.self.onmessage({{ data: {{
            id: 7, representation: "mesh", url: "f.ply", buffer: ab }} }});
        }});
        process.stdout.write(JSON.stringify({{
          id: out.id,
          error: out.error || null,
          count: out.parsed && out.parsed.count,
          triangles: out.parsed && out.parsed.indices.length / 3,
        }}));
        """,
        tmp_path,
    )
    assert result["error"] is None
    assert result["id"] == 7
    assert (result["count"], result["triangles"]) == (3, 1)


@requires_node
def test_the_id_is_echoed_so_replies_can_be_matched(tmp_path):
    """Decodes are concurrent from the page's side; the id is how they pair up."""
    result = run(
        """
        const out = await new Promise((resolve) => {
          globalThis.self.postMessage = (message) => resolve(message);
          globalThis.self.onmessage({ data: {
            id: 4242, representation: "mesh", url: "f.nope",
            buffer: new ArrayBuffer(4) } });
        });
        process.stdout.write(JSON.stringify({ id: out.id, error: out.error }));
        """,
        tmp_path,
        name="id.mjs",
    )
    assert result["id"] == 4242
    assert result["error"]


@requires_node
def test_an_unknown_codec_comes_back_as_a_message_not_a_crash(tmp_path):
    """And the message names what this client does decode."""
    result = run(
        """
        const out = await new Promise((resolve) => {
          globalThis.self.postMessage = (message) => resolve(message);
          globalThis.self.onmessage({ data: {
            id: 1, representation: "mesh", url: "frame.obj",
            buffer: new ArrayBuffer(8) } });
        });
        process.stdout.write(JSON.stringify({ error: out.error }));
        """,
        tmp_path,
        name="unknown.mjs",
    )
    assert ".obj" in result["error"]
    assert ".ply" in result["error"] and ".drc" in result["error"]


@requires_node
def test_a_corrupt_frame_comes_back_as_a_message(tmp_path):
    """An exception inside a parser must not take the worker down with it."""
    result = run(
        """
        const out = await new Promise((resolve) => {
          globalThis.self.postMessage = (message) => resolve(message);
          globalThis.self.onmessage({ data: {
            id: 1, representation: "mesh", url: "f.ply",
            buffer: new TextEncoder().encode("not a ply at all").buffer } });
        });
        process.stdout.write(JSON.stringify({
          error: out.error, stillAlive: typeof globalThis.self.onmessage,
        }));
        """,
        tmp_path,
        name="corrupt.mjs",
    )
    assert result["error"]
    assert result["stillAlive"] == "function"


@requires_node
def test_the_reply_transfers_its_buffers_rather_than_copying(tmp_path):
    """A parsed frame is megabytes; copying it back undoes much of the saving."""
    import struct

    header = (
        "ply\nformat binary_little_endian 1.0\nelement vertex 3\n"
        "property float x\nproperty float y\nproperty float z\n"
        "element face 1\nproperty list uchar int vertex_indices\nend_header\n"
    )
    body = b"".join(
        struct.pack("<fff", *p) for p in [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
    ) + struct.pack("<BIII", 3, 0, 1, 2)
    frame = tmp_path / "f.ply"
    frame.write_bytes(header.encode() + body)

    result = run(
        f"""
        const b = readFileSync({str(frame)!r});
        const ab = b.buffer.slice(b.byteOffset, b.byteOffset + b.byteLength);
        const out = await new Promise((resolve) => {{
          globalThis.self.postMessage = (message, transfer) =>
            resolve({{ message, transfer }});
          globalThis.self.onmessage({{ data: {{
            id: 1, representation: "mesh", url: "f.ply", buffer: ab }} }});
        }});
        const parsed = out.message.parsed;
        const arrays = Object.entries(parsed)
          .filter(([, v]) => v && v.buffer instanceof ArrayBuffer)
          .map(([k]) => k);
        process.stdout.write(JSON.stringify({{
          transferred: (out.transfer || []).length,
          arrays: arrays.sort(),
        }}));
        """,
        tmp_path,
        name="transfer.mjs",
    )
    # Every typed array in the reply is in the transfer list, collected by
    # inspection so a parser that grows a field does not have to declare it.
    assert result["transferred"] == len(result["arrays"])
    assert result["transferred"] >= 2
    assert "positions" in result["arrays"]


# ------------------------------------------------------------ the page side ---


def test_the_page_no_longer_parses_anything():
    """The whole point: nothing that costs 16 to 32 ms runs on the main thread."""
    page = viewer_path().read_text()
    for parser in ("function parsePly(", "function parseMeshPly(",
                   "function parseSplat(", "function parseDraco("):
        assert parser not in page, parser


def test_the_page_starts_one_worker_and_matches_replies_by_id():
    page = viewer_path().read_text()
    start = page.index("class WorkerDecoder")
    body = page[start : page.index("\nconst decoder", start)]
    assert "new Worker(" in body
    assert "this.pending" in body
    assert "postMessage({ id, representation, url, buffer }, [buffer])" in body
    # A worker that cannot start must reject in flight requests, or the page
    # sits at "loading" with no reason given.
    assert "onerror" in body


def test_the_worker_is_served_from_the_client_package(tmp_path):
    """It is fetched by URL, so it has to be a file the server hands out."""
    from streamer import bundle
    from streamer.server import serve
    import urllib.request

    bundle.write(
        tmp_path,
        title="t",
        source="s",
        clips=[bundle.Clip(name="c", representation="mesh", frames=["c/f.ply"])],
    )
    server = serve(tmp_path, port=0, block=False)
    try:
        base = f"http://127.0.0.1:{server.server_address[1]}"
        response = urllib.request.urlopen(f"{base}/client/worker.js", timeout=10)
        assert response.status == 200
        assert response.headers["Content-Type"] == "text/javascript"
        assert b"self.onmessage" in response.read()
    finally:
        server.shutdown()
        server.server_close()
