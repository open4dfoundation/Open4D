"""Open4D compression pipelines — uniform Python entry points.

Each Open4D algorithm is a :class:`~open4d.modules.pipelines.base.Codec`:

    from open4d.modules import get_codec
    codec = get_codec("n4mc")           # "tsmc" | "tvmc"
    print(codec.available())            # environment probe
    result = codec.compress(source)     # runs the real pipeline

or reach a codec class directly::

    from open4d.modules.pipelines import N4MCCodec
"""
from .base import (
    Capability,
    Codec,
    CompressionResult,
    PipelineError,
    StageRunner,
    StageTiming,
)
from .n4mc import N4MCCodec
from .tsmc import TSMCCodec
from .tvmc import TVMCCodec

_REGISTRY = {
    "n4mc": N4MCCodec,
    "tsmc": TSMCCodec,
    "tvmc": TVMCCodec,
}

__all__ = [
    "Capability",
    "Codec",
    "CompressionResult",
    "PipelineError",
    "StageRunner",
    "StageTiming",
    "N4MCCodec",
    "TSMCCodec",
    "TVMCCodec",
    "get_codec",
    "list_codecs",
]


def list_codecs():
    """Return the sorted list of registered codec names."""
    return sorted(_REGISTRY)


def get_codec(name: str) -> Codec:
    """Instantiate a codec by name (``"n4mc"`` | ``"tsmc"`` | ``"tvmc"``)."""
    key = name.lower()
    if key not in _REGISTRY:
        raise KeyError(f"unknown codec {name!r}; available: {list_codecs()}")
    return _REGISTRY[key]()
