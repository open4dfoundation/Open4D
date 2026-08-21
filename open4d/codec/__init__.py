"""Modular codecs for finite Open4D triangle-mesh sequences."""

from ._api import (
    CodecInfo,
    available_codecs,
    decode_sequence,
    encode_sequence,
    register_codec,
)
from ._draco import DracoCodec
from ._klt import KLTCodec
from ._n4mc import N4MCCodec
from ._npz import NumPyZipCodec
from ._protocol import Codec, CodecError
from ._qndf import QNDFCodec
from ._temporal import TemporalMeshCodec
from ._vmesh import VMeshCodec

__all__ = [
    "Codec",
    "CodecError",
    "CodecInfo",
    "DracoCodec",
    "KLTCodec",
    "N4MCCodec",
    "NumPyZipCodec",
    "QNDFCodec",
    "TemporalMeshCodec",
    "VMeshCodec",
    "available_codecs",
    "decode_sequence",
    "encode_sequence",
    "register_codec",
]
