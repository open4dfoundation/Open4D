"""Exceptions raised by Open4D's public I/O API."""


class Open4DError(Exception):
    """Base class for public Open4D errors."""


class SequenceIOError(Open4DError):
    """Base class for sequence loading and inspection errors."""


class SourceNotFoundError(SequenceIOError):
    """The requested sequence source does not exist."""


class UnsupportedFormatError(SequenceIOError):
    """No installed reader supports the requested source format."""


class AmbiguousFormatError(SequenceIOError):
    """More than one reader could own a source."""


class MissingDependencyError(SequenceIOError):
    """A selected reader needs an optional dependency."""


class DecodeError(SequenceIOError):
    """A source was recognized but one of its frames could not be decoded."""


class UnsupportedFeatureError(SequenceIOError):
    """The source is valid but requests an unsupported I/O feature."""
