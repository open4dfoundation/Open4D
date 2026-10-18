"""Attribute storage contracts; runtime evidence is specific to each profile."""

from dataclasses import dataclass


@dataclass(frozen=True)
class AttributeSupport:
    name: str
    domains: tuple[str, ...]
    storage: str
    fidelity: str
    verification: str


def attribute_support(codec):
    identifier = codec.id
    if getattr(codec, "representation", None) == "plugin":
        return ()
    if identifier in ("npz", "raw", "deflate", "bzip2", "lzma", "rle"):
        return tuple(AttributeSupport(name, domains, "array payload", "exact canonical values", "array roundtrip")
                     for name, domains in (("colors", ("vertex",)), ("normals", ("vertex",)),
                        ("texture_coordinates", ("vertex", "corner")),
                        ("attributes", ("vertex", "face", "corner")), ("texture", ("image", "face"))))
    if identifier == "draco":
        return (AttributeSupport("colors", ("vertex",), "native Draco", "8-bit RGB/RGBA", "native roundtrip"),
                AttributeSupport("normals", ("vertex",), "native Draco", "native quantization", "native roundtrip"),
                AttributeSupport("texture_coordinates", ("vertex", "corner"), "native Draco", "native quantization; seam splitting", "native roundtrip"),
                AttributeSupport("attributes", ("vertex", "face", "corner"), "native named Draco attributes", "canonical float32/int32/uint32/bool; up to four components", "native roundtrip"),
                AttributeSupport("texture", ("image", "face"), "auxiliary lossless image arrays and native material indices", "exact pixels", "native and auxiliary roundtrip"))
    if identifier in ("vdmc", "faster_vdmc"):
        return (AttributeSupport("texture_coordinates", ("vertex", "corner"),
                    "native V-DMC profile /2", "lossy remeshing and UV reparameterization", "native texture content roundtrip"),
                AttributeSupport("texture", ("image",), "native texture-video bitstream",
                    "one uint8 sRGB image; lossy HM/VTM coding", "native texture content roundtrip"),
                *(AttributeSupport(name, (), "unsupported", "rejected", "unverified")
                  for name in ("colors", "normals", "attributes")))
    preserves = getattr(codec, "preserves", ())
    return tuple(AttributeSupport(name, (), "unsupported" if name not in preserves else "backend specific",
                                  "rejected" if name not in preserves else "unspecified", "unverified")
                 for name in ("colors", "normals", "texture_coordinates", "attributes", "texture"))
