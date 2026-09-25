"""Sequence-level adapters for the native V-DMC implementations."""

from __future__ import annotations

from collections.abc import Mapping
import json
import math
from numbers import Real
import os
from pathlib import Path
import tempfile
from types import MappingProxyType
from zipfile import BadZipFile, ZIP_STORED, ZipFile

import numpy as np

from open4d.core import Frame, Sequence, TopologyMode, TriangleMesh
from open4d.io import Open4DError, open_sequence
from open4d.io._mesh import write_obj

from ._npz import _json_value, _publish_file, _validate_manifest
from ._protocol import CodecError
from ._native import run as _run
from ._v3c import pack_vmesh, probe_codec, unpack_vmesh

_SCHEMA = "open4d.vmesh-sequence/v1"
_POSITION_BIT_DEPTH = 12
_DEFAULT_RAW_FPS = 30.0


def _executable(value: str | os.PathLike[str] | None, variable: str) -> Path:
    selected = value or os.environ.get(variable)
    if not selected:
        raise CodecError(f"native codec executable is required; set {variable}")
    path = Path(selected).absolute()
    if not path.is_file() or not os.access(path, os.X_OK):
        raise CodecError(f"native codec executable is not runnable: {path}")
    return path


def _configuration(
    value: str | os.PathLike[str] | None, variable: str
) -> Path | None:
    selected = value or os.environ.get(variable)
    if not selected:
        return None
    path = Path(selected).absolute()
    if not path.is_file():
        raise CodecError(f"native codec configuration is missing: {path}")
    return path


def _raw_fps(value: float | None) -> float:
    if value is None:
        return _DEFAULT_RAW_FPS
    if not isinstance(value, Real) or isinstance(value, bool):
        raise TypeError("fps must be a real number or None")
    result = float(value)
    if not math.isfinite(result) or result <= 0:
        raise ValueError("fps must be finite and greater than zero")
    return result


def _position_normalization(manifest):
    if "position_bounds" not in manifest and "position_bit_depth" not in manifest:
        return None
    try:
        bounds = np.asarray(manifest["position_bounds"], dtype=np.float64)
        bits = manifest["position_bit_depth"]
        if (bounds.shape != (2, 3) or not np.isfinite(bounds).all()
                or np.any(bounds[0] > bounds[1]) or type(bits) is not int or not 1 <= bits <= 30):
            raise ValueError("expected finite XYZ bounds and a bit depth from 1 to 30")
    except (KeyError, TypeError, ValueError) as error:
        raise CodecError(f"invalid position normalization: {error}") from error
    return bounds, (1 << bits) - 1


class _DecodedProvider:
    def __init__(self, temporary: tempfile.TemporaryDirectory, decoded: Sequence, manifest: dict):
        self.temporary = temporary
        self.decoded = decoded
        self.manifest = manifest
        self.normalization = _position_normalization(manifest)
        self.frames = manifest["frames"]
        self.metadata = MappingProxyType(manifest.get("metadata", {}))
        self.topology = TopologyMode.UNKNOWN
        self.has_constant_vertex_count = None
        self.has_vertex_correspondence = None
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
        if self.normalization is not None:
            (lower, upper), limit = self.normalization
            positions = lower + geometry.positions / limit * (upper - lower)
            geometry = TriangleMesh(positions, geometry.triangles)
        return Frame(
            record["frame_index"], record["timestamp"], geometry,
            record.get("metadata", {}),
        )

    def close(self) -> None:
        try:
            self.decoded.close()
        finally:
            self.temporary.cleanup()


class _RawDecodedProvider:
    def __init__(
        self,
        temporary: tempfile.TemporaryDirectory,
        decoded: Sequence,
        source: Path,
        codec: str,
        fps: float,
    ) -> None:
        self.temporary = temporary
        self.decoded = decoded
        self.metadata = MappingProxyType(
            {
                "name": source.stem,
                "source": str(source),
                "format": ".vmesh",
                "codec": codec,
                "fps": fps,
                "raw_bitstream": True,
            }
        )
        self.topology = decoded.topology
        self.has_constant_vertex_count = decoded.has_constant_vertex_count
        self.has_vertex_correspondence = decoded.has_vertex_correspondence

    @property
    def frame_count(self) -> int:
        return len(self.decoded)

    @property
    def timestamps(self) -> tuple[float, ...]:
        return self.decoded.timestamps

    def get_frame(self, index: int) -> Frame:
        return self.decoded[index]

    def close(self) -> None:
        try:
            self.decoded.close()
        finally:
            self.temporary.cleanup()


class VMeshCodec:
    """Invoke one external V-Mesh process per sequence direction."""

    suffixes = (".v4d", ".vmesh")
    backend = "native-sequence"
    lossless = False
    preserves = ("positions", "triangles")

    def __init__(self, identifier: str) -> None:
        self.id = identifier
        self._environment = identifier.upper()

    def can_decode(self, source: Path) -> bool:
        if Path(source).suffix.lower() == ".vmesh":
            return probe_codec(source) == self.id
        try:
            with ZipFile(source) as archive:
                manifest = json.loads(archive.read("manifest.json"))
            return (
                isinstance(manifest, Mapping)
                and manifest.get("schema") == _SCHEMA
                and manifest.get("codec") == self.id
            )
        except (OSError, BadZipFile, KeyError, ValueError, TypeError):
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
            if destination.suffix.lower() == ".vmesh":
                # Preserve native V3C bytes plus the timing and normalization
                # previously available only in the .v4d ZIP wrapper.
                native_manifest = {k: v for k, v in manifest.items() if k != "schema"}
                native_manifest.update(version=1, native={"profile": f"{self.id}/1", "decoder_config": bool(decoder_config)})
                (work / "metadata.json").write_text(json.dumps(native_manifest), encoding="utf-8")
                if decoder_config:
                    import shutil
                    shutil.copyfile(Path(decoder_config).absolute(), work / "decoder.cfg")
                return pack_vmesh(work, destination, overwrite=overwrite)
            with tempfile.NamedTemporaryFile(
                prefix=f".{destination.name}.", suffix=".tmp",
                dir=destination.parent, delete=False,
            ) as temporary_stream:
                temporary = Path(temporary_stream.name)
            try:
                with ZipFile(temporary, "w", compression=ZIP_STORED) as archive:
                    archive.write(stream, "sequence.vmesh")
                    if decoder_config:
                        archive.write(Path(decoder_config).absolute(), "decoder.cfg")
                    archive.writestr("manifest.json", json.dumps(manifest))
                _publish_file(temporary, destination, overwrite=overwrite)
            except Exception:
                temporary.unlink(missing_ok=True)
                raise
        return destination

    def decode(
        self,
        source: Path,
        *,
        decoder: str | os.PathLike[str] | None = None,
        decoder_config: str | os.PathLike[str] | None = None,
        fps: float | None = None,
    ) -> Sequence:
        source = Path(source).absolute()
        carried = probe_codec(source) if source.suffix.lower() == ".vmesh" else None
        if carried is not None and carried != self.id:
            raise CodecError(f".vmesh contains {carried}, not {self.id}")
        raw = source.suffix.lower() == ".vmesh" and carried is None
        if not raw and fps is not None:
            raise TypeError("fps applies only to manifest-free .vmesh bitstreams")
        raw_rate = _raw_fps(fps) if raw else None
        executable = _executable(decoder, f"OPEN4D_{self._environment}_DECODER")
        config_variable = f"OPEN4D_{self._environment}_DECODER_CONFIG"
        configured = (
            _configuration(decoder_config, config_variable)
            if decoder_config is not None
            else None
        )
        temporary = tempfile.TemporaryDirectory(prefix=f"open4d-{self.id}-decode-")
        work = Path(temporary.name)
        decoded = None
        try:
            if carried:
                native = unpack_vmesh(source, work / "native")
                manifest = json.loads((native / "metadata.json").read_text())
                _validate_manifest(manifest, schema=None, codec=self.id)
                _position_normalization(manifest)
                stream = native / "sequence.vmesh"
            elif raw:
                stream = source
                manifest = None
            else:
                with ZipFile(source) as archive:
                    manifest = json.loads(archive.read("manifest.json"))
                    _validate_manifest(manifest, schema=_SCHEMA, codec=self.id)
                    _position_normalization(manifest)
                    archive.extract("sequence.vmesh", work)
                    if "decoder.cfg" in archive.namelist():
                        archive.extract("decoder.cfg", work)
                stream = work / "sequence.vmesh"
            output = work / "decoded"
            output.mkdir()
            # The pinned V-DMC decoder parses decTex even with zero attributes.
            command = [str(executable)]
            embedded_config = (work / "native" if carried else work) / "decoder.cfg"
            config = configured or (
                embedded_config
                if embedded_config.is_file()
                else _configuration(None, config_variable)
            )
            if config is not None:
                command.append(f"--config={config}")
            command.extend([
                f"--compressed={stream}",
                f"--decMesh={output / 'frame_%06d.obj'}",
                f"--decTex={output / 'texture_%06d.png'}", "--startFrameIndex=0",
            ])
            _run(command, f"{self.id} decoder")
            try:
                decoded = open_sequence(output, fps=raw_rate)
            except (Open4DError, OSError) as error:
                raise CodecError(
                    f"{self.id} decoder produced no readable OBJ frames: {error}"
                ) from error
            if raw:
                return Sequence(
                    _RawDecodedProvider(
                        temporary, decoded, source, self.id, raw_rate
                    )
                )
            assert manifest is not None
            if len(decoded) != len(manifest["frames"]):
                raise CodecError(
                    f"{self.id} decoded {len(decoded)} frames, expected {len(manifest['frames'])}"
                )
            return Sequence(_DecodedProvider(temporary, decoded, manifest))
        except BaseException:
            try:
                if decoded is not None:
                    decoded.close()
            finally:
                temporary.cleanup()
            raise


VDMC_CODEC = VMeshCodec("vdmc")
FASTER_VDMC_CODEC = VMeshCodec("faster_vdmc")
