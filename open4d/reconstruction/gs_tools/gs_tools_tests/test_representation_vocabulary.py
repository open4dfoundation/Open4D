"""`streamer`, `gs_tools` and `open4d.core` must name representations alike.

These drifted apart once already: the viewer had `splats`/`images` while core
had only triangle meshes, and the cost was that adding a representation meant
editing every function that touched a clip. The point of the shared vocabulary
is that it stays shared, so it is asserted rather than trusted -- and now that
the producer and the streaming module are separate packages, nothing but a test
holds them to it.
"""

from __future__ import annotations

import re
from pathlib import Path

import pytest

from open4d.core import Representation

from streamer import bundle
from streamer.client import viewer_path

from gs_tools.methods import capture, rerf, vega

CORE_VALUES = {member.value for member in Representation}


def _viewer_source() -> str:
    return viewer_path().read_text()


def _worker_source() -> str:
    """The decode worker, which is where the geometry codecs live."""
    return (viewer_path().parent / "worker.js").read_text()


def _codec_table(source: str) -> dict[str, set[str]]:
    """The client's CODECS table: representation -> the suffixes it decodes."""
    body = re.search(r"const CODECS = \{(.*?)\n\};", source, re.DOTALL)
    assert body, "CODECS table not found"
    found: dict[str, set[str]] = {}
    for name, entries in re.findall(
        r"^  (\w+):\s*\{(.*?)\},$", body.group(1), re.MULTILINE
    ):
        found[name] = set(re.findall(r'"(\.[a-z0-9]+)"', entries))
    return found


def test_the_client_decodes_exactly_what_the_codec_registry_promises():
    """Python says a `.drc` mesh is client-decodable; the client must agree.

    Two hand-maintained tables in two languages -- a Python registry that tells a
    producer what it may write, and a JavaScript one that decides what actually
    parses. A promise on one side with no parser on the other is a blank pane,
    which is the failure this registry exists to remove.

    Geometry is decoded in the worker and pixels on the main thread, so the two
    halves are checked separately rather than against one table. That split is
    deliberate: an `<img>` needs the main thread, and a browser already decodes
    one off it.
    """
    from streamer import codecs

    worker = _codec_table(_worker_source())
    for representation in Representation:
        promised = {spec.suffix for spec in codecs.client_decodable(representation)}
        if representation.has_geometry:
            assert worker.get(representation.value, set()) == promised, representation
        else:
            # Pixels never reach the worker.
            assert representation.value not in worker


def test_the_page_decodes_pixels_and_delegates_the_rest():
    """The other half of the split, so neither side is left unasserted."""
    from streamer import codecs

    page = _viewer_source()
    table = re.search(r"const REPRESENTATIONS = \{(.*?)\n\};", page, re.DOTALL)
    assert table, "REPRESENTATIONS not found in the page"
    for name, body in re.findall(
        r"^  (\w+): \{(.*?)^  \},$", table.group(1), re.MULTILINE | re.DOTALL
    ):
        if Representation(name).has_geometry:
            assert f'decodeFrame("{name}")' in body, name
        else:
            assert "decodeImage" in body, name
    # And the suffixes Python promises for pixels are the ones decodeImage gets.
    assert {spec.suffix for spec in codecs.client_decodable("pixels")} == {
        ".jpg", ".jpeg", ".png"
    }


def test_the_client_does_not_decode_what_python_says_is_server_side():
    """ReRF can never decode in a browser; a parser for it would be a lie."""
    from streamer import codecs

    worker = _codec_table(_worker_source())
    page = _viewer_source()
    for spec in codecs.known():
        if spec.decodes == "server":
            assert spec.suffix not in worker.get(spec.representation.value, set())
            assert spec.suffix not in page


def _registry_keys(source: str) -> set[str]:
    """The keys of the viewer's REPRESENTATIONS object literal."""
    body = re.search(
        r"const REPRESENTATIONS = \{(.*?)\n\};", source, re.DOTALL
    )
    assert body, "REPRESENTATIONS literal not found in the viewer"
    return set(re.findall(r"^  (\w+): \{", body.group(1), re.MULTILINE))


def _legacy_map(source: str) -> dict[str, str]:
    line = re.search(r"const LEGACY_KINDS = \{(.*?)\};", source)
    assert line, "LEGACY_KINDS not found in the viewer"
    return dict(re.findall(r"(\w+): \"(\w+)\"", line.group(1)))


def test_viewer_only_knows_representations_core_defines():
    """A key the viewer invents is a vocabulary fork; catch it here."""
    assert _registry_keys(_viewer_source()) <= CORE_VALUES


def test_the_viewer_renders_every_representation_core_defines():
    assert _registry_keys(_viewer_source()) == CORE_VALUES


def test_geometry_flags_agree_with_core():
    """`geometry:` in the viewer is `Representation.has_geometry`, not a second opinion."""
    source = _viewer_source()
    body = re.search(r"const REPRESENTATIONS = \{(.*?)\n\};", source, re.DOTALL)
    for name, flag in re.findall(
        r"^  (\w+): \{\n\s*geometry: (true|false),", body.group(1), re.MULTILINE
    ):
        assert Representation(name).has_geometry is (flag == "true"), name


def test_legacy_kinds_migrate_onto_real_representations():
    mapping = _legacy_map(_viewer_source())
    assert mapping == {"splats": "gaussians", "images": "pixels"}
    assert set(mapping.values()) <= CORE_VALUES


@pytest.mark.parametrize(
    ("module", "expected"),
    [(vega, "gaussians"), (rerf, "pixels"), (capture, "pixels")],
)
def test_every_exporter_declares_a_core_representation(module, expected):
    source = Path(module.__file__).read_text()
    found = set(re.findall(r'representation="(\w+)"', source))
    assert found == {expected}
    assert found <= CORE_VALUES


def test_no_exporter_still_writes_the_v1_field():
    for module in (vega, rerf, capture):
        assert 'kind="' not in Path(module.__file__).read_text()


def test_bundle_version_was_bumped_for_the_field_rename():
    """A v1 bundle carries `kind`; a reader has to be able to tell them apart."""
    assert bundle.VERSION >= 2


def test_clip_has_no_kind_field():
    assert "kind" not in {field.name for field in bundle.dataclasses.fields(bundle.Clip)}
    assert "representation" in {
        field.name for field in bundle.dataclasses.fields(bundle.Clip)
    }
