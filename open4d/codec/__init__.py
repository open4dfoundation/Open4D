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
from ._vmesh import VMeshCodec
from ._tracked import TrackedMeshCodec
from ._v3c import inspect_vmesh, pack_vmesh, unpack_vmesh

__all__ = [
    "Codec",
    "CodecError",
    "CodecInfo",
    "KLTCodec",
    "N4MCCodec",
    "QNDFCodec",
    "VMeshCodec",
    "TrackedMeshCodec",
    "available_codecs",
    "decode_sequence",
    "encode_sequence",
    "register_codec",
    "inspect_vmesh",
    "pack_vmesh",
    "unpack_vmesh",
]
