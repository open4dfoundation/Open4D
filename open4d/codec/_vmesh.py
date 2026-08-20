"""Sequence-level adapters for the native V-DMC implementations."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import tempfile
from types import MappingProxyType
from zipfile import BadZipFile, ZIP_STORED, ZipFile

import numpy as np

from open4d.core import Frame, Sequence, TopologyMode
from open4d.io import open_sequence
from open4d.io._mesh import write_obj

from ._npz import _json_value
from ._protocol import CodecError

_SCHEMA = "open4d.vmesh-sequence/v1"


def _executable(value: str | os.PathLike[str] | None, variable: str) -> Path:
    selected = value or os.environ.get(variable)
    if not selected:
        raise CodecError(f"native codec executable is required; set {variable}")
    path = Path(selected).absolute()
    if not path.is_file() or not os.access(path, os.X_OK):
        raise CodecError(f"native codec executable is not runnable: {path}")
    return path


def _run(command: list[str], label: str) -> None:
    result = subprocess.run(command, check=False, capture_output=True, text=True)
    if result.returncode:
        detail = (result.stderr or result.stdout).strip().splitlines()
        raise CodecError(
            f"{label} exited {result.returncode}: "
            f"{detail[-1] if detail else 'no diagnostic output'}"
        )


class _DecodedProvider:
    def __init__(self, temporary: tempfile.TemporaryDirectory, decoded: Sequence, manifest: dict):
        self.temporary = temporary
        self.decoded = decoded
        self.frames = manifest["frames"]
        self.metadata = MappingProxyType(manifest.get("metadata", {}))
        self.topology = TopologyMode(manifest.get("topology", "unknown"))
        self.has_constant_vertex_count = manifest.get("has_constant_vertex_count")
        self.has_vertex_correspondence = manifest.get("has_vertex_correspondence")

    @property
    def frame_count(self) -> int:
        return len(self.frames)

    @property
    def timestamps(self) -> tuple[float, ...]:
        return tuple(frame["timestamp"] for frame in self.frames)

    def get_frame(self, index: int) -> Frame:
        record = self.frames[index]
        decoded = self.decoded[index]
        return Frame(
            record["frame_index"], record["timestamp"], decoded.geometry,
            record.get("metadata", {}),
        )

    def close(self) -> None:
        self.decoded.close()
        self.temporary.cleanup()


class VMeshCodec:
    """Wrap one complete V-Mesh encode/decode, never one process per frame."""

    suffixes = (".v4d",)
    backend = "native-sequence"
    lossless = False
    preserves = ("positions", "triangles")

    def __init__(self, identifier: str) -> None:
        self.id = identifier
        self._environment = identifier.upper()

    def can_decode(self, source: Path) -> bool:
        try:
            with ZipFile(source) as archive:
                manifest = json.loads(archive.read("manifest.json"))
            return manifest.get("schema") == _SCHEMA and manifest.get("codec") == self.id
        except (OSError, BadZipFile, KeyError, json.JSONDecodeError):
            return False

    def encode(
        self,
        sequence: Sequence,
        destination: Path,
        *,
        encoder: str | os.PathLike[str] | None = None,
        encoder_config: str | os.PathLike[str],
        decoder_config: str | os.PathLike[str],
        overwrite: bool = False,
    ) -> Path:
        executable = _executable(encoder, f"OPEN4D_{self._environment}_ENCODER")
        encoder_config = Path(encoder_config).absolute()
        decoder_config = Path(decoder_config).absolute()
        for config in (encoder_config, decoder_config):
            if not config.is_file():
                raise CodecError(f"native codec configuration is missing: {config}")
        destination = Path(destination).absolute()
        if destination.exists() and not overwrite:
            raise FileExistsError(f"artifact already exists: {destination}")
        if not len(sequence):
            raise CodecError("V-Mesh cannot encode an empty sequence")
        destination.parent.mkdir(parents=True, exist_ok=True)

        manifest = {
            "schema": _SCHEMA, "codec": self.id,
            "metadata": _json_value(sequence.metadata, "sequence"),
            "topology": sequence.topology.value,
            "has_constant_vertex_count": sequence.has_constant_vertex_count,
            "has_vertex_correspondence": sequence.has_vertex_correspondence,
            "frames": [],
        }
        with tempfile.TemporaryDirectory(prefix=f"open4d-{self.id}-") as directory:
            work = Path(directory)
            lower = np.full(3, np.inf)
            upper = np.full(3, -np.inf)
            for index, frame in enumerate(sequence):
                mesh = frame.geometry
                if any((mesh.colors is not None, mesh.normals is not None,
                        mesh.texture_coordinates is not None, bool(mesh.attributes))):
                    raise CodecError(f"{self.id} geometry-only profile cannot preserve attributes")
                write_obj(work / f"frame_{index:06d}.obj", mesh.positions, mesh.triangles)
                lower = np.minimum(lower, mesh.positions.min(0))
                upper = np.maximum(upper, mesh.positions.max(0))
                manifest["frames"].append({
                    "frame_index": frame.frame_index, "timestamp": frame.timestamp,
                    "metadata": _json_value(frame.metadata, f"frame {index}"),
                })
            stream = work / "sequence.vmesh"
            _run([
                str(executable), f"--config={encoder_config}",
                f"--srcMesh={work / 'frame_%06d.obj'}", "--srcTex=",
                "--videoAttributeCount=0", "--textureMapCount=0",
                "--textureParameterizationType=-1", "--encodeTextureVideo=0",
                "--startFrameIndex=0", f"--frameCount={len(sequence)}",
                f"--minPosition={','.join(map(str, lower))}",
                f"--maxPosition={','.join(map(str, upper))}",
                f"--compressed={stream}",
            ], f"{self.id} encoder")
            if not stream.is_file() or not stream.stat().st_size:
                raise CodecError(f"{self.id} encoder produced no bitstream")
            temporary = destination.with_name(f".{destination.name}.tmp")
            try:
                with ZipFile(temporary, "w", compression=ZIP_STORED) as archive:
                    archive.write(stream, "sequence.vmesh")
                    archive.write(decoder_config, "decoder.cfg")
                    archive.writestr("manifest.json", json.dumps(manifest))
                temporary.replace(destination)
            except Exception:
                temporary.unlink(missing_ok=True)
                raise
        return destination

    def decode(
        self, source: Path, *, decoder: str | os.PathLike[str] | None = None
    ) -> Sequence:
        executable = _executable(decoder, f"OPEN4D_{self._environment}_DECODER")
        source = Path(source).absolute()
        temporary = tempfile.TemporaryDirectory(prefix=f"open4d-{self.id}-decode-")
        work = Path(temporary.name)
        try:
            with ZipFile(source) as archive:
                manifest = json.loads(archive.read("manifest.json"))
                if manifest.get("schema") != _SCHEMA or manifest.get("codec") != self.id:
                    raise CodecError(f"artifact is not {self.id}")
                archive.extract("sequence.vmesh", work)
                archive.extract("decoder.cfg", work)
            output = work / "decoded"
            output.mkdir()
            # The pinned V-DMC decoder parses decTex even with zero attributes.
            _run([
                str(executable), f"--config={work / 'decoder.cfg'}",
                f"--compressed={work / 'sequence.vmesh'}",
                f"--decMesh={output / 'frame_%06d.obj'}",
                f"--decTex={output / 'texture_%06d.png'}", "--startFrameIndex=0",
            ], f"{self.id} decoder")
            decoded = open_sequence(output)
            if len(decoded) != len(manifest["frames"]):
                raise CodecError(
                    f"{self.id} decoded {len(decoded)} frames, expected {len(manifest['frames'])}"
                )
            return Sequence(_DecodedProvider(temporary, decoded, manifest))
        except Exception:
            temporary.cleanup()
            raise


VDMC_CODEC = VMeshCodec("vdmc")
FASTER_VDMC_CODEC = VMeshCodec("faster_vdmc")
