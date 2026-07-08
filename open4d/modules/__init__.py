"""Open4D algorithm modules.

Open4D's compression pipelines exposed as uniform *codecs*. The primary entry
point is :func:`get_codec`::

    from open4d.modules import get_codec, list_codecs

    list_codecs()                       # ['n4mc', 'tsmc', 'tvmc']
    codec = get_codec("n4mc")
    print(codec.available())            # environment capability probe
    result = codec.compress(mesh_seq)   # runs the real pipeline, times stages

All codecs share the :class:`CompressionResult` contract (per-stage timings,
artifact paths, metrics). The runnable Python API lives in the ``pipelines``
subpackage; this package's ``tvmc/`` and ``unity_decoder/`` directories are the
raw pipeline source trees, not the API.

Importing this package is cheap — it pulls only numpy/stdlib. The heavy
dependencies (torch, open3d, dotnet, Draco) are touched only when a codec's
``available()`` or ``compress()`` actually runs.
"""
from .pipelines import (
    Capability,
    Codec,
    CompressionResult,
    N4MCCodec,
    PipelineError,
    StageTiming,
    TSMCCodec,
    TVMCCodec,
    get_codec,
    list_codecs,
)

__all__ = [
    "get_codec",
    "list_codecs",
    "Codec",
    "Capability",
    "CompressionResult",
    "StageTiming",
    "PipelineError",
    "N4MCCodec",
    "TSMCCodec",
    "TVMCCodec",
]
