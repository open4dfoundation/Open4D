"""Finite sequence metadata shared by VMESH adapters."""

from collections.abc import Mapping
import math
from pathlib import Path
import numpy as np
from open4d._files import publish_file as _publish_file
from ._protocol import CodecError


def _json_value(value, name: str):
    if value is None or isinstance(value, (str, int, bool)):
        return value
    if isinstance(value, float):
        if not math.isfinite(value):
            raise CodecError(f"{name} metadata numbers must be finite")
        return value
    if isinstance(value, np.generic):
        return _json_value(value.item(), name)
    if isinstance(value, Path):
        return str(value)
    if isinstance(value, Mapping):
        if any(not isinstance(key, str) for key in value):
            raise CodecError(f"{name} metadata keys must be strings")
        return {key: _json_value(item, f"{name}.{key}") for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [_json_value(item, name) for item in value]
    raise CodecError(f"{name} metadata value {type(value).__name__} is not serializable")


def _validate_manifest(manifest, *, schema: str | None, codec: str) -> dict:
    if not isinstance(manifest, dict):
        raise CodecError("artifact manifest root must be an object")
    if manifest.get("schema") != schema or manifest.get("codec") != codec:
        raise CodecError(f"unsupported {codec} artifact schema or codec")
    frames = manifest.get("frames")
    if not isinstance(frames, list):
        raise CodecError("artifact manifest must contain a frame list")
    nonmonotonic = manifest.get("allow_nonmonotonic_timestamps", False)
    if not isinstance(nonmonotonic, bool):
        raise CodecError("allow_nonmonotonic_timestamps must be boolean")
    for name in ("has_constant_vertex_count", "has_vertex_correspondence"):
        if manifest.get(name) is not None and not isinstance(manifest[name], bool):
            raise CodecError(f"{name} must be boolean or null")
    if not isinstance(manifest.get("metadata", {}), dict):
        raise CodecError("sequence metadata must be an object")
    _json_value(manifest.get("metadata", {}), "sequence")
    previous = None
    for ordinal, record in enumerate(frames):
        if not isinstance(record, dict):
            raise CodecError(f"invalid frame record {ordinal}")
        index, timestamp = record.get("frame_index"), record.get("timestamp")
        if type(index) is not int or index < 0:
            raise CodecError(f"invalid frame index at {ordinal}")
        if type(timestamp) not in (int, float) or not math.isfinite(timestamp):
            raise CodecError(f"invalid frame timestamp at {ordinal}")
        if previous is not None and not nonmonotonic and timestamp < previous:
            raise CodecError("frame timestamps must be nondecreasing")
        previous = timestamp
        if not isinstance(record.get("metadata", {}), dict):
            raise CodecError(f"frame {ordinal} metadata must be an object")
        _json_value(record.get("metadata", {}), f"frame {ordinal}")
    return manifest


def require_vmesh_destination(destination: str | Path) -> Path:
    path = Path(destination).absolute()
    if path.suffix.lower() != ".vmesh":
        raise ValueError("compressed output requires a .vmesh extension")
    return path
