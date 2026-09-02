"""Whole-page checks on the client, of the kind a browser would catch too late.

The page is edited by hand and served as one file, so a syntax error or a
dangling reference ships silently: the server returns 200, the browser reports
it in a console nobody is watching, and every pane is blank. These are the two
cheapest checks that catch that class of mistake -- the script parses, and the
representation registry actually evaluates.
"""

from __future__ import annotations

import json
import shutil
import subprocess
from pathlib import Path

import pytest
from open4d.core import Representation

from streamer import representations
from streamer.client import viewer_path

pytestmark = pytest.mark.cpu

NODE = shutil.which("node")
requires_node = pytest.mark.skipif(NODE is None, reason="node is not installed")


def page() -> str:
    return viewer_path().read_text()


def script() -> str:
    """The page's script, which is where everything that can break lives."""
    source = page()
    start = source.index("<script>") + len("<script>")
    return source[start : source.rindex("</script>")]


def cut(name: str) -> str:
    """One top-level definition, by the name the page gives it."""
    source = page()
    for prefix in (f"function {name}(", f"const {name} =", f"class {name} "):
        start = source.find(prefix)
        if start < 0:
            continue
        if prefix.startswith("const"):
            return source[start : source.index(";\n", start) + 2]
        return source[start : source.index("\n}\n", start) + 3]
    raise AssertionError(f"{name} is not defined in the viewer")


@requires_node
def test_the_script_parses(tmp_path):
    path = tmp_path / "viewer.js"
    path.write_text(script())
    finished = subprocess.run(
        [NODE, "--check", str(path)], capture_output=True, text=True, timeout=120
    )
    assert finished.returncode == 0, finished.stderr


@requires_node
def test_the_representation_registry_evaluates(tmp_path):
    """It is a const literal that calls functions declared further down the file.

    That works only because those are function declarations and therefore
    hoisted. Reordering them into `const` arrow functions would leave the
    literal reading them in their temporal dead zone -- which fails at load,
    before anything renders, and only in a browser.
    """
    probe = "\n".join(
        cut(name)
        for name in (
            "parseMeshPly",
            "REPRESENTATIONS",
            "parseGaussianFrame",
            "decodeGeometry",
            "decodeImage",
        )
    ) + """
        const out = {};
        for (const [name, spec] of Object.entries(REPRESENTATIONS)) {
          out[name] = {
            decode: typeof spec.decode,
            geometry: spec.geometry,
            cacheSize: spec.cacheSize,
          };
        }
        process.stdout.write(JSON.stringify(out));
    """
    path = tmp_path / "registry.mjs"
    path.write_text(probe)
    finished = subprocess.run(
        [NODE, str(path)], capture_output=True, text=True, timeout=120
    )
    assert finished.returncode == 0, finished.stderr
    entries = json.loads(finished.stdout)

    assert set(entries) == {member.value for member in Representation}
    for name, entry in entries.items():
        assert entry["decode"] == "function", name
        assert entry["geometry"] is Representation(name).has_geometry, name
        assert isinstance(entry["cacheSize"], int) and entry["cacheSize"] > 0, name


def test_the_page_carries_no_external_dependency():
    """No build step and no CDN is what makes the page servable as one file."""
    source = page()
    for pattern in ("http://", "https://", "cdn.", "<script src"):
        assert pattern not in source, pattern


def test_every_playable_representation_has_a_client_entry():
    """A spec claiming playable with no entry renders an empty pane."""
    source = page()
    for spec in representations.playable():
        assert f"\n  {spec.name}: {{" in source, spec.name
