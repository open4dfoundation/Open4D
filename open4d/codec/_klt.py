"""In-process KLT TSDF codec using standalone O4D carriage."""

from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
from importlib import import_module
import json
import os
from pathlib import Path
import pickle
import shutil
from types import MappingProxyType, SimpleNamespace
import tempfile
from zipfile import BadZipFile, ZipFile

import numpy as np

from open4d.core import Frame, Sequence, TopologyMode, TriangleMesh
from open4d.io import open_sequence

from ._metadata import _json_value, _validate_manifest
from ._protocol import CodecError
from ._research import research_module
from ._tsdf import write_tsdf_sequence
from ._torch import torch_device
from ._o4d_format import contains_codec, pack_o4d, probe_codec, unpack_o4d

_SCHEMA = "open4d.klt-sequence/v1"


def _extract_legacy_klt(source: Path, destination: Path) -> dict:
    """Convert the retired archive explicitly; never used by normal decoding."""
    try:
        with ZipFile(source) as archive:
            members = archive.infolist()
            if len({member.filename for member in members}) != len(members):
                raise CodecError("duplicate KLT archive member")
            if archive.getinfo("manifest.json").file_size > 16 * 1024 * 1024:
                raise CodecError("oversized KLT manifest")
            manifest = json.loads(archive.read("manifest.json"))
            _validate_manifest(manifest, schema=_SCHEMA, codec="klt")
            _normalization(manifest)
            metadata = {key: value for key, value in manifest.items() if key != "schema"}
            metadata.update(version=1, native={"profile": "klt/1"})
            from ._native_profiles import layout
            required = [name for name, _ in layout("klt", len(metadata["frames"]), metadata["native"])[1:]]
            if set(archive.namelist()) != {"manifest.json", *required}:
                raise CodecError("unexpected KLT archive payloads")
            for name in required:
                member = archive.getinfo(name)
                limit = 16 * 1024 * 1024 if name.endswith((".pt", ".npz")) else 128 * 1024 * 1024
                if not 0 < member.file_size <= limit:
                    raise CodecError(f"KLT legacy payload outside limits: {name}")
                with archive.open(member) as payload, (destination / name).open("xb") as output:
                    shutil.copyfileobj(payload, output)
        torch = import_module("torch")
        checkpoint = destination / "decoder_context.pt"
        context = torch.load(checkpoint, map_location="cpu", weights_only=True)
        if not isinstance(context, dict) or context.pop("schema", None) != "open4d.klt/v1":
            raise CodecError("invalid legacy KLT context")
        context["version"] = 1
        # Old encoders could advertise more components than their training rank.
        context["num_components"] = len(context["basis"])
        _backend().validate_decoder_context(context)
        torch.save(context, checkpoint)
        (destination / "metadata.json").write_text(json.dumps(metadata, allow_nan=False), encoding="utf-8")
        return metadata
    except (BadZipFile, KeyError, ValueError, TypeError, RuntimeError, pickle.UnpicklingError) as error:
        raise CodecError(f"invalid legacy KLT artifact: {error}") from error


def _normalization(manifest):
    try:
        normalization = manifest["normalization"]
        center = np.asarray(normalization["center"], dtype=np.float64)
        scale = normalization["scale"]
        if (center.shape != (3,) or not np.isfinite(center).all()
                or type(scale) not in (int, float) or not np.isfinite(scale) or scale <= 0):
            raise ValueError("expected a finite XYZ center and positive scale")
    except (KeyError, TypeError, ValueError) as error:
        raise CodecError(f"invalid TSDF normalization: {error}") from error
    return center, float(scale)


def _backend():
    try:
        return research_module("klt.klt")
    except ImportError as error:
        raise CodecError("KLT dependencies are missing; install open4d[klt]") from error


class _KLTProvider:
    def __init__(self, temporary, decoded: Sequence, manifest: dict) -> None:
        self.temporary, self.decoded = temporary, decoded
        self.frames = manifest["frames"]
        self.metadata = MappingProxyType(manifest.get("metadata", {}))
        self.topology = TopologyMode.CHANGING
        self.has_constant_vertex_count = None
        self.has_vertex_correspondence = False
        self.allow_nonmonotonic_timestamps = manifest.get(
            "allow_nonmonotonic_timestamps", False
        )
        self.center, self.scale = _normalization(manifest)

    @property
    def frame_count(self):
        return len(self.frames)

    @property
    def timestamps(self):
        return tuple(record["timestamp"] for record in self.frames)

    def get_frame(self, index):
        record, decoded = self.frames[index], self.decoded[index]
        geometry = TriangleMesh(
            decoded.geometry.positions / self.scale + self.center,
            decoded.geometry.triangles,
        )
        return Frame(
            record["frame_index"], record["timestamp"], geometry,
            record.get("metadata", {}),
        )

    def close(self):
        try:
            self.decoded.close()
        finally:
            self.temporary.cleanup()


class KLTCodec:
    id = "klt"
    suffixes = (".o4d",)
    backend = "python-in-process"
    lossless = False
    preserves = ("positions", "triangles")

    def can_decode(self, source: Path) -> bool:
        return contains_codec(source, self.id)

    def encode(
        self, sequence: Sequence, destination: Path, *, overwrite: bool = False,
        resolution: int = 63, num_components: int = 64, block_size: int = 8,
        k_total: int = 16384, training_frames=(0,), frame_rate: float = 30,
        verbose: bool = False,
    ) -> Path:
        destination = Path(destination).absolute()
        if destination.suffix.lower() != ".o4d":
            raise ValueError("KLT destination must have a .o4d extension")
        if destination.exists() and not overwrite:
            raise FileExistsError(f"artifact already exists: {destination}")
        if type(resolution) is not int or not 7 <= resolution <= 255:
            raise ValueError("KLT resolution must be an integer from 7 to 255")
        if type(block_size) is not int or not 1 <= block_size <= min(16, resolution + 1):
            raise ValueError("KLT block_size must be an integer from 1 to 16 within the grid")
        if type(num_components) is not int or not 1 <= num_components <= min(block_size ** 3, 512):
            raise ValueError("KLT num_components exceeds the block dimensions")
        if type(k_total) is not int or not 1 <= k_total <= 65536:
            raise ValueError("KLT k_total must be an integer from 1 to 65536")
        training_frames = tuple(training_frames)
        if not training_frames or any(type(index) is not int or not 0 <= index < len(sequence)
                                      for index in training_frames):
            raise ValueError("KLT training_frames must select existing frames")
        if type(frame_rate) not in (int, float) or not np.isfinite(frame_rate) or frame_rate <= 0:
            raise ValueError("KLT frame_rate must be finite and positive")
        for frame in sequence:
            mesh = frame.geometry
            if any((mesh.colors is not None, mesh.normals is not None,
                    mesh.texture_coordinates is not None, bool(mesh.attributes))):
                raise CodecError("KLT's TSDF profile cannot preserve mesh attributes")
        destination.parent.mkdir(parents=True, exist_ok=True)
        manifest = {
            "version": 1, "codec": self.id, "native": {"profile": "klt/1"},
            "metadata": _json_value(sequence.metadata, "sequence"),
            "allow_nonmonotonic_timestamps": sequence.allow_nonmonotonic_timestamps,
            "frames": [{
                "frame_index": frame.frame_index, "timestamp": frame.timestamp,
                "metadata": _json_value(frame.metadata, f"frame {ordinal}"),
            } for ordinal, frame in enumerate(sequence)],
        }
        backend = _backend()
        with tempfile.TemporaryDirectory(prefix="open4d-klt-") as directory:
            work = Path(directory)
            manifest["normalization"] = write_tsdf_sequence(
                sequence, work / "tsdf", resolution=resolution
            )
            arguments = SimpleNamespace(
                input_path=str(work / "tsdf"), output_path=str(work / "encoded"),
                num_components=num_components, block_size=block_size,
                voxel_grid_res=resolution, k_total=k_total, fps=frame_rate,
                num_frames=len(sequence), training_frames=list(training_frames),
            )
            if verbose:
                backend.run_compression(arguments, verify_decode=False)
            else:
                with open(os.devnull, "w") as sink, redirect_stdout(sink), redirect_stderr(sink):
                    backend.run_compression(arguments, verify_decode=False)
            native = work / "encoded/compressed"
            (native / "metadata.json").write_text(json.dumps(manifest, allow_nan=False), encoding="utf-8")
            pack_o4d(native, destination, overwrite=overwrite)
        return destination

    def decode(self, source: Path, *, device=None) -> Sequence:
        if device == "auto":
            device = str(torch_device(import_module("torch"), device))
        source = Path(source).absolute()
        if source.suffix.lower() != ".o4d":
            raise CodecError("KLT decoding requires .o4d; migrate the legacy artifact explicitly")
        if probe_codec(source) != self.id:
            raise CodecError("O4D does not contain KLT")
        temporary = tempfile.TemporaryDirectory(prefix="open4d-klt-decode-")
        work = Path(temporary.name)
        decoded = None
        try:
            unpack_o4d(source, work / "compressed")
            manifest = json.loads((work / "compressed/metadata.json").read_text(encoding="utf-8"))
            _normalization(manifest)
            try:
                _backend().decode_compressed(work / "compressed", work / "decoded", device)
            except (ValueError, KeyError, TypeError, RuntimeError, BadZipFile, pickle.UnpicklingError) as error:
                raise CodecError(f"invalid KLT native payload: {error}") from error
            decoded = open_sequence(work / "decoded")
            if len(decoded) != len(manifest["frames"]):
                raise CodecError(
                    f"KLT decoded {len(decoded)} frames, expected {len(manifest['frames'])}"
                )
            return Sequence(_KLTProvider(temporary, decoded, manifest))
        except BaseException:
            try:
                if decoded is not None:
                    decoded.close()
            finally:
                temporary.cleanup()
            raise


KLT_CODEC = KLTCodec()
