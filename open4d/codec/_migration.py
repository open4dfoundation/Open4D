"""Explicit, one-way conversion of retired sequence artifacts to VMESH."""

from __future__ import annotations

import json
import pickle
from pathlib import Path
import shutil
import tempfile
from zipfile import BadZipFile, ZipFile

from ._metadata import _validate_manifest, require_vmesh_destination
from ._protocol import CodecError
from ._vmesh_format import _json, pack_vmesh

_MAX_PAYLOAD = 8 * 1024**3


def _extract_vdmc(source: Path, destination: Path) -> None:
    from ._vmesh import _position_normalization

    try:
        with ZipFile(source) as archive:
            names = archive.namelist()
            if len(names) != len(set(names)) or archive.getinfo("manifest.json").file_size > 16 * 1024**2:
                raise CodecError("duplicate members or oversized migration manifest")
            if set(names) - {"manifest.json", "sequence.vmesh", "decoder.cfg"}:
                raise CodecError("unexpected V-DMC migration member")
            manifest = _json(archive.read("manifest.json"))
            codec = manifest.get("codec")
            if codec not in ("vdmc", "faster_vdmc"):
                raise CodecError("unsupported native V-DMC codec")
            _validate_manifest(manifest, schema="open4d.vmesh-sequence/v1", codec=codec)
            if not 1 <= len(manifest["frames"]) <= 65536:
                raise CodecError("migration frame count outside limits")
            _position_normalization(manifest)
            required = ["sequence.vmesh"]
            if "decoder.cfg" in names:
                required.append("decoder.cfg")
            for name in required:
                size = archive.getinfo(name).file_size
                maximum = 16 * 1024**2 if name == "decoder.cfg" else _MAX_PAYLOAD
                if not 0 < size <= maximum:
                    raise CodecError("migration payload size outside limits")
                with archive.open(name) as payload, (destination / name).open("xb") as output:
                    shutil.copyfileobj(payload, output)
            manifest.pop("schema")
            manifest.update(version=1, native={"profile": f"{codec}/1", "decoder_config": len(required) == 2})
            (destination / "metadata.json").write_text(json.dumps(manifest, allow_nan=False), encoding="utf-8")
    except (BadZipFile, KeyError, ValueError, TypeError, AttributeError) as error:
        raise CodecError(f"invalid migration input: {error}") from error


def migrate_legacy(source: str | Path, destination: str | Path, *, overwrite: bool = False,
                   fps: float = 30.0) -> Path:
    """Convert an older artifact explicitly; normal readers accept VMESH only.

    Native payloads are preserved except redundant checkpoint schema fields.
    Tensor checkpoints use restricted weights-only loading. Executable early
    QNDF-int8 contexts are deliberately unsupported.
    """
    source = Path(source).absolute()
    destination = require_vmesh_destination(destination)
    if not isinstance(overwrite, bool):
        raise TypeError("overwrite must be bool")
    if destination.exists() and not overwrite:
        raise FileExistsError(destination)
    if source.is_symlink():
        raise CodecError("linked migration source")
    if source.is_dir():
        if (source / "metadata.json").is_file():
            return pack_vmesh(source, destination, overwrite=overwrite)
        from open4d.native import import_native, save_native
        with import_native(source, codec="vega", fps=fps) as native:
            return save_native(native, destination, overwrite=overwrite)
    if source.suffix.lower() == ".seq":
        from open4d._streamer import _require
        _require()
        from streamer.sequence import convert_legacy
        return convert_legacy(source, destination, fps=fps, overwrite=overwrite)
    with tempfile.TemporaryDirectory(prefix="vmesh-migration-") as folder:
        native = Path(folder)
        suffix = source.suffix.lower()
        if suffix == ".k4d":
            from ._klt import _extract_legacy_klt
            _extract_legacy_klt(source, native)
        elif suffix in (".q4d", ".qi4d"):
            from ._qndf import _extract_legacy_qndf
            _extract_legacy_qndf(source, native, int8=suffix == ".qi4d")
        elif suffix == ".n4d":
            from ._n4mc import _extract_n4d, _write_native_metadata
            manifest = _extract_n4d(source, native)
            from importlib import import_module
            torch = import_module("torch")
            try:
                checkpoint = torch.load(native / "checkpoint.pt", map_location="cpu", weights_only=True)
            except (OSError, RuntimeError, ValueError, TypeError, pickle.UnpicklingError) as error:
                raise CodecError("invalid N4MC migration checkpoint") from error
            if not isinstance(checkpoint, dict):
                raise CodecError("invalid N4MC checkpoint")
            checkpoint.pop("schema", None)
            torch.save(checkpoint, native / "checkpoint.pt")
            _write_native_metadata(manifest, native)
        elif suffix == ".v4d":
            _extract_vdmc(source, native)
        else:
            raise ValueError("unsupported migration input; import native payload directories explicitly")
        return pack_vmesh(native, destination, overwrite=overwrite)
