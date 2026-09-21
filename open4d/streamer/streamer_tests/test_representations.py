"""What the client can render. Suffixes belong to `streamer.codecs`, not here."""

from __future__ import annotations

import pytest
from open4d.core import Representation

from streamer import codecs, representations
from streamer.representations import RepresentationSpec

pytestmark = pytest.mark.cpu


def test_every_core_representation_is_registered():
    """A representation core knows about but transport does not is a gap, not a choice."""
    assert {spec.representation for spec in representations.known()} == set(
        Representation
    )


def test_known_is_ordered_by_cores_declaration():
    assert [spec.name for spec in representations.known()] == [
        member.value for member in Representation
    ]


def test_has_geometry_is_read_from_core_not_stored():
    for spec in representations.known():
        assert spec.has_geometry is spec.representation.has_geometry


def test_everything_core_defines_is_playable_today():
    """The renderer gap is closed; the flag stays for the next representation."""
    assert {spec.name for spec in representations.playable()} == {
        member.value for member in Representation
    }


def test_unplayable_is_still_expressible(monkeypatch):
    """A representation can arrive before its renderer, and must say so."""
    monkeypatch.setattr(representations, "_REGISTRY", dict(representations._REGISTRY))
    representations.register(
        RepresentationSpec(representation=Representation.MESH, playable=False),
        replace=True,
    )
    assert representations.spec("mesh").playable is False
    assert representations.spec("mesh").has_geometry is True
    assert "mesh" not in {spec.name for spec in representations.playable()}


def test_spec_accepts_the_enum_or_the_wire_value():
    assert representations.spec("gaussians") is representations.spec(
        Representation.GAUSSIANS
    )


def test_spec_rejects_an_unknown_name():
    with pytest.raises(ValueError):
        representations.spec("voxels")


# ------------------------------------------------------ derived, not declared ---


def test_media_types_come_from_the_codecs(monkeypatch):
    """They were listed here too, which let the two disagree about a `.drc`."""
    assert dict(representations.spec("mesh").media_types) == {
        spec.suffix: spec.media_type
        for spec in codecs.for_representation(Representation.MESH)
    }


def test_registering_a_codec_changes_what_a_representation_serves(monkeypatch):
    """The point of deriving it: one registration, and the server follows."""
    monkeypatch.setattr(codecs, "_REGISTRY", dict(codecs._REGISTRY))
    codecs.register(
        codecs.CodecSpec(
            name="mesh-vdmc", suffix=".v4d", representation=Representation.MESH
        )
    )
    assert ".v4d" in representations.spec("mesh").media_types
    assert ".v4d" in representations.media_types()


def test_a_representation_exposes_its_codecs():
    names = {spec.name for spec in representations.spec("gaussians").codecs}
    assert names == {"3dgs-ply", "splat"}


def test_media_types_delegates_to_the_codec_registry():
    assert representations.media_types() == codecs.media_types()


# ------------------------------------------------------------- validation ---


def test_spec_requires_a_core_representation():
    with pytest.raises(TypeError, match="open4d.core.Representation"):
        RepresentationSpec(representation="gaussians")


def test_playable_must_be_a_bool():
    with pytest.raises(TypeError, match="playable must be bool"):
        RepresentationSpec(representation=Representation.MESH, playable="yes")


def test_register_refuses_to_shadow_silently():
    with pytest.raises(ValueError, match="already registered"):
        representations.register(
            RepresentationSpec(representation=Representation.GAUSSIANS)
        )


def test_register_can_replace_deliberately(monkeypatch):
    monkeypatch.setattr(representations, "_REGISTRY", dict(representations._REGISTRY))
    replacement = RepresentationSpec(
        representation=Representation.POINTS, playable=False
    )
    assert representations.register(replacement, replace=True) is replacement
    assert representations.spec("points").playable is False


def test_spec_is_immutable():
    with pytest.raises(Exception):
        representations.spec("pixels").playable = False
