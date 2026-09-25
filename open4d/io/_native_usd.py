"""Self-contained O4D native temporal data in USD, without executable loading.

The authored Xform is an O4D application schema, not a standard renderable
Gaussian or neural-field prim. Compressed bytes survive USDC round trips.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import tempfile

import numpy as np

from open4d._files import publish_file
from open4d.codec._protocol import CodecError
from open4d.codec._v3c import _json, inspect_vmesh
from ._usd import _pxr

_PRIM = "/Open4DNative"
_SCHEMA = "open4d.usd-native-temporal/1"
_CHUNK = 1024 * 1024


def _open(path):
    Sdf, Usd, _, _ = _pxr()
    from pxr import Tf
    from ._errors import DecodeError
    try:
        layer = Sdf.Layer.OpenAsAnonymous(str(Path(path).absolute()))
    except Tf.ErrorException as error:
        raise DecodeError(f"cannot read USD layer {path}: {error}") from error
    if layer is None:
        raise CodecError(f"cannot read USD layer {path}")
    stage = Usd.Stage.Open(layer)
    return stage, stage.GetPrimAtPath(_PRIM)


def is_native_usd(path):
    _, prim = _open(path)
    return bool(prim and prim.HasAttribute("open4d:nativeSchema"))


def write_native_usd(sequence, destination, *, overwrite=False):
    sequence._check_open()
    destination = Path(destination).absolute()
    if destination.suffix.lower() not in (".usd", ".usda", ".usdc"):
        raise ValueError("native USD interchange supports .usd, .usda and .usdc")
    if destination.exists() and not overwrite:
        raise FileExistsError(destination)
    Sdf, Usd, UsdGeom, Vt = _pxr()
    descriptor = inspect_vmesh(sequence.path)
    if any(right <= left for left, right in zip(sequence.timestamps, sequence.timestamps[1:])):
        raise CodecError("native USD export requires strictly increasing timestamps")
    size = sequence.path.stat().st_size
    count = (size + _CHUNK - 1) // _CHUNK
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=f".{destination.name}-", dir=destination.parent) as directory:
        temporary = Path(directory) / destination.name
        stage = Usd.Stage.CreateNew(str(temporary))
        prim = UsdGeom.Xform.Define(stage, _PRIM).GetPrim()
        stage.SetDefaultPrim(prim)
        stage.SetTimeCodesPerSecond(1.0)
        stage.SetStartTimeCode(sequence.timestamps[0])
        stage.SetEndTimeCode(sequence.timestamps[-1])
        prim.CreateAttribute("open4d:nativeSchema", Sdf.ValueTypeNames.String).Set(_SCHEMA)
        frame = prim.CreateAttribute("open4d:frameIndex", Sdf.ValueTypeNames.Int64)
        for index, timestamp in zip(sequence.frame_indices, sequence.timestamps):
            frame.Set(index, timestamp)
        digest = hashlib.sha256()
        with sequence.path.open("rb") as source:
            for index in range(count):
                data = source.read(_CHUNK)
                digest.update(data)
                prim.CreateAttribute(f"open4d:payload:chunk{index:06d}", Sdf.ValueTypeNames.UCharArray).Set(
                    Vt.UCharArray.FromNumpy(np.frombuffer(data, dtype=np.uint8)))
            if source.read(1):
                raise CodecError("native stream changed while exporting USD")
        header = dict(codec=sequence.codec, bytes=size, chunks=count, sha256=digest.hexdigest(),
                      representation=descriptor["representation"])
        prim.CreateAttribute("open4d:payloadManifest", Sdf.ValueTypeNames.String).Set(json.dumps(header, separators=(",", ":")))
        stage.GetRootLayer().Save()
        del stage
        # Verify the USD actually contains the original stream before publishing.
        with read_native_usd(temporary) as recovered:
            if recovered.path.stat().st_size != size:
                raise CodecError("USD native payload size changed")
        publish_file(temporary, destination, overwrite=overwrite)
    return destination


def read_native_usd(source, *, runtime=None, python=None):
    from open4d.native import NativeSequence

    stage, prim = _open(source)
    if not prim or prim.GetAttribute("open4d:nativeSchema").Get() != _SCHEMA:
        raise CodecError("unsupported O4D native USD schema")
    raw = prim.GetAttribute("open4d:payloadManifest").Get()
    if not isinstance(raw, str) or len(raw) > 1024 * 1024:
        raise CodecError("invalid native USD payload manifest")
    header = _json(raw)
    if not isinstance(header, dict):
        raise CodecError("invalid native USD payload manifest")
    count, size = header.get("chunks"), header.get("bytes")
    if (type(count) is not int or not 1 <= count <= 65536 or type(size) is not int
            or not (count - 1) * _CHUNK < size <= count * _CHUNK):
        raise CodecError("invalid native USD payload bounds")
    chunks = sorted(a.GetName() for a in prim.GetAttributes() if a.GetName().startswith("open4d:payload:chunk"))
    if chunks != [f"open4d:payload:chunk{i:06d}" for i in range(count)]:
        raise CodecError("missing or extra native USD chunks")
    temporary = tempfile.TemporaryDirectory(prefix="open4d-native-usd-")
    try:
        path = Path(temporary.name) / "sequence.vmesh"
        digest = hashlib.sha256()
        with path.open("xb") as stream:
            for index in range(count):
                attribute = prim.GetAttribute(chunks[index])
                if str(attribute.GetTypeName()) != "uchar[]" or attribute.GetNumTimeSamples():
                    raise CodecError("native USD chunks must be static uchar arrays")
                data = bytes(attribute.Get())
                if len(data) != min(_CHUNK, size - index * _CHUNK):
                    raise CodecError("native USD chunk size mismatch")
                digest.update(data)
                stream.write(data)
        if digest.hexdigest() != header.get("sha256"):
            raise CodecError("native USD payload SHA-256 mismatch")
        result = NativeSequence(path, temporary=temporary, runtime=runtime, python=python)
        if result.codec != header.get("codec") or result.representation != header.get("representation"):
            raise CodecError("native USD profile disagrees with carried stream")
        frame = prim.GetAttribute("open4d:frameIndex")
        if (stage.GetTimeCodesPerSecond() != 1.0 or tuple(frame.GetTimeSamples()) != result.timestamps
                or tuple(frame.Get(t) for t in result.timestamps) != result.frame_indices):
            raise CodecError("USD timeline disagrees with native temporal payload")
        return result
    except BaseException:
        temporary.cleanup()
        raise
