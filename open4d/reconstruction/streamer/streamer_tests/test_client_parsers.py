"""The client's frame parsers, run as shipped.

The client is a browser page, so nothing in a Python suite had ever executed a
line of it -- the parsers were the largest untested surface in the module, and
they are exactly where a format mistake turns into "the pane is blank" with no
error anywhere. These run the real functions, extracted from the real
`viewer.html`, under Node against files Open4D's own writers produced.

Extracted by text rather than imported because the page is deliberately one file
with no build step and no module system: that is what makes it servable as-is,
and the cost is that a test has to cut the function out. Cutting it out is also
the point -- a copy of the parser in a fixture would pass while the shipped one
was broken.

Skipped when Node is absent, which is a real gap rather than a hidden one: the
suite says so instead of quietly covering less.
"""

from __future__ import annotations

import json
import shutil
import struct
import subprocess
import textwrap
from pathlib import Path

import numpy as np
import pytest
from open4d import Frame, MemoryFrameProvider, PointCloud, Sequence, TriangleMesh
from open4d.io import write_sequence

from streamer.client import viewer_path

pytestmark = pytest.mark.cpu

NODE = shutil.which("node")
requires_node = pytest.mark.skipif(NODE is None, reason="node is not installed")

#: The parsers under test, by the name they are defined under in the page.
PARSERS = ("parseMeshPly", "parsePly", "parseSplat")


def _extract(name: str) -> str:
    """One top-level function's source out of the page."""
    page = viewer_path().read_text()
    start = page.index(f"function {name}(")
    end = page.index("\n}\n", start) + 3
    return page[start:end]


def run_parser(name: str, path: Path, tmp_path: Path) -> dict:
    """Parse ``path`` with the page's ``name`` and return a JSON-able summary."""
    script = tmp_path / f"run_{name}.mjs"
    script.write_text(
        _extract(name)
        + textwrap.dedent(
            f"""
            import {{ readFileSync }} from "node:fs";
            const bytes = readFileSync(process.argv[2]);
            const parsed = {name}(bytes.buffer.slice(
                bytes.byteOffset, bytes.byteOffset + bytes.byteLength));
            const out = {{
              count: parsed.count,
              triangles: parsed.indices ? parsed.indices.length / 3 : 0,
              maxIndex: parsed.indices
                ? parsed.indices.reduce((a, b) => Math.max(a, b), 0) : -1,
              hasColors: !!parsed.colors,
              firstColors: parsed.colors ? Array.from(parsed.colors.slice(0, 8)) : [],
              positions: Array.from((parsed.positions || []).slice(0, 6)),
              opacity: parsed.data ? parsed.data[3] : null,
              bounds: parsed.bounds || null,
            }};
            process.stdout.write(JSON.stringify(out));
            """
        )
    )
    finished = subprocess.run(
        [NODE, str(script), str(path)], capture_output=True, text=True, timeout=120
    )
    if finished.returncode:
        raise AssertionError(f"{name} failed on {path.name}:\n{finished.stderr}")
    return json.loads(finished.stdout)


# ------------------------------------------------------------------ fixtures ---


def write_one(geometry, directory: Path) -> Path:
    sequence = Sequence(MemoryFrameProvider([Frame(0, 0.0, geometry)]))
    write_sequence(sequence, directory, format="ply", overwrite=True)
    return directory / "frame_000000.ply"


@pytest.fixture
def mesh_ply(tmp_path):
    return write_one(
        TriangleMesh(
            np.asarray([[0, 0, 0], [1, 0, 0], [0, 1, 0], [1, 1, 0]], dtype=np.float32),
            np.asarray([[0, 1, 2], [1, 3, 2]], dtype=np.uint32),
            colors=np.asarray(
                [[1, 0, 0], [0, 1, 0], [0, 0, 1], [1, 1, 0]], dtype=np.float32
            ),
        ),
        tmp_path / "mesh",
    )


@pytest.fixture
def points_ply(tmp_path):
    rng = np.random.default_rng(0)
    return write_one(
        PointCloud(rng.random((25, 3)).astype(np.float32)), tmp_path / "points"
    )


# --------------------------------------------------------------- parseMeshPly ---


@requires_node
def test_reads_a_mesh_open4d_wrote(mesh_ply, tmp_path):
    result = run_parser("parseMeshPly", mesh_ply, tmp_path)
    assert result["count"] == 4
    assert result["triangles"] == 2
    assert result["maxIndex"] < result["count"]
    assert result["hasColors"]


@requires_node
def test_float_colour_scales_to_bytes(mesh_ply, tmp_path):
    """Open4D stores colour as float in [0, 1]; most other tools use uchar."""
    result = run_parser("parseMeshPly", mesh_ply, tmp_path)
    assert result["firstColors"] == [255, 0, 0, 255, 0, 255, 0, 255]


@requires_node
def test_uchar_colour_reads_identically(tmp_path):
    header = (
        "ply\nformat binary_little_endian 1.0\nelement vertex 3\n"
        "property float x\nproperty float y\nproperty float z\n"
        "property uchar red\nproperty uchar green\nproperty uchar blue\n"
        "element face 1\nproperty list uchar int vertex_indices\nend_header\n"
    )
    body = b"".join(
        struct.pack("<fffBBB", *values)
        for values in [(0, 0, 0, 255, 0, 0), (1, 0, 0, 0, 255, 0), (0, 1, 0, 0, 0, 255)]
    ) + struct.pack("<BIII", 3, 0, 1, 2)
    path = tmp_path / "uchar.ply"
    path.write_bytes(header.encode() + body)
    result = run_parser("parseMeshPly", path, tmp_path)
    assert result["firstColors"] == [255, 0, 0, 255, 0, 255, 0, 255]


@requires_node
def test_a_quad_is_fan_triangulated(tmp_path):
    """Open4D only writes triangles; other producers write n-gons."""
    header = (
        "ply\nformat binary_little_endian 1.0\nelement vertex 4\n"
        "property float x\nproperty float y\nproperty float z\n"
        "element face 1\nproperty list uchar int vertex_indices\nend_header\n"
    )
    body = b"".join(
        struct.pack("<fff", *p) for p in [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)]
    ) + struct.pack("<BIIII", 4, 0, 1, 2, 3)
    path = tmp_path / "quad.ply"
    path.write_bytes(header.encode() + body)
    assert run_parser("parseMeshPly", path, tmp_path)["triangles"] == 2


@requires_node
def test_a_point_cloud_has_no_faces_and_still_parses(points_ply, tmp_path):
    """The renderer falls through to points when a frame carries no faces."""
    result = run_parser("parseMeshPly", points_ply, tmp_path)
    assert result["count"] == 25
    assert result["triangles"] == 0


@requires_node
def test_bounds_are_finite_and_ordered(mesh_ply, tmp_path):
    lower, upper = run_parser("parseMeshPly", mesh_ply, tmp_path)["bounds"]
    assert all(low <= high for low, high in zip(lower, upper))
    assert all(abs(value) < 1e30 for value in lower + upper)


@requires_node
def test_a_real_captured_sequence_parses(tmp_path):
    """The 10-frame basketball OBJ sequence the TVMC codec vendors, via open4d.load."""
    import open4d

    source = (
        Path(__file__).resolve().parents[3]
        / "codecs/tvmc/arap-volume-tracking/data/basketball_player"
    )
    if not source.is_dir():
        pytest.skip(f"{source} is not present")
    with open4d.load(source, fps=10) as sequence:
        write_sequence(sequence[:1], tmp_path / "real", format="ply", overwrite=True)
    result = run_parser("parseMeshPly", tmp_path / "real" / "frame_000000.ply", tmp_path)
    assert result["count"] > 10_000
    assert result["triangles"] > 10_000
    assert result["maxIndex"] < result["count"]


# ------------------------------------------------- the Gaussian parsers too ---


@requires_node
def test_parse_splat_reads_activated_values(tmp_path):
    """.splat stores activated values; applying sigmoid again renders as fog."""
    count = 4
    payload = bytearray()
    for i in range(count):
        payload += struct.pack("<ffffff", float(i), 0.0, 0.0, 0.1, 0.1, 0.1)
        payload += bytes([200, 100, 50, 128])          # rgb + opacity
        # w = 1 would be 256, so the encoder clamps it to 255; the reader
        # renormalises, which is what makes that clamp harmless.
        payload += bytes([255, 128, 128, 128])
    path = tmp_path / "f.splat"
    path.write_bytes(bytes(payload))
    result = run_parser("parseSplat", path, tmp_path)
    assert result["count"] == count
    # 128/255, carried straight through rather than passed through a sigmoid.
    assert result["opacity"] == pytest.approx(128 / 255, abs=1e-6)
    assert result["firstColors"][:3] == [200, 100, 50]


@requires_node
def test_parse_splat_rejects_a_truncated_frame(tmp_path):
    path = tmp_path / "bad.splat"
    path.write_bytes(bytes(33))
    with pytest.raises(AssertionError, match="not a multiple of 32"):
        run_parser("parseSplat", path, tmp_path)


@requires_node
def test_every_named_parser_is_still_present():
    """The extraction is by name, so a rename must fail here, not silently."""
    page = viewer_path().read_text()
    for name in PARSERS:
        assert f"function {name}(" in page
