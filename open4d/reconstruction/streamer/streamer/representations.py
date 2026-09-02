"""What the streamer needs to know about a representation, and nothing else.

The point of this module is that adding a representation should not require
editing the server, the manifest writer, or any exporter. Before it existed the
server carried a hardcoded suffix-to-MIME table and the client a hardcoded pair
of clip kinds, so a new representation meant touching unrelated files and
discovering the omissions at runtime -- a ``.obj`` arriving as ``text/html``,
say, which fails as a parse error rather than as a missing registration.

A spec is deliberately small. It answers only the questions transport actually
has:

* which file suffixes are frames of this representation, and what
  ``Content-Type`` each must be served as;
* whether the bundled client can render it *today*.

Everything else about a representation lives elsewhere on purpose.
``has_geometry`` -- whether a free camera is meaningful -- is
`open4d.core.Representation`'s to answer and is read from there rather than
copied, because a second copy is a second opinion. How a frame is *decoded* is
the client's business, and for some representations cannot be the streamer's at
all: ReRF's entropy coder exists only as a CPython 3.8 binary, which is why
``pixels`` is a first-class representation rather than a fallback.

``playable`` is a property of this repository's client, not of the
representation: a bundle declaring something the client cannot draw is reported
as unplayable rather than shown as an empty pane. Every representation core
defines is playable today, so nothing sets it False -- it stays because the next
representation will arrive before its renderer does, and that gap should be
stated rather than discovered.
"""

from __future__ import annotations

from dataclasses import dataclass
from types import MappingProxyType
from typing import Mapping

from open4d.core import Representation

_OCTET = "application/octet-stream"


@dataclass(frozen=True)
class RepresentationSpec:
    """Transport-level facts about one representation."""

    representation: Representation
    #: Frame-file suffix (lowercase, with the dot) -> ``Content-Type``.
    media_types: Mapping[str, str]
    #: Whether the client packaged in `streamer.client` can render it today.
    playable: bool = True

    def __post_init__(self) -> None:
        if not isinstance(self.representation, Representation):
            raise TypeError("representation must be an open4d.core.Representation")
        if not isinstance(self.media_types, Mapping) or not self.media_types:
            raise ValueError("media_types must be a non-empty mapping")
        cleaned: dict[str, str] = {}
        for suffix, media_type in self.media_types.items():
            if not isinstance(suffix, str) or not suffix.startswith("."):
                raise ValueError(f"suffix {suffix!r} must start with a dot")
            if not isinstance(media_type, str) or not media_type:
                raise ValueError(f"media type for {suffix!r} must be a non-empty string")
            cleaned[suffix.lower()] = media_type
        if not isinstance(self.playable, bool):
            raise TypeError("playable must be bool")
        object.__setattr__(self, "media_types", MappingProxyType(cleaned))

    @property
    def name(self) -> str:
        """The wire value, as it appears in ``view.json`` and in URLs."""
        return self.representation.value

    @property
    def has_geometry(self) -> bool:
        """Read from core, never stored: one definition, one answer."""
        return self.representation.has_geometry


_REGISTRY: dict[Representation, RepresentationSpec] = {}


def register(spec: RepresentationSpec, *, replace: bool = False) -> RepresentationSpec:
    """Add ``spec`` to the registry and return it.

    Refuses to shadow an existing registration unless asked, because two specs
    for one representation is the failure this registry exists to prevent -- and
    a silent overwrite would make which one wins depend on import order.
    """
    if not isinstance(spec, RepresentationSpec):
        raise TypeError("spec must be a RepresentationSpec")
    if spec.representation in _REGISTRY and not replace:
        raise ValueError(
            f"{spec.name!r} is already registered; pass replace=True to override"
        )
    _REGISTRY[spec.representation] = spec
    return spec


def spec(representation: Representation | str) -> RepresentationSpec:
    """The spec for ``representation``, by enum member or by wire value."""
    key = (
        representation
        if isinstance(representation, Representation)
        else Representation(representation)
    )
    try:
        return _REGISTRY[key]
    except KeyError:
        raise KeyError(
            f"{key.value!r} has no RepresentationSpec; register one with "
            "streamer.representations.register"
        ) from None


def known() -> tuple[RepresentationSpec, ...]:
    """Every registered spec, in the order core declares the representations."""
    return tuple(
        _REGISTRY[member] for member in Representation if member in _REGISTRY
    )


def playable() -> tuple[RepresentationSpec, ...]:
    """The specs the packaged client can actually render."""
    return tuple(item for item in known() if item.playable)


def media_types() -> dict[str, str]:
    """Every registered suffix, merged, for a server's extension map.

    A suffix shared by several representations -- ``.ply`` is a mesh, a point
    cloud and a Gaussian cloud -- is fine as long as they agree on the type,
    which is checked rather than assumed: disagreement here would mean a frame's
    ``Content-Type`` depended on registration order.
    """
    merged: dict[str, str] = {}
    for item in known():
        for suffix, media_type in item.media_types.items():
            existing = merged.get(suffix)
            if existing is not None and existing != media_type:
                raise ValueError(
                    f"{suffix!r} is registered as both {existing!r} and "
                    f"{media_type!r}; a suffix must have one Content-Type"
                )
            merged[suffix] = media_type
    return merged


# ------------------------------------------------------------- the defaults ---
# Registered here rather than by the modules that produce them, so that a server
# started against a bundle it did not write still knows how to send its frames.

register(
    RepresentationSpec(
        representation=Representation.GAUSSIANS,
        # `.splat` is the quantised form; the client parses `.ply` today.
        media_types={".ply": _OCTET, ".splat": _OCTET},
    )
)
register(
    RepresentationSpec(
        representation=Representation.PIXELS,
        media_types={".jpg": "image/jpeg", ".jpeg": "image/jpeg", ".png": "image/png"},
    )
)
register(
    RepresentationSpec(
        representation=Representation.MESH,
        # `.ply` is the interchange form and `.drc` the compressed one the
        # client also decodes -- 12.9x smaller on this repository's mesh
        # sequence. `.obj` and `.glb` are registered so a bundle carrying them
        # is served with a sensible type rather than as markup, which is a
        # separate question from whether anything can render them.
        media_types={
            ".ply": _OCTET,
            ".drc": _OCTET,
            ".obj": "model/obj",
            ".glb": "model/gltf-binary",
        },
    )
)
register(
    RepresentationSpec(
        representation=Representation.POINTS,
        media_types={".ply": _OCTET, ".drc": _OCTET},
    )
)
