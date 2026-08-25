"""Sequence-level adapters for the native V-DMC implementations."""

from __future__ import annotations

from collections.abc import Mapping
import json
import os
from pathlib import Path
import subprocess
import tempfile
from types import MappingProxyType
from zipfile import BadZipFile, ZIP_STORED, ZipFile

import numpy as np

from open4d.core import Frame, Sequence, TopologyMode, TriangleMesh
from open4d.io import open_sequence
from open4d.io._mesh import write_obj

from ._npz import _json_value
from ._protocol import CodecError

_SCHEMA = "open4d.vmesh-sequence/v1"
_POSITION_BIT_DEPTH = 12


def _executable(value: str | os.PathLike[str] | None, variable: str) -> Path:
    selected = value or os.environ.get(variable)
    if not selected:
        raise CodecError(f"native codec executable is required; set {variable}")
    path = Path(selected).absolute()
    if not path.is_file() or not os.access(path, os.X_OK):
        raise CodecError(f"native codec executable is not runnable: {path}")
    return path


def _run(command: list[str], label: str) -> None:
    result = subprocess.run(
        command, shell=False, check=False, capture_output=True, text=True
    )
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
        self.manifest = manifest
        self.frames = manifest["frames"]
        self.metadata = MappingProxyType(manifest.get("metadata", {}))
        self.topology = TopologyMode(manifest.get("topology", "unknown"))
        self.has_constant_vertex_count = manifest.get("has_constant_vertex_count")
        self.has_vertex_correspondence = manifest.get("has_vertex_correspondence")
        self.allow_nonmonotonic_timestamps = manifest.get(
            "allow_nonmonotonic_timestamps", False
        )

    @property
    def frame_count(self) -> int:
        return len(self.frames)

    @property
    def timestamps(self) -> tuple[float, ...]:
        return tuple(frame["timestamp"] for frame in self.frames)

    def get_frame(self, index: int) -> Frame:
        record = self.frames[index]
        decoded = self.decoded[index]
        geometry = decoded.geometry
        if "position_bounds" in self.manifest:
            lower, upper = np.asarray(self.manifest["position_bounds"])
            limit = (1 << self.manifest["position_bit_depth"]) - 1
            positions = lower + geometry.positions / limit * (upper - lower)
            geometry = TriangleMesh(positions, geometry.triangles)
        return Frame(
            record["frame_index"], record["timestamp"], geometry,
            record.get("metadata", {}),
        )

    def close(self) -> None:
        self.decoded.close()
        self.temporary.cleanup()


class VMeshCodec:
    """Invoke one external V-Mesh process per sequence direction."""

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
            return (
                isinstance(manifest, Mapping)
                and manifest.get("schema") == _SCHEMA
                and manifest.get("codec") == self.id
            )
        except (OSError, BadZipFile, KeyError, json.JSONDecodeError):
            return False

    def encode(
        self,
        sequence: Sequence,
        destination: Path,
        *,
        encoder: str | os.PathLike[str] | None = None,
        encoder_config: str | os.PathLike[str] | None = None,
        decoder_config: str | os.PathLike[str] | None = None,
        overwrite: bool = False,
    ) -> Path:
        executable = _executable(encoder, f"OPEN4D_{self._environment}_ENCODER")
        configs = [Path(value).absolute() for value in (encoder_config, decoder_config) if value]
        for config in configs:
            if not config.is_file():
                raise CodecError(f"native codec configuration is missing: {config}")
        destination = Path(destination).absolute()
        if destination.exists() and not overwrite:
            raise FileExistsError(f"artifact already exists: {destination}")
        if not len(sequence):
            raise CodecError("V-Mesh cannot encode an empty sequence")
        destination.parent.mkdir(parents=True, exist_ok=True)

        lower = np.full(3, np.inf)
        upper = np.full(3, -np.inf)
        for frame in sequence:
            lower = np.minimum(lower, frame.geometry.positions.min(0))
            upper = np.maximum(upper, frame.geometry.positions.max(0))
        manifest = {
            "schema": _SCHEMA, "codec": self.id,
            "metadata": _json_value(sequence.metadata, "sequence"),
            "topology": sequence.topology.value,
            "has_constant_vertex_count": sequence.has_constant_vertex_count,
            "has_vertex_correspondence": sequence.has_vertex_correspondence,
            "allow_nonmonotonic_timestamps": sequence.allow_nonmonotonic_timestamps,
            "position_bounds": [lower.tolist(), upper.tolist()],
            "position_bit_depth": _POSITION_BIT_DEPTH,
            "frames": [],
        }
        with tempfile.TemporaryDirectory(prefix=f"open4d-{self.id}-") as directory:
            work = Path(directory)
            extent = upper - lower
            limit = (1 << _POSITION_BIT_DEPTH) - 1
            for index, frame in enumerate(sequence):
                mesh = frame.geometry
                if any((mesh.colors is not None, mesh.normals is not None,
                        mesh.texture_coordinates is not None, bool(mesh.attributes))):
                    raise CodecError(f"{self.id} geometry-only profile cannot preserve attributes")
                normalized = np.divide(
                    mesh.positions - lower, extent,
                    out=np.zeros_like(mesh.positions), where=extent != 0,
                )
                positions = np.rint(normalized * limit).clip(0, limit)
                write_obj(work / f"frame_{index:06d}.obj", positions, mesh.triangles)
                manifest["frames"].append({
                    "frame_index": frame.frame_index, "timestamp": frame.timestamp,
                    "metadata": _json_value(frame.metadata, f"frame {index}"),
                })
            stream = work / "sequence.vmesh"
            command = [str(executable)]
            if encoder_config:
                command.append(f"--config={Path(encoder_config).absolute()}")
            command.extend([
                f"--srcMesh={work / 'frame_%06d.obj'}", "--srcTex=",
                "--videoAttributeCount=0", "--textureMapCount=0",
                "--textureParameterizationType=-1", "--encodeTextureVideo=0",
                "--encodeDisplacements=1", f"--positionBitDepth={_POSITION_BIT_DEPTH}",
                "--startFrameIndex=0", f"--frameCount={len(sequence)}",
                f"--minPosition={','.join(map(str, lower))}",
                f"--maxPosition={','.join(map(str, upper))}",
                f"--compressed={stream}",
            ])
            _run(command, f"{self.id} encoder")
            if not stream.is_file() or not stream.stat().st_size:
                raise CodecError(f"{self.id} encoder produced no bitstream")
            temporary = destination.with_name(f".{destination.name}.tmp")
            try:
                with ZipFile(temporary, "w", compression=ZIP_STORED) as archive:
                    archive.write(stream, "sequence.vmesh")
                    if decoder_config:
                        archive.write(Path(decoder_config).absolute(), "decoder.cfg")
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
                if not isinstance(manifest, Mapping):
                    raise CodecError("V-Mesh artifact manifest root must be an object")
                if manifest.get("schema") != _SCHEMA or manifest.get("codec") != self.id:
                    raise CodecError(f"artifact is not {self.id}")
                archive.extract("sequence.vmesh", work)
                if "decoder.cfg" in archive.namelist():
                    archive.extract("decoder.cfg", work)
            output = work / "decoded"
            output.mkdir()
            # The pinned V-DMC decoder parses decTex even with zero attributes.
            command = [str(executable)]
            if (work / "decoder.cfg").is_file():
                command.append(f"--config={work / 'decoder.cfg'}")
            command.extend([
                f"--compressed={work / 'sequence.vmesh'}",
                f"--decMesh={output / 'frame_%06d.obj'}",
                f"--decTex={output / 'texture_%06d.png'}", "--startFrameIndex=0",
            ])
            _run(command, f"{self.id} decoder")
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
