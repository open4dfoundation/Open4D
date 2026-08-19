"""Load heterogeneous 3D sequence storage through one lazy Python API."""

from ._api import (
    FormatInfo,
    SequenceInfo,
    available_formats,
    inspect_sequence,
    open_sequence,
)
from ._errors import (
    AmbiguousFormatError,
    DecodeError,
    MissingDependencyError,
    Open4DError,
    SequenceIOError,
    SourceNotFoundError,
    UnsupportedFeatureError,
    UnsupportedFormatError,
)

__all__ = [
    "AmbiguousFormatError",
    "DecodeError",
    "FormatInfo",
    "MissingDependencyError",
    "Open4DError",
    "SequenceIOError",
    "SequenceInfo",
    "SourceNotFoundError",
    "UnsupportedFeatureError",
    "UnsupportedFormatError",
    "available_formats",
    "inspect_sequence",
    "open_sequence",
]
