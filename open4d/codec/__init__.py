"""Modular codecs for finite Open4D triangle-mesh sequences."""

from ._api import (
    CodecInfo,
    available_codecs,
    decode_sequence,
    encode_sequence,
    register_codec,
)
from ._klt import KLTCodec
from ._n4mc import N4MCCodec
from ._protocol import Codec, CodecError
from ._qndf import QNDFCodec
from ._o4d import O4DCodec
from ._tracked import TrackedMeshCodec
from ._migration import migrate_legacy
from ._o4d_format import inspect_o4d, pack_o4d, unpack_o4d

__all__ = [
    "Codec",
    "CodecError",
    "CodecInfo",
    "KLTCodec",
    "N4MCCodec",
    "QNDFCodec",
    "O4DCodec",
    "TrackedMeshCodec",
    "available_codecs",
    "decode_sequence",
    "encode_sequence",
    "register_codec",
    "inspect_o4d",
    "pack_o4d",
    "migrate_legacy",
    "unpack_o4d",
]
