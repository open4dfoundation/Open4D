"""Modular codecs for finite Open4D triangle-mesh sequences."""

from ._api import (
    CodecInfo,
    available_codecs,
    codec_info,
    decode_sequence,
    encode_sequence,
    register_codec,
)
from .klt import KLTCodec
from .n4mc import N4MCCodec
from ._protocol import Codec, CodecError
from ._attributes import AttributeSupport
from .qndf import QNDFCodec
from .vdmc import O4DCodec
from .tvmc import TrackedMeshCodec
from ._migration import migrate_legacy
from ._o4d_format import inspect_o4d, pack_o4d, unpack_o4d
from ._stream import StreamEncoder, StreamDecoder, stream_profiles, splats_to_frame, frame_to_splats

__all__ = [
    "Codec",
    "CodecError",
    "CodecInfo",
    "AttributeSupport",
    "KLTCodec",
    "N4MCCodec",
    "QNDFCodec",
    "O4DCodec",
    "TrackedMeshCodec",
    "available_codecs",
    "codec_info",
    "decode_sequence",
    "encode_sequence",
    "register_codec",
    "inspect_o4d",
    "pack_o4d",
    "migrate_legacy",
    "unpack_o4d",
    "StreamEncoder", "StreamDecoder", "stream_profiles", "splats_to_frame", "frame_to_splats",
]
