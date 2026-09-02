"""What is on the wire, and who can decode it.

The registry that answers "can this module be streamed to a browser at all"
without reading the client's source. Before it existed, Open4D's codecs produced
nine suffixes the client could decode none of, and nothing in the code said so.
"""

from __future__ import annotations

import pytest
from open4d.core import Representation

from streamer import codecs
from streamer.codecs import CodecSpec

pytestmark = pytest.mark.cpu


# ------------------------------------------------- keyed by both, not by suffix ---


def test_the_same_suffix_is_a_different_codec_per_representation():
    """`.ply` is the reason the key is a pair: a 3DGS cloud and a mesh share it."""
    assert codecs.for_frame("mesh", ".ply").name == "mesh-ply"
    assert codecs.for_frame("points", ".ply").name == "points-ply"
    assert codecs.for_frame("gaussians", ".ply").name == "3dgs-ply"


def test_for_frame_accepts_the_enum_or_the_wire_value():
    assert codecs.for_frame(Representation.MESH, ".drc") is codecs.for_frame(
        "mesh", ".drc"
    )


def test_for_frame_is_case_insensitive_on_the_suffix():
    assert codecs.for_frame("mesh", ".DRC").name == "mesh-draco"


def test_an_unknown_frame_names_what_is_registered():
    """The message is the point: a blank pane becomes a sentence."""
    with pytest.raises(KeyError) as raised:
        codecs.for_frame("mesh", ".obj")
    message = str(raised.value)
    assert ".obj" in message
    assert ".ply" in message and ".drc" in message


def test_a_suffix_registered_for_one_representation_is_not_found_for_another():
    with pytest.raises(KeyError):
        codecs.for_frame("gaussians", ".drc")


def test_by_name_finds_a_codec():
    assert codecs.by_name("mesh-draco").suffix == ".drc"
    with pytest.raises(KeyError, match="no codec named"):
        codecs.by_name("nope")


# --------------------------------------------------------- the decodes axis ---


def test_rerf_is_registered_as_server_decoded():
    """Not an omission: its entropy coder is a sourceless CPython 3.8 binary."""
    spec = codecs.for_frame("pixels", ".rerf")
    assert spec.decodes == "server"
    assert "browser" in spec.cost


def test_client_decodable_excludes_it():
    assert "rerf" not in {spec.name for spec in codecs.client_decodable()}
    assert "rerf" in {spec.name for spec in codecs.known()}


def test_client_decodable_answers_per_representation():
    assert {spec.name for spec in codecs.client_decodable("gaussians")} == {
        "3dgs-ply",
        "splat",
    }
    assert {spec.name for spec in codecs.client_decodable("mesh")} == {
        "mesh-ply",
        "mesh-draco",
    }


def test_every_geometry_representation_has_a_client_decodable_codec():
    """Otherwise its free camera is a claim with nothing behind it."""
    for member in Representation:
        if member.has_geometry:
            assert codecs.client_decodable(member), member


def test_decodes_must_be_client_or_server():
    with pytest.raises(ValueError, match="decodes must be one of"):
        CodecSpec(
            name="x", suffix=".x", representation=Representation.MESH, decodes="wasm"
        )


# ------------------------------------------------------------- lossiness ---


def test_a_lossy_codec_must_say_what_it_costs():
    """That line reaches the person looking at the render, so it is not optional."""
    with pytest.raises(ValueError, match="must say what it costs"):
        CodecSpec(
            name="x", suffix=".x", representation=Representation.MESH, lossy=True
        )


def test_the_lossy_codecs_all_say_so():
    for spec in codecs.known():
        if spec.lossy:
            assert spec.cost, spec.name


def test_interchange_formats_are_lossless():
    for name in ("mesh-ply", "points-ply", "3dgs-ply"):
        assert codecs.by_name(name).lossy is False


def test_the_compressed_ones_are_lossy():
    for name in ("mesh-draco", "splat", "rerf"):
        assert codecs.by_name(name).lossy is True


# ------------------------------------------------------------ media types ---


def test_media_types_merge_across_representations():
    merged = codecs.media_types()
    assert merged[".ply"] == "application/octet-stream"
    assert merged[".drc"] == "application/octet-stream"
    assert merged[".jpg"] == "image/jpeg"
    assert merged[".png"] == "image/png"


def test_media_types_refuses_a_suffix_with_two_types(monkeypatch):
    """Otherwise a frame's Content-Type would depend on registration order."""
    monkeypatch.setattr(codecs, "_REGISTRY", dict(codecs._REGISTRY))
    codecs.register(
        CodecSpec(
            name="odd",
            suffix=".ply",
            representation=Representation.PIXELS,
            media_type="text/plain",
        )
    )
    with pytest.raises(ValueError, match="one Content-Type"):
        codecs.media_types()


# ---------------------------------------------------------- registration ---


def test_register_refuses_to_shadow_silently():
    with pytest.raises(ValueError, match="already registered as"):
        codecs.register(
            CodecSpec(
                name="other-draco", suffix=".drc", representation=Representation.MESH
            )
        )


def test_register_can_replace_deliberately(monkeypatch):
    monkeypatch.setattr(codecs, "_REGISTRY", dict(codecs._REGISTRY))
    replacement = CodecSpec(
        name="mesh-draco-v2", suffix=".drc", representation=Representation.MESH
    )
    assert codecs.register(replacement, replace=True) is replacement
    assert codecs.for_frame("mesh", ".drc").name == "mesh-draco-v2"


def test_adding_a_codec_is_one_registration(monkeypatch):
    """The whole point. A new wire format touches this and a parser, nothing else."""
    monkeypatch.setattr(codecs, "_REGISTRY", dict(codecs._REGISTRY))
    codecs.register(
        CodecSpec(
            name="mesh-vdmc",
            suffix=".v4d",
            representation=Representation.MESH,
            lossy=True,
            cost="V-DMC is lossy at any rate worth using",
        )
    )
    assert codecs.for_frame("mesh", ".v4d").name == "mesh-vdmc"
    assert ".v4d" in codecs.media_types()
    assert "mesh-vdmc" in {spec.name for spec in codecs.client_decodable("mesh")}


def test_suffixes_must_be_lowercase_and_dotted():
    for bad in ("ply", ".PLY"):
        with pytest.raises(ValueError, match="lowercase and dotted"):
            CodecSpec(name="x", suffix=bad, representation=Representation.MESH)


def test_representation_must_come_from_core():
    with pytest.raises(TypeError, match="open4d.core.Representation"):
        CodecSpec(name="x", suffix=".x", representation="mesh")


def test_known_is_ordered_by_representation_then_suffix():
    order = [(spec.representation.value, spec.suffix) for spec in codecs.known()]
    assert order == sorted(
        order, key=lambda pair: (list(Representation).index(
            Representation(pair[0])), pair[1])
    )
