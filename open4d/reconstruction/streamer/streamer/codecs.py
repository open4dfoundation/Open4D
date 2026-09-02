"""What is on the wire, and who can decode it.

`representations` answers what a *decoded* frame is. This answers the question
one level down, which is the one that decides whether a module can be streamed
at all: in what format do its frames travel, and can the client turn them back
into that representation?

Until this existed the two questions were conflated, and the cost was concrete.
Open4D's codecs produce ``.d4d``, ``.v4d``, ``.q4d`` and six more; the client
could decode ``.ply``, ``.splat``, ``.jpg`` and ``.png``. The two sets did not
intersect at all, and nothing in the code said so — a producer found out by
watching a pane stay blank. A registry that names the wire format and where it
decodes makes that a lookup instead of a discovery.

**The key is (representation, suffix), not suffix.** ``.ply`` is claimed by three
representations here and needs two different parsers: a 3DGS PLY and a mesh PLY
share an extension and nothing else. Keying on the extension alone is what
forced the client to carry a hand-written dispatcher per representation, which
is exactly the per-format branching this registry removes.

``decodes`` is the axis that makes the platform honest about heterogeneous
modules:

``client``
    The browser can turn the bytes back into geometry. This is what buys a free
    camera, and it needs a decoder shipped in `streamer.client`.
``server``
    It cannot, and no amount of work here will change that for some formats —
    ReRF's entropy coder exists only as a CPython 3.8 binary. Those are decoded
    and rendered where the GPU is, and what reaches the client is ``pixels``.
    That is the codec's output representation as the platform sees it, which is
    why ``.rerf`` is registered as producing ``pixels`` rather than a volume it
    never delivers.
"""

from __future__ import annotations

from dataclasses import dataclass

from open4d.core import Representation

_OCTET = "application/octet-stream"

#: Where a codec's frames are turned back into a representation.
DECODERS = ("client", "server")


@dataclass(frozen=True)
class CodecSpec:
    """One wire format, and what the platform can do with it."""

    #: Stable name, used in a clip's ``detail`` and in error messages.
    name: str
    #: Frame-file suffix, lowercase, with the dot.
    suffix: str
    #: What a decoded frame of this codec is.
    representation: Representation
    #: ``Content-Type`` a server must send it as.
    media_type: str = _OCTET
    #: ``"client"`` or ``"server"``; see the module docstring.
    decodes: str = "client"
    #: Whether decoding loses anything relative to what was encoded.
    lossy: bool = False
    #: What it costs, in one line, for a human reading a table of these. Empty
    #: for a lossless interchange format, where there is nothing to warn about.
    cost: str = ""

    def __post_init__(self) -> None:
        if not isinstance(self.representation, Representation):
            raise TypeError("representation must be an open4d.core.Representation")
        if not self.suffix.startswith(".") or self.suffix != self.suffix.lower():
            raise ValueError(f"suffix {self.suffix!r} must be lowercase and dotted")
        if self.decodes not in DECODERS:
            raise ValueError(
                f"decodes must be one of {', '.join(DECODERS)}; got {self.decodes!r}"
            )
        if self.lossy and not self.cost:
            raise ValueError(
                f"{self.name}: a lossy codec must say what it costs — that line is "
                "what reaches the person looking at the render"
            )

    @property
    def key(self) -> tuple[Representation, str]:
        return (self.representation, self.suffix)


_REGISTRY: dict[tuple[Representation, str], CodecSpec] = {}


def register(spec: CodecSpec, *, replace: bool = False) -> CodecSpec:
    """Add ``spec`` to the registry and return it."""
    if not isinstance(spec, CodecSpec):
        raise TypeError("spec must be a CodecSpec")
    if spec.key in _REGISTRY and not replace:
        existing = _REGISTRY[spec.key]
        raise ValueError(
            f"{spec.representation.value} {spec.suffix} is already registered as "
            f"{existing.name!r}; pass replace=True to override"
        )
    _REGISTRY[spec.key] = spec
    return spec


def known() -> tuple[CodecSpec, ...]:
    """Every registered codec, ordered by representation then suffix."""
    return tuple(
        _REGISTRY[key]
        for key in sorted(
            _REGISTRY, key=lambda k: (list(Representation).index(k[0]), k[1])
        )
    )


def for_frame(representation: Representation | str, suffix: str) -> CodecSpec:
    """The codec a frame of this representation and suffix is in."""
    key = (
        representation
        if isinstance(representation, Representation)
        else Representation(representation)
    )
    try:
        return _REGISTRY[(key, suffix.lower())]
    except KeyError:
        offered = ", ".join(
            spec.suffix for spec in known() if spec.representation is key
        )
        raise KeyError(
            f"no codec for a {key.value} frame ending {suffix!r}"
            + (f"; registered: {offered}" if offered else "")
        ) from None


def by_name(name: str) -> CodecSpec:
    """A codec by its stable name."""
    for spec in known():
        if spec.name == name:
            return spec
    raise KeyError(f"no codec named {name!r}")


def for_representation(representation: Representation | str) -> tuple[CodecSpec, ...]:
    """Every codec that produces this representation."""
    key = (
        representation
        if isinstance(representation, Representation)
        else Representation(representation)
    )
    return tuple(spec for spec in known() if spec.representation is key)


def client_decodable(
    representation: Representation | str | None = None,
) -> tuple[CodecSpec, ...]:
    """Codecs a browser can decode, optionally for one representation.

    The answer to "can I stream this module to a browser at all", which used to
    require reading the client's source.
    """
    found = (
        known()
        if representation is None
        else for_representation(representation)
    )
    return tuple(spec for spec in found if spec.decodes == "client")


def media_types() -> dict[str, str]:
    """Every registered suffix, merged, for a server's extension map.

    A suffix shared between representations is fine as long as they agree on the
    type, which is checked rather than assumed: disagreement would make a
    frame's ``Content-Type`` depend on registration order.
    """
    merged: dict[str, str] = {}
    for spec in known():
        existing = merged.get(spec.suffix)
        if existing is not None and existing != spec.media_type:
            raise ValueError(
                f"{spec.suffix!r} is registered as both {existing!r} and "
                f"{spec.media_type!r}; a suffix must have one Content-Type"
            )
        merged[spec.suffix] = spec.media_type
    return merged


# ------------------------------------------------------------- the defaults ---
# Registered here rather than by the modules that produce them, so a server
# started against a bundle it did not write still knows how to send its frames
# and a reader can see the whole picture in one place.

register(CodecSpec(
    name="mesh-ply",
    suffix=".ply",
    representation=Representation.MESH,
    # Interchange, not delivery: readable by anything, 13x larger than Draco.
))
register(CodecSpec(
    name="mesh-draco",
    suffix=".drc",
    representation=Representation.MESH,
    lossy=True,
    cost="positions quantised (0.005% of the diagonal at 14 bits) and duplicate "
         "vertices merged",
))
register(CodecSpec(
    name="points-ply",
    suffix=".ply",
    representation=Representation.POINTS,
))
register(CodecSpec(
    name="points-draco",
    suffix=".drc",
    representation=Representation.POINTS,
    lossy=True,
    cost="positions quantised and duplicate points merged",
))
register(CodecSpec(
    name="3dgs-ply",
    suffix=".ply",
    representation=Representation.GAUSSIANS,
    # The format every splat tool reads, and the reason a Vega export opens in
    # SuperSplat or SIBR. Stores raw training parameters at float32.
))
register(CodecSpec(
    name="splat",
    suffix=".splat",
    representation=Representation.GAUSSIANS,
    lossy=True,
    cost="every spherical-harmonic band above degree 0 dropped, so appearance "
         "stops changing with view direction; colour, opacity and rotation "
         "quantised to 8 bits",
))
register(CodecSpec(
    name="jpeg",
    suffix=".jpg",
    representation=Representation.PIXELS,
    media_type="image/jpeg",
    lossy=True,
    cost="lossy image compression, and a fixed viewpoint: pixels carry no "
         "geometry, so there is no free camera",
))
register(CodecSpec(
    name="jpeg",
    suffix=".jpeg",
    representation=Representation.PIXELS,
    media_type="image/jpeg",
    lossy=True,
    cost="lossy image compression, and a fixed viewpoint",
))
register(CodecSpec(
    name="png",
    suffix=".png",
    representation=Representation.PIXELS,
    media_type="image/png",
    cost="a fixed viewpoint: pixels carry no geometry",
))
register(CodecSpec(
    name="rerf",
    suffix=".rerf",
    representation=Representation.PIXELS,
    decodes="server",
    lossy=True,
    cost="DCT and arithmetic coded, and undecodable in a browser at all — its "
         "entropy coder ships only as a CPython 3.8 binary, so it is rendered "
         "where the GPU is and arrives as pixels at a fixed viewpoint",
))
