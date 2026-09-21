"""What the streamer needs to know about a representation, and nothing else.

The point of this module is that adding a representation should not require
editing the server, the manifest writer, or any exporter. Before it existed the
server carried a hardcoded suffix-to-MIME table and the client a hardcoded pair
of clip kinds, so a new representation meant touching unrelated files and
discovering the omissions at runtime -- a ``.obj`` arriving as ``text/html``,
say, which fails as a parse error rather than as a missing registration.

A spec is deliberately small, and got smaller when `streamer.codecs` arrived.
It answers one question -- whether the bundled client can *render* this
representation today -- and derives the rest:

* ``has_geometry``, whether a free camera is meaningful, is
  `open4d.core.Representation`'s to answer and is read from there rather than
  copied, because a second copy is a second opinion.
* ``media_types`` used to be listed here per representation, which duplicated
  what the codec registry knows and let the two disagree. It is now derived
  from `streamer.codecs`: a suffix is a property of a codec, not of a
  representation, and the same ``.ply`` is three different codecs depending on
  which representation is asking.

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
    """What the client can do with one representation."""

    representation: Representation
    #: Whether the client packaged in `streamer.client` can render it today.
    playable: bool = True

    def __post_init__(self) -> None:
        if not isinstance(self.representation, Representation):
            raise TypeError("representation must be an open4d.core.Representation")
        if not isinstance(self.playable, bool):
            raise TypeError("playable must be bool")

    @property
    def name(self) -> str:
        """The wire value, as it appears in ``view.json`` and in URLs."""
        return self.representation.value

    @property
    def has_geometry(self) -> bool:
        """Read from core, never stored: one definition, one answer."""
        return self.representation.has_geometry

    @property
    def media_types(self) -> Mapping[str, str]:
        """Suffix -> ``Content-Type``, from the codecs that produce this.

        Derived rather than declared: a suffix belongs to a codec. Listing them
        here as well is how the registry and the server came to disagree about
        what a ``.drc`` was.
        """
        from . import codecs

        return MappingProxyType({
            spec.suffix: spec.media_type
            for spec in codecs.for_representation(self.representation)
        })

    @property
    def codecs(self) -> tuple:
        """Every codec producing this representation; see `streamer.codecs`."""
        from . import codecs as registry

        return registry.for_representation(self.representation)


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
    """Every suffix any codec produces, for a server's extension map.

    Kept here as well as in `streamer.codecs` because this is where the server
    has always asked; it is now a delegation rather than a second list.
    """
    from . import codecs

    return codecs.media_types()


# ------------------------------------------------------------- the defaults ---
# One line each, now that suffixes live with the codecs that produce them. What
# is left is the single claim this module makes: the packaged client can render
# all four.

register(RepresentationSpec(representation=Representation.GAUSSIANS))
register(RepresentationSpec(representation=Representation.PIXELS))
register(RepresentationSpec(representation=Representation.MESH))
register(RepresentationSpec(representation=Representation.POINTS))
