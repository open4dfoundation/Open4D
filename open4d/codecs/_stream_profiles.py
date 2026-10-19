"""Payload profiles for live O4S2 frame packets.

A profile turns one frame's arrays into a payload and back. Arrays arrive in
stream-schema order, with ``None`` for an absent optional field. Profiles never
see sockets or packet headers; `_stream` owns framing, limits and state.

Error bounds of the lossy profiles, per decoded value:

``draco``
    Positions are quantized by Draco to ``quantization_bits`` over the largest
    side ``E`` of the frame's bounding box: error at most ``E / (2**bits - 1)``
    per coordinate (half that, plus float rounding, in practice). Colors become
    8-bit (error at most 1/510). Point normals and point-aligned attributes are
    carried as unquantized Draco attributes. Mesh normals and texture
    coordinates use Draco's own default quantization. Without
    ``preserve_order`` Draco reorders points and vertices, and merges duplicate
    points, so decoded counts may differ from the input.
``quantized``
    SPZ-style fixed point, then deflate. Positions: 16 bits over each axis of
    the frame's bounding box, error at most ``extent / 131070`` per axis.
    Colors and opacities: 8 bits, error at most 1/510. Rotations and unit
    normals: signed 16-bit components, error at most 1/65534 per stored
    component (GaussianCloud renormalizes decoded quaternions). Scales,
    spherical harmonics, texture coordinates and other float attributes:
    IEEE half precision, relative error at most 2**-11 for magnitudes in
    [2**-14, 65504] and absolute error at most 2**-25 below; a field with
    values outside the half range is sent unquantized. Integer and boolean
    fields, including triangles, are exact.
"""

from __future__ import annotations

from dataclasses import dataclass
from importlib import import_module
import math
import zlib

import numpy as np

_SNORM16 = 32767
_UNIT8 = 255
_BOX16 = 65535
_HALF_MAX = float(np.finfo(np.float16).max)


@dataclass(frozen=True)
class StreamField:
    """One schema entry: a name, a little-endian dtype and a shape.

    The first dimension is None because element counts vary per frame; the
    others are fixed for the stream.
    """

    name: str
    dtype: np.dtype
    shape: tuple

    def describe(self) -> dict:
        return {"name": self.name, "dtype": self.dtype.str, "shape": list(self.shape)}


def _integer(options, name, default, low, high):
    value = options.pop(name, default)
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f"{name} must be an integer in [{low}, {high}]")
    return value


def _boolean(options, name, default):
    value = options.pop(name, default)
    if type(value) is not bool:
        raise ValueError(f"{name} must be a boolean")
    return value


def _finish(options, profile):
    if options:
        raise ValueError(f"unknown {profile} stream option(s): {', '.join(sorted(map(str, options)))}")


def _size(shape, dtype) -> int:
    return math.prod(shape) * np.dtype(dtype).itemsize


def _inflate(payload: bytes, expected: int) -> bytes:
    """Deflate-decode exactly ``expected`` bytes, never allocating more."""
    decoder = zlib.decompressobj()
    try:
        data = decoder.decompress(payload, expected + 1)
    except zlib.error as error:
        raise ValueError(f"invalid compressed payload: {error}") from error
    if len(data) != expected or not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
        raise ValueError("compressed payload does not match its declared shapes")
    return data


def _split(data, fields, shapes, dtypes=None):
    arrays, offset = [], 0
    for index, (field, shape) in enumerate(zip(fields, shapes)):
        if shape is None:
            arrays.append(None)
            continue
        dtype = field.dtype if dtypes is None else dtypes[index]
        size = _size(shape, dtype)
        arrays.append(np.frombuffer(data, dtype=dtype, count=math.prod(shape),
                                    offset=offset).reshape(shape))
        offset += size
    if offset != len(data):
        raise ValueError("payload size does not match its declared shapes")
    return arrays


def _xor(data: bytes, reference: bytes) -> bytes:
    return np.bitwise_xor(np.frombuffer(data, np.uint8),
                          np.frombuffer(reference, np.uint8)).tobytes()


class _ArrayProfile:
    """Little-endian array bytes, optionally deflate/zstd compressed.

    With ``keyframe_interval`` above one, frames between keyframes XOR each
    field's bytes with the previous frame's when the shapes match, which
    deflate/zstd then compress well for slowly moving geometry.
    """

    version = 1
    lossless = True
    exact_shapes = True
    representations = frozenset({"mesh", "points", "gaussians"})

    def __init__(self, identifier, compressor=None):
        self.id = identifier
        self.compressor = compressor
        self.requires = "zstandard" if compressor == "zstd" else None

    @property
    def available(self) -> bool:
        if self.compressor != "zstd":
            return True
        try:
            import_module("zstandard")
        except ImportError:
            return False
        return True

    def _zstd(self):
        try:
            return import_module("zstandard")
        except ImportError as error:
            raise ValueError("the zstd stream profile needs the optional 'zstandard' package") from error

    def options(self, options: dict) -> dict:
        options = dict(options)
        result = {}
        if self.compressor == "zlib":
            result["level"] = _integer(options, "level", 6, 0, 9)
        elif self.compressor == "zstd":
            result["level"] = _integer(options, "level", 3, 1, 22)
        if self.compressor is not None:
            result["keyframe_interval"] = _integer(options, "keyframe_interval", 1, 1, 1 << 20)
        _finish(options, self.id)
        return result

    def independent(self, options) -> bool:
        return options.get("keyframe_interval", 1) == 1

    def encode(self, fields, arrays, reference, options):
        parts, delta = [], []
        for index, array in enumerate(arrays):
            if array is None:
                delta.append(False)
                continue
            data = array.tobytes()
            use = (reference is not None and reference[index] is not None
                   and len(reference[index]) == len(data) and len(data) > 0)
            parts.append(_xor(data, reference[index]) if use else data)
            delta.append(use)
        raw = b"".join(parts)
        if self.compressor == "zlib":
            raw = zlib.compress(raw, options["level"])
        elif self.compressor == "zstd":
            raw = self._zstd().ZstdCompressor(level=options["level"]).compress(raw)
        return raw, ({"delta": delta} if reference is not None else {})

    def _decompress(self, payload, expected):
        if self.compressor is None:
            if len(payload) != expected:
                raise ValueError("payload size does not match its declared shapes")
            return payload
        if self.compressor == "zlib":
            return _inflate(payload, expected)
        zstandard = self._zstd()
        try:
            if zstandard.frame_content_size(payload) != expected:
                raise ValueError("compressed payload does not match its declared shapes")
            data = zstandard.ZstdDecompressor().decompress(payload, max_output_size=expected)
        except zstandard.ZstdError as error:
            raise ValueError(f"invalid compressed payload: {error}") from error
        if len(data) != expected:
            raise ValueError("compressed payload does not match its declared shapes")
        return data

    def decode(self, payload, info, fields, shapes, reference, options):
        expected = sum(_size(shape, field.dtype)
                       for field, shape in zip(fields, shapes) if shape is not None)
        if reference is None:
            if info != {}:
                raise ValueError("keyframe codec information must be empty")
            delta = [False] * len(fields)
        else:
            delta = info.get("delta") if set(info) == {"delta"} else None
            if (not isinstance(delta, list) or len(delta) != len(fields)
                    or any(type(flag) is not bool for flag in delta)):
                raise ValueError("delta frame needs one boolean per field")
        arrays = _split(self._decompress(payload, expected), fields, shapes)
        for index, use in enumerate(delta):
            if not use:
                continue
            if (arrays[index] is None or reference[index] is None
                    or len(reference[index]) != arrays[index].nbytes):
                raise ValueError("delta field has no matching reference")
            data = _xor(arrays[index].tobytes(), reference[index])
            arrays[index] = np.frombuffer(data, dtype=fields[index].dtype).reshape(shapes[index])
        return arrays


class _QuantizedProfile:
    """Fixed-point and half-precision fields, then deflate; see module notes."""

    id = "quantized"
    version = 1
    lossless = False
    exact_shapes = True
    available = True
    requires = None
    representations = frozenset({"mesh", "points", "gaussians"})
    _ENCODED = {"box16": np.dtype("<u2"), "unit8": np.dtype("u1"),
                "snorm16": np.dtype("<i2"), "f16": np.dtype("<f2")}

    def options(self, options: dict) -> dict:
        options = dict(options)
        result = {"level": _integer(options, "level", 6, 0, 9)}
        _finish(options, self.id)
        return result

    def independent(self, options) -> bool:
        return True

    @staticmethod
    def _half(array):
        if array.size and float(np.abs(array).max()) > _HALF_MAX:
            return {"kind": "raw"}, array
        return {"kind": "f16"}, array.astype("<f2")

    def _quantize(self, field, array):
        if array.dtype.kind != "f" or array.size == 0:
            return {"kind": "raw"}, array
        name = field.name
        if name == "positions":
            flat = array.reshape(len(array), -1).astype(np.float64)
            low, high = flat.min(axis=0), flat.max(axis=0)
            extent = np.where(high > low, high - low, 1.0)
            values = np.rint((flat - low) / extent * _BOX16).astype("<u2")
            return {"kind": "box16", "low": low.tolist(), "high": high.tolist()}, values
        if name in ("colors", "opacities") and array.min() >= 0 and array.max() <= 1:
            return {"kind": "unit8"}, np.rint(array * _UNIT8).astype("u1")
        if name in ("rotations", "normals") and float(np.abs(array).max()) <= 1:
            return {"kind": "snorm16"}, np.rint(array * _SNORM16).astype("<i2")
        return self._half(array)

    def encode(self, fields, arrays, reference, options):
        encodings, parts = [], []
        for field, array in zip(fields, arrays):
            if array is None:
                encodings.append(None)
                continue
            encoding, values = self._quantize(field, array)
            encodings.append(encoding)
            parts.append(np.ascontiguousarray(values).tobytes())
        return zlib.compress(b"".join(parts), options["level"]), {"fields": encodings}

    def decode(self, payload, info, fields, shapes, reference, options):
        encodings = info.get("fields") if set(info) == {"fields"} else None
        if not isinstance(encodings, list) or len(encodings) != len(fields):
            raise ValueError("quantized frame needs one encoding per field")
        dtypes = []
        for field, shape, encoding in zip(fields, shapes, encodings):
            if shape is None or encoding is None:
                if (shape is None) != (encoding is None):
                    raise ValueError("quantized encoding disagrees with field presence")
                dtypes.append(None)
                continue
            kind = encoding.get("kind") if isinstance(encoding, dict) else None
            if kind == "raw" and set(encoding) == {"kind"}:
                dtypes.append(field.dtype)
                continue
            if kind not in self._ENCODED or field.dtype.kind != "f":
                raise ValueError("invalid quantized field encoding")
            components = math.prod(shape[1:])
            if kind == "box16":
                low, high = encoding.get("low"), encoding.get("high")
                if (set(encoding) != {"kind", "low", "high"}
                        or not all(isinstance(bound, list) and len(bound) == components
                                   and all(type(value) in (int, float) and math.isfinite(value)
                                           for value in bound) for bound in (low, high))
                        or any(a > b for a, b in zip(low, high))):
                    raise ValueError("invalid quantized position bounds")
            elif set(encoding) != {"kind"}:
                raise ValueError("invalid quantized field encoding")
            dtypes.append(self._ENCODED[kind])
        expected = sum(_size(shape, dtype) for shape, dtype in zip(shapes, dtypes)
                       if shape is not None)
        stored = _split(_inflate(payload, expected), fields, shapes, dtypes)
        arrays = []
        for field, shape, encoding, values in zip(fields, shapes, encodings, stored):
            if values is None or encoding["kind"] == "raw":
                arrays.append(values)
                continue
            kind = encoding["kind"]
            if kind == "box16":
                low = np.asarray(encoding["low"], np.float64)
                high = np.asarray(encoding["high"], np.float64)
                flat = values.reshape(len(values), -1).astype(np.float64)
                values = low + flat * ((high - low) / _BOX16)
            elif kind == "unit8":
                values = values.astype(np.float64) / _UNIT8
            elif kind == "snorm16":
                values = np.clip(values.astype(np.float64) / _SNORM16, -1, 1)
            arrays.append(np.asarray(values, dtype=field.dtype).reshape(shape))
        return arrays


class _DracoProfile:
    """Google Draco point clouds and meshes through DracoPy; see module notes."""

    id = "draco"
    version = 1
    lossless = False
    exact_shapes = False
    requires = "DracoPy>=2,<3"
    representations = frozenset({"mesh", "points"})

    @property
    def available(self) -> bool:
        try:
            import_module("DracoPy")
        except ImportError:
            return False
        return True

    @staticmethod
    def _backend():
        try:
            return import_module("DracoPy")
        except ImportError as error:
            raise ValueError("the draco stream profile needs the optional binding; "
                             "install 'DracoPy>=2,<3'") from error

    def options(self, options: dict) -> dict:
        options = dict(options)
        result = {"quantization_bits": _integer(options, "quantization_bits", 14, 1, 30),
                  "compression_level": _integer(options, "compression_level", 7, 0, 10),
                  "preserve_order": _boolean(options, "preserve_order", False)}
        _finish(options, self.id)
        return result

    def independent(self, options) -> bool:
        return True

    @staticmethod
    def _generic(field, array, count):
        if len(array) != count:
            raise ValueError(f"draco streams carry point- or vertex-aligned attributes only; "
                             f"{field.name!r} is not")
        values = array.reshape(count, -1)
        if values.dtype.kind == "b":
            return values.astype(np.uint8)
        if values.dtype.kind in "iu":
            if values.size and (values.min() < 0 or values.max() > np.iinfo(np.uint32).max):
                raise ValueError(f"draco cannot carry {field.name!r}: integers must fit uint32")
            return values.astype(np.uint32)
        return values.astype(np.float32)

    def encode(self, fields, arrays, reference, options):
        values = {field.name: array for field, array in zip(fields, arrays) if array is not None}
        positions = values["positions"]
        if not len(positions):
            return b"", {}
        triangles = values.get("triangles")
        index = None
        uv = values.get("texture_coordinates")
        if uv is not None and uv.ndim == 3:
            # Draco has per-vertex UVs: split vertices so every corner has its own.
            index = triangles.reshape(-1)
            triangles = np.arange(len(index), dtype=np.uint32).reshape(-1, 3)
            uv = uv.reshape(-1, 2)
        count = len(positions)

        def vertex(array):
            if array is None or index is None:
                return array
            if len(array) != count:
                raise ValueError("draco streams carry point- or vertex-aligned attributes only")
            return array[index]

        positions = vertex(positions)
        colors = vertex(values.get("colors"))
        normals = vertex(values.get("normals"))
        generic = {}
        for field in fields:
            if field.name.startswith("attribute:") and field.name in values:
                generic[field.name] = vertex(self._generic(field, values[field.name], count))
        if triangles is None and normals is not None:
            # DracoPy silently drops point-cloud normals, so carry them generically.
            generic["normals"] = normals.astype(np.float32)
            normals = None
        try:
            payload = self._backend().encode(
                positions, triangles,
                colors=None if colors is None else np.rint(np.clip(colors, 0, 1) * _UNIT8).astype(np.uint8),
                normals=None if normals is None else normals.astype(np.float64),
                tex_coord=None if uv is None else uv.astype(np.float64),
                generic_attributes=generic or None,
                quantization_bits=options["quantization_bits"],
                compression_level=options["compression_level"],
                preserve_order=options["preserve_order"],
            )
        except ValueError:
            raise
        except Exception as error:
            raise ValueError(f"draco could not encode the frame: {error}") from error
        return bytes(payload), {}

    def decode(self, payload, info, fields, shapes, reference, options):
        if info != {}:
            raise ValueError("draco codec information must be empty")
        present = {field.name for field, shape in zip(fields, shapes) if shape is not None}
        if not payload:
            if shapes[0] is None or shapes[0][0] != 0:
                raise ValueError("an empty draco payload must declare zero elements")
            return [None if shape is None else np.empty((0, *field.shape[1:]), field.dtype)
                    for field, shape in zip(fields, shapes)]
        try:
            decoded = self._backend().decode(payload)
        except ValueError:
            raise
        except Exception as error:
            raise ValueError(f"invalid draco payload: {error}") from error
        mesh = "triangles" in {field.name for field in fields}
        faces = getattr(decoded, "faces", None)
        if mesh != (faces is not None):
            raise ValueError("draco payload geometry type disagrees with the stream")

        def generic(name):
            attribute = decoded.get_attribute_by_name(name)
            return None if attribute is None else attribute["data"]

        found = {"positions": decoded.points, "triangles": faces,
                 "colors": decoded.colors,
                 "normals": getattr(decoded, "normals", None) if mesh else generic("normals"),
                 "texture_coordinates": getattr(decoded, "tex_coord", None) if mesh else None}
        arrays = []
        count = len(decoded.points)
        for field in fields:
            value = found[field.name] if field.name in found else generic(field.name)
            if (value is not None) != (field.name in present):
                raise ValueError(f"draco payload presence of {field.name!r} disagrees with the frame")
            if value is None:
                arrays.append(None)
                continue
            value = np.asarray(value)
            if field.name == "colors":
                value = value.astype(np.float32) / _UNIT8
            elif field.name == "texture_coordinates" and len(field.shape) == 3:
                value = np.asarray(value, np.float32)[np.asarray(faces, np.int64)]
            elif field.dtype.kind == "b":
                value = value != 0
            if field.name not in ("triangles", "texture_coordinates") and len(value) != count:
                raise ValueError(f"draco field {field.name!r} is not point-aligned")
            try:
                value = value.reshape(len(value), *field.shape[1:]).astype(field.dtype, copy=False)
            except ValueError as error:
                raise ValueError(f"draco field {field.name!r} has the wrong shape") from error
            arrays.append(value)
        return arrays


_ZLIB = _ArrayProfile("zlib", "zlib")
_ZSTD = _ArrayProfile("zstd", "zstd")
PROFILES = {profile.id: profile for profile in (
    _ArrayProfile("raw"), _ZLIB, _ZSTD, _DracoProfile(), _QuantizedProfile())}


def resolve_profile(name):
    """Return a profile by id; ``lossless`` picks zstd when installed, else zlib."""
    if name == "lossless":
        return _ZSTD if _ZSTD.available else _ZLIB
    if not isinstance(name, str) or name not in PROFILES:
        raise ValueError(f"unknown stream profile {name!r}; choose from "
                         f"{', '.join(['lossless', *PROFILES])}")
    return PROFILES[name]
