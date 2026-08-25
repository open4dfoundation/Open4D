"""In-process adapter for Open4D's KLT TSDF codec."""

from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
from importlib import import_module
import json
import os
from pathlib import Path
import shutil
from types import MappingProxyType, SimpleNamespace
import tempfile
from zipfile import BadZipFile, ZIP_DEFLATED, ZipFile

import numpy as np

from open4d.core import Frame, Sequence, TopologyMode, TriangleMesh
from open4d.io import open_sequence

from ._npz import _json_value
from ._protocol import CodecError
from ._tsdf import write_tsdf_sequence

_SCHEMA = "open4d.klt-sequence/v1"


def _backend():
    try:
        return import_module("open4d.codecs.klt.klt")
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
        normalization = manifest["normalization"]
        self.center = np.asarray(normalization["center"], dtype=np.float32)
        self.scale = float(normalization["scale"])

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
        self.decoded.close()
        self.temporary.cleanup()


class KLTCodec:
    id = "klt"
    suffixes = (".k4d",)
    backend = "python-in-process"
    lossless = False
    preserves = ("positions", "triangles")

    def can_decode(self, source: Path) -> bool:
        try:
            with ZipFile(source) as archive:
                return json.loads(archive.read("manifest.json")).get("schema") == _SCHEMA
        except (OSError, BadZipFile, KeyError, json.JSONDecodeError):
            return False

    def encode(
        self, sequence: Sequence, destination: Path, *, overwrite: bool = False,
        resolution: int = 63, num_components: int = 64, block_size: int = 8,
        k_total: int = 16384, training_frames=(0,), frame_rate: float = 30,
        verbose: bool = False,
    ) -> Path:
        destination = Path(destination).absolute()
        if destination.exists() and not overwrite:
            raise FileExistsError(f"artifact already exists: {destination}")
        for frame in sequence:
            mesh = frame.geometry
            if any((mesh.colors is not None, mesh.normals is not None,
                    mesh.texture_coordinates is not None, bool(mesh.attributes))):
                raise CodecError("KLT's TSDF profile cannot preserve mesh attributes")
        destination.parent.mkdir(parents=True, exist_ok=True)
        manifest = {
            "schema": _SCHEMA, "codec": self.id,
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
            with tempfile.NamedTemporaryFile(
                prefix=f".{destination.name}.", suffix=".tmp",
                dir=destination.parent, delete=False,
            ) as stream:
                temporary = Path(stream.name)
            try:
                shutil.copyfile(work / "encoded/compressed_archive.zip", temporary)
                with ZipFile(temporary, "a", compression=ZIP_DEFLATED) as archive:
                    archive.writestr("manifest.json", json.dumps(manifest))
                temporary.replace(destination)
            except Exception:
                temporary.unlink(missing_ok=True)
                raise
        return destination

    def decode(self, source: Path, *, device=None) -> Sequence:
        source = Path(source).absolute()
        temporary = tempfile.TemporaryDirectory(prefix="open4d-klt-decode-")
        work = Path(temporary.name)
        try:
            with ZipFile(source) as archive:
                manifest = json.loads(archive.read("manifest.json"))
                if manifest.get("schema") != _SCHEMA:
                    raise CodecError("unsupported KLT artifact schema")
                members = [item for item in archive.infolist() if item.filename != "manifest.json"]
                if any(Path(item.filename).is_absolute() or ".." in Path(item.filename).parts
                       for item in members):
                    raise CodecError("unsafe path in KLT artifact")
                archive.extractall(work / "compressed", members)
            _backend().decode_compressed(work / "compressed", work / "decoded", device)
            decoded = open_sequence(work / "decoded")
            if len(decoded) != len(manifest["frames"]):
                raise CodecError(
                    f"KLT decoded {len(decoded)} frames, expected {len(manifest['frames'])}"
                )
            return Sequence(_KLTProvider(temporary, decoded, manifest))
        except Exception:
            temporary.cleanup()
            raise


KLT_CODEC = KLTCodec()
