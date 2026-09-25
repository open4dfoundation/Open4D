"""Public codec adapters for native temporal neural research artifacts."""
from pathlib import Path
import os

from ._native_profiles import PROFILES
from ._protocol import CodecError
from ._v3c import probe_codec


def encode_native(source, destination, *, codec, overwrite=False, **options):
    from open4d.native import NativeSequence, import_native, save_native
    if isinstance(source, NativeSequence):
        if options:
            raise TypeError("native repacking does not accept training/import options")
        if source.codec != codec:
            raise CodecError(f"native source contains {source.codec}, not {codec}")
        return save_native(source, destination, overwrite=overwrite)
    if isinstance(source, (str, os.PathLike)) and Path(source).suffix.lower() in (".usd", ".usdc", ".usda", ".vmesh"):
        from open4d import load
        if options:
            raise TypeError("native repacking does not accept training/import options")
        with load(source) as opened:
            if not isinstance(opened, NativeSequence):
                raise TypeError(f"{codec} needs a native temporal USD representation; generic mesh USD is not an encoder input")
            return encode_native(opened, destination, codec=codec, overwrite=overwrite)
    with import_native(source, codec=codec, **options) as imported:
        return save_native(imported, destination, overwrite=overwrite)


class NativeTemporalCodec:
    suffixes = (".vmesh",)
    backend = "research-subprocess"
    lossless = False
    preserves = ("native_temporal_representation", "timestamps", "metadata")

    def __init__(self, identifier):
        self.id = identifier
        self.representation = PROFILES[identifier][0]

    def can_decode(self, path):
        return probe_codec(path) == self.id

    def encode(self, sequence, destination, **options):
        return encode_native(sequence, destination, codec=self.id, **options)

    def decode(self, source, *, runtime=None, python=None):
        from open4d.native import NativeSequence
        result = NativeSequence(source, runtime=runtime, python=python)
        if result.codec != self.id:
            result.close()
            raise CodecError(f"native stream does not contain {self.id}")
        return result


QUEEN_CODEC = NativeTemporalCodec("queen")
GSTREAM_CODEC = NativeTemporalCodec("3dgstream")
RERF_CODEC = NativeTemporalCodec("rerf")
