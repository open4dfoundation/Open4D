"""The registry is the pluggable seam, so its contract is worth pinning down."""

from __future__ import annotations

import pytest
from open4d.core import Representation

from streamer import representations
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
        RepresentationSpec(
            representation=Representation.MESH,
            media_types={".ply": "application/octet-stream"},
            playable=False,
        ),
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


def test_media_types_merge_across_representations():
    merged = representations.media_types()
    # `.ply` is registered by three representations; they agree, so it survives.
    assert merged[".ply"] == "application/octet-stream"
    assert merged[".jpg"] == "image/jpeg"
    assert merged[".obj"] == "model/obj"


def test_media_types_refuses_a_suffix_with_two_types(monkeypatch):
    """Otherwise a frame's Content-Type would depend on registration order."""
    conflicting = RepresentationSpec(
        representation=Representation.POINTS,
        media_types={".ply": "text/plain"},
    )
    monkeypatch.setitem(
        representations._REGISTRY, Representation.POINTS, conflicting
    )
    with pytest.raises(ValueError, match="one Content-Type"):
        representations.media_types()


def test_register_refuses_to_shadow_silently():
    with pytest.raises(ValueError, match="already registered"):
        representations.register(
            RepresentationSpec(
                representation=Representation.GAUSSIANS,
                media_types={".ply": "application/octet-stream"},
            )
        )


def test_register_can_replace_deliberately(monkeypatch):
    monkeypatch.setattr(
        representations, "_REGISTRY", dict(representations._REGISTRY)
    )
    replacement = RepresentationSpec(
        representation=Representation.POINTS,
        media_types={".ply": "application/octet-stream"},
        playable=True,
    )
    assert representations.register(replacement, replace=True) is replacement
    assert representations.spec("points").playable is True


def test_a_new_representation_needs_no_edit_to_the_server(monkeypatch):
    """The whole point: registering is enough for frames to be served correctly."""
    monkeypatch.setattr(
        representations, "_REGISTRY", dict(representations._REGISTRY)
    )
    representations.register(
        RepresentationSpec(
            representation=Representation.MESH,
            media_types={".drc": "application/octet-stream"},
            playable=False,
        ),
        replace=True,
    )
    assert ".drc" in representations.media_types()


# ------------------------------------------------------------- validation ---


def test_spec_requires_a_core_representation():
    with pytest.raises(TypeError, match="open4d.core.Representation"):
        RepresentationSpec(representation="gaussians", media_types={".ply": "x"})


def test_spec_requires_suffixes_to_look_like_suffixes():
    with pytest.raises(ValueError, match="must start with a dot"):
        RepresentationSpec(
            representation=Representation.MESH, media_types={"ply": "x"}
        )


def test_spec_requires_at_least_one_media_type():
    with pytest.raises(ValueError, match="non-empty mapping"):
        RepresentationSpec(representation=Representation.MESH, media_types={})


def test_suffixes_are_lowercased():
    spec = RepresentationSpec(
        representation=Representation.MESH, media_types={".PLY": "application/x"}
    )
    assert ".ply" in spec.media_types


def test_spec_is_immutable():
    spec = representations.spec("pixels")
    with pytest.raises(Exception):
        spec.playable = False
    with pytest.raises(TypeError):
        spec.media_types[".bmp"] = "image/bmp"
