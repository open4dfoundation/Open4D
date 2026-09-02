"""A compressed format on the wire, decoded in the client.

The first one. Everything else this client reads is an interchange dump of
geometry or a picture: measured on the mesh sequence the TVMC codec vendors,
761 kB of PLY a frame becomes 59 kB of Draco, which is 1.77 MB/s at 30 fps
rather than 23. That is the difference between a link and a LAN.

Two costs, both bounded and both measured in
`test_quantisation_error_is_small_and_bounded`: positions are quantised, and
duplicate vertices are merged so the decoded count is lower than the encoder
was given. A `.drc` frame is a delivery form; the PLY stays the source of truth.
"""

from __future__ import annotations

import json
import shutil
import subprocess
import textwrap
import urllib.request
from pathlib import Path

import numpy as np
import pytest

from streamer import bundle, export, representations
from streamer.client import viewer_path
from streamer.server import serve

pytestmark = pytest.mark.cpu

NODE = shutil.which("node")
requires_node = pytest.mark.skipif(NODE is None, reason="node is not installed")

DracoPy = pytest.importorskip("DracoPy", reason="Draco frames need open4d[draco]")

CLIENT = viewer_path().parent
VENDOR = CLIENT / "vendor" / "draco"
MESH_SOURCE = (
    Path(__file__).resolve().parents[3]
    / "codecs/tvmc/arap-volume-tracking/data/basketball_player"
)


def mesh_sequence(frames: int = 2):
    """A short real sequence, or a skip. Synthetic geometry compresses unlike real."""
    import open4d

    if not MESH_SOURCE.is_dir():
        pytest.skip(f"{MESH_SOURCE} is not present")
    return open4d.load(MESH_SOURCE, fps=10), frames


# ------------------------------------------------------------ the decoder ---


def test_the_decoder_is_vendored_not_fetched_from_a_cdn():
    """Served from this origin is what keeps the page free of external deps."""
    assert (VENDOR / "draco_wasm_wrapper.js").is_file()
    assert (VENDOR / "draco_decoder.wasm").is_file()
    assert (VENDOR / "README.md").is_file()


def test_the_wrapper_and_the_module_are_a_matched_pair():
    """Refreshing one alone is the failure the README warns about."""
    notes = (VENDOR / "README.md").read_text()
    assert "matched pair" in notes
    assert "google/draco" in notes


# ------------------------------------------------------------ the registry ---


def test_drc_is_registered_for_the_representations_it_carries():
    assert ".drc" in representations.spec("mesh").media_types
    assert ".drc" in representations.spec("points").media_types
    assert representations.media_types()[".drc"] == "application/octet-stream"


# ------------------------------------------------------------- the encoder ---


def test_the_exporter_writes_one_draco_frame_per_frame(tmp_path):
    """Per frame, not one container: a streaming client fetches frames."""
    sequence, frames = mesh_sequence()
    with sequence as seq:
        clip = export.from_sequence(
            seq[:frames], tmp_path, name="mesh", frame_format="draco"
        )
    assert len(clip.frames) == frames
    assert all(name.endswith(".drc") for name in clip.frames)
    assert clip.representation == "mesh"
    assert clip.detail["frame_format"] == "draco"
    assert clip.detail["quantization_bits"] == export.DRACO_QUANTIZATION_BITS


def test_draco_frames_are_much_smaller_than_ply(tmp_path):
    sequence, frames = mesh_sequence(1)
    with sequence as seq:
        as_ply = export.from_sequence(seq[:frames], tmp_path / "p", name="m")
        as_drc = export.from_sequence(
            seq[:frames], tmp_path / "d", name="m", frame_format="draco"
        )
    ply_size = (tmp_path / "p" / as_ply.frames[0]).stat().st_size
    drc_size = (tmp_path / "d" / as_drc.frames[0]).stat().st_size
    # Measured at 12.9x on this content; asserted loosely so a Draco version
    # bump does not fail the suite for being slightly different.
    assert drc_size * 5 < ply_size, f"{ply_size} -> {drc_size}"


def test_the_notes_say_it_is_lossy_and_how(tmp_path):
    sequence, _ = mesh_sequence(1)
    with sequence as seq:
        clip = export.from_sequence(
            seq[:1], tmp_path, name="m", frame_format="draco"
        )
    notes = " ".join(clip.notes)
    assert "delivery form" in notes
    assert "quantisation" in notes
    assert "merging duplicate vertices" in notes


def test_a_ply_clip_says_nothing_about_quantisation(tmp_path):
    sequence, _ = mesh_sequence(1)
    with sequence as seq:
        clip = export.from_sequence(seq[:1], tmp_path, name="m")
    assert "quantization_bits" not in clip.detail
    assert not any("quantisation" in note for note in clip.notes)


def test_an_unknown_frame_format_is_refused(tmp_path):
    sequence, _ = mesh_sequence(1)
    with sequence as seq:
        with pytest.raises(ValueError, match="unknown frame format"):
            export.from_sequence(seq[:1], tmp_path, name="m", frame_format="obj")


def test_quantisation_error_is_small_and_bounded():
    """What the compression costs, stated as a number rather than a hope."""
    scipy_spatial = pytest.importorskip("scipy.spatial")
    import open4d

    if not MESH_SOURCE.is_dir():
        pytest.skip("mesh source is not present")
    with open4d.load(MESH_SOURCE, fps=10) as seq:
        geometry = seq[0].geometry
    positions = geometry.positions.astype(np.float64)
    diagonal = float(np.linalg.norm(positions.max(0) - positions.min(0)))

    payload = DracoPy.encode(
        geometry.positions.astype(np.float32),
        geometry.triangles.astype(np.uint32),
        quantization_bits=export.DRACO_QUANTIZATION_BITS,
    )
    decoded = np.asarray(DracoPy.decode(payload).points, dtype=np.float64).reshape(-1, 3)
    error = scipy_spatial.cKDTree(decoded).query(positions)[0]
    # 0.0046% of the diagonal when this was written. One tenth of a percent is a
    # generous ceiling that still fails if quantisation is turned down hard.
    assert error.max() / diagonal < 1e-3
    # Deduplication, not quantisation: the source splits vertices at seams.
    assert len(decoded) <= len(positions)


# -------------------------------------------------------------- the server ---


def draco_bundle(tmp_path: Path) -> Path:
    sequence, frames = mesh_sequence(2)
    with sequence as seq:
        clip = export.from_sequence(
            seq[:frames], tmp_path, name="mesh", frame_format="draco"
        )
    bundle.write(tmp_path, title="draco", source=str(MESH_SOURCE), clips=[clip])
    return tmp_path


def test_the_server_hands_out_the_decoder_and_the_frames(tmp_path):
    server = serve(draco_bundle(tmp_path), port=0, block=False)
    try:
        base = f"http://127.0.0.1:{server.server_address[1]}"
        wrapper = urllib.request.urlopen(f"{base}/client/vendor/draco/draco_wasm_wrapper.js")
        assert wrapper.headers["Content-Type"] == "text/javascript"
        module = urllib.request.urlopen(f"{base}/client/vendor/draco/draco_decoder.wasm")
        # A browser refuses to compile a module served as anything else.
        assert module.headers["Content-Type"] == "application/wasm"
        frame = urllib.request.urlopen(f"{base}/mesh/frame_000000.drc")
        assert frame.headers["Content-Type"] == "application/octet-stream"
    finally:
        server.shutdown()
        server.server_close()


@pytest.mark.parametrize(
    "path", ["/client/../../../etc/passwd", "/client/nope.js", "/client/"]
)
def test_the_client_asset_route_refuses_anything_outside_the_package(tmp_path, path):
    """The path comes off a URL, so traversal is a request this will receive."""
    import urllib.error

    server = serve(draco_bundle(tmp_path), port=0, block=False)
    try:
        base = f"http://127.0.0.1:{server.server_address[1]}"
        with pytest.raises(urllib.error.HTTPError) as raised:
            urllib.request.urlopen(f"{base}{path}", timeout=10)
        assert raised.value.code == 404
    finally:
        server.shutdown()
        server.server_close()


# --------------------------------------------------- the shipped decoder ---


def _cut(name: str) -> str:
    page = viewer_path().read_text()
    # `async function X(` first: `function X(` is a substring of it, and slicing
    # from the shorter match drops the async keyword and will not parse.
    for prefix in (
        f"async function {name}(",
        f"function {name}(",
        f"const {name} =",
        f"let {name} =",
    ):
        start = page.find(prefix)
        if start < 0:
            continue
        if prefix.startswith(("const", "let")):
            return page[start : page.index(";\n", start) + 2]
        line = page[start : page.index("\n", start)]
        if line.count("{") and line.count("{") == line.count("}"):
            return line
        return page[start : page.index("\n}\n", start) + 3]
    raise AssertionError(f"{name} is not defined in the viewer")


def decode_with_client(frame: Path, tmp_path: Path) -> dict:
    """Decode ``frame`` with the client's own code, under Node.

    Stubs only the two browser facilities `loadDraco` uses -- a script tag and
    `fetch` -- and maps the client route onto the package the way the server
    does. Everything under test is the shipped thing.
    """
    script = tmp_path / f"decode_{frame.stem}.cjs"
    script.write_text(
        textwrap.dedent(
            f"""
            const fs = require("fs");
            const path = require("path");
            const CLIENT = {str(CLIENT)!r};
            const onDisk = (url) => path.join(CLIENT, url.replace(/^client\\//, ""));
            globalThis.self = globalThis;
            globalThis.document = {{
              head: {{ appendChild(tag) {{ tag.onload(); }} }},
              createElement() {{
                return {{
                  set src(value) {{
                    globalThis.self.DracoDecoderModule = require(onDisk(value));
                  }},
                }};
              }},
            }};
            globalThis.fetch = async (url) => ({{
              arrayBuffer: async () => {{
                const b = fs.readFileSync(onDisk(url));
                return b.buffer.slice(b.byteOffset, b.byteOffset + b.byteLength);
              }},
            }});
            """
        )
        + "\n".join(
            _cut(name)
            for name in (
                "DRACO_PATH",
                "dracoModule",
                "loadDraco",
                "parseDraco",
                "parseMeshPly",
                "parseGeometryFrame",
            )
        )
        + textwrap.dedent(
            f"""
            (async () => {{
              const b = fs.readFileSync({str(frame)!r});
              const ab = b.buffer.slice(b.byteOffset, b.byteOffset + b.byteLength);
              const parsed = await parseGeometryFrame(ab, {str(frame)!r});
              process.stdout.write(JSON.stringify({{
                count: parsed.count,
                triangles: parsed.indices.length / 3,
                maxIndexOk: parsed.indices.reduce((a, x) => Math.max(a, x), 0)
                            < parsed.count,
                boundsFinite: parsed.bounds.flat().every(Number.isFinite),
                bounds: parsed.bounds,
              }}));
            }})().catch((e) => {{
              process.stderr.write(String(e && e.message || e));
              process.exit(1);
            }});
            """
        )
    )
    finished = subprocess.run(
        [NODE, str(script)], capture_output=True, text=True, timeout=180
    )
    assert finished.returncode == 0, finished.stderr
    return json.loads(finished.stdout)


@requires_node
def test_the_shipped_client_decodes_a_real_draco_frame(tmp_path):
    root = draco_bundle(tmp_path)
    result = decode_with_client(root / "mesh" / "frame_000000.drc", tmp_path)
    assert result["count"] > 10_000
    assert result["triangles"] > 10_000
    assert result["maxIndexOk"]
    assert result["boundsFinite"]


@requires_node
def test_draco_and_ply_frames_agree_on_the_bounds(tmp_path):
    """The strongest cheap check that the geometry survived: same box, both ways.

    The PLY clip's bounds are computed on the Python side from the decoded
    sequence; the Draco frame's come out of the client. Agreement means the two
    paths are reading the same geometry, not merely both producing something.
    """
    sequence, _ = mesh_sequence(1)
    with sequence as seq:
        ply_clip = export.from_sequence(seq[:1], tmp_path / "p", name="m")
    root = draco_bundle(tmp_path / "d")
    decoded = decode_with_client(root / "mesh" / "frame_000000.drc", tmp_path)

    lower, upper = decoded["bounds"]
    # Quantisation moves a vertex by well under a hundredth of a unit here; the
    # tolerance is loose enough not to be a quantisation-bit regression test and
    # tight enough to fail if the axes were reordered or the scale was lost.
    for got, expected in zip(lower, ply_clip.bounds_min):
        assert abs(got - expected) < 0.05, (lower, ply_clip.bounds_min)
    for got, expected in zip(upper, ply_clip.bounds_max):
        assert abs(got - expected) < 0.05, (upper, ply_clip.bounds_max)
