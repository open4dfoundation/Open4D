"""Modular in-process codecs for Open4D sequences."""

from ._api import (
    CodecInfo,
    available_codecs,
    decode_sequence,
    encode_sequence,
    register_codec,
)
from ._draco import DracoCodec
from ._npz import NumPyZipCodec
from ._protocol import Codec, CodecError
from ._vmesh import VMeshCodec

__all__ = [
    "Codec",
    "CodecError",
    "CodecInfo",
    "DracoCodec",
    "NumPyZipCodec",
    "VMeshCodec",
    "available_codecs",
    "decode_sequence",
    "encode_sequence",
    "register_codec",
]
