"""Explicit registrations for adapter contract tests; core stays plugin-free."""

import pytest


@pytest.fixture
def registered_research_codecs(monkeypatch):
    from open4d.codecs import _api
    from open4d.codecs.klt import KLT_CODEC
    from open4d.codecs.n4mc import N4MC_CODEC
    from open4d.codecs.qndf import QNDF_CODEC, QNDF_INT8_CODEC
    from open4d.codecs.vdmc import VDMC_CODEC, FASTER_VDMC_CODEC
    from open4d.codecs.tvmc import TVMC_CODEC, TSMC_CODEC
    from open4d.codecs.queen import QUEEN_CODEC, GSTREAM_CODEC, RERF_CODEC
    from open4d.codecs.vega import VEGA_CODEC

    implementations = (KLT_CODEC, N4MC_CODEC, QNDF_CODEC, QNDF_INT8_CODEC,
                       VDMC_CODEC, FASTER_VDMC_CODEC, TVMC_CODEC, TSMC_CODEC,
                       VEGA_CODEC, QUEEN_CODEC, GSTREAM_CODEC, RERF_CODEC)
    codecs = {**_api._CODECS, **{codec.id: codec for codec in implementations}}
    monkeypatch.setattr(_api, "_CODECS", codecs)
    return codecs


@pytest.fixture
def registered_research_methods(monkeypatch):
    from open4d.reconstruction import _registry
    methods = {**_registry._METHODS,
               "queen": _registry.queen_reconstruction,
               "3dgstream": _registry.gstream_reconstruction}
    monkeypatch.setattr(_registry, "_METHODS", methods)
    return methods
