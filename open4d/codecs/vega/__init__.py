"""Vega Gaussian codec adapter; the research implementation is in ``source/``."""

from __future__ import annotations

import os
from pathlib import Path
import tempfile

from ... import gaussians


class _VegaCodec:
    id = "vega"
    suffixes = (".o4d",)
    representation = "gaussian_splats"
    backend = "research-subprocess"
    lossless = False
    preserves = ("positions", "scales", "rotations", "opacities", "neural_appearance")

    def can_decode(self, source):
        from .._o4d_format import contains_codec
        return contains_codec(source, self.id)

    def encode(self, sequence, destination: Path, **options) -> Path:
        from .._metadata import require_o4d_destination
        from ...native import NativeSequence, import_native, save_native
        from ..queen import encode_native
        destination = require_o4d_destination(destination)
        if isinstance(sequence, (NativeSequence, gaussians.VegaRun, str, os.PathLike)):
            return encode_native(sequence, destination, codec="vega", **options)
        overwrite = options.pop("overwrite", False)
        if destination.exists() and not overwrite:
            raise FileExistsError(destination)
        timeline = {key: options.pop(key) for key in ("fps", "timestamps", "frame_indices", "metadata", "frame_metadata") if key in options}
        with tempfile.TemporaryDirectory(prefix="open4d-vega-encode-") as folder:
            encoded = gaussians.encode_gaussians(sequence, Path(folder) / "native", **options)
            with import_native(encoded, **timeline) as native:
                return save_native(native, destination, overwrite=overwrite)

    def decode(self, source: Path, **options) -> NativeSequence:
        """Open the owned native state; call ``.decode()`` to evaluate frames."""
        from ..queen import NativeTemporalCodec
        return NativeTemporalCodec("vega").decode(source, **options)


VEGA_CODEC = _VegaCodec()
