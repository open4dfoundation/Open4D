"""TVMC and TSMC adapters backed by their research pipelines."""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
from types import MappingProxyType

from open4d._files import publish_directory
from open4d.core import Frame, Sequence, TopologyMode, TriangleMesh
from open4d.io import open_sequence
from open4d.io._mesh import write_obj

from ._npz import _json_value, _validate_manifest
from ._protocol import CodecError
from ._native import run as _run_native
from ._v3c import pack_vmesh, probe_codec, unpack_vmesh


def _executable(value, label: str) -> str:
    candidate = shutil.which(str(value))
    if candidate is None:
        raise CodecError(f"{label} is not executable: {value}")
    return str(Path(candidate).absolute())


def _positive_integer(value, name: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        raise ValueError(f"{name} must be a positive integer")
    return value


def _run(python: str, action: str, request: Path) -> None:
    worker = Path(__file__).with_name("_tracked_worker.py")
    _run_native([python, str(worker), action, str(request)], action, cwd=request.parent)


def _manifest(source: Path, codec: str) -> dict:
    try:
        value = json.loads((source / "metadata.json").read_text(encoding="utf-8"))
        if value["codec"] != codec or type(value["version"]) is not int or value["version"] != 1:
            raise ValueError("codec or version does not match")
        _validate_manifest(value, schema=None, codec=codec)
        if not value["frames"]:
            raise ValueError("frame list is empty or invalid")
        return value
    except (OSError, ValueError, KeyError, TypeError) as error:
        raise CodecError(f"invalid {codec} directory {source}: {error}") from error


class _DecodedProvider:
    topology = TopologyMode.FIXED
    has_constant_vertex_count = True
    has_vertex_correspondence = True

    def __init__(self, temporary, sequence, manifest):
        self.temporary = temporary
        self.sequence = sequence
        self.frames = manifest["frames"]
        self.metadata = MappingProxyType(manifest.get("metadata", {}))
        self.allow_nonmonotonic_timestamps = manifest.get(
            "allow_nonmonotonic_timestamps", False
        )

    @property
    def frame_count(self):
        return len(self.frames)

    @property
    def timestamps(self):
        return tuple(record["timestamp"] for record in self.frames)

    def get_frame(self, index):
        record = self.frames[index]
        return Frame(
            record["frame_index"], record["timestamp"],
            self.sequence[index].geometry, record.get("metadata", {}),
        )

    def close(self):
        try:
            self.sequence.close()
        finally:
            self.temporary.cleanup()


class TrackedMeshCodec:
    backend = "research-subprocess"
    lossless = False
    preserves = ("positions", "triangles")

    def __init__(self, identifier):
        self.id = identifier
        self.suffixes = (f".{identifier}", ".vmesh")

    def can_decode(self, source: Path) -> bool:
        source = Path(source)
        if source.is_file() and source.suffix.lower() == ".vmesh":
            return probe_codec(source) == self.id
        try:
            _manifest(source, self.id)
        except CodecError:
            return False
        return True

    def _settings(self, backend, python, encoder, decoder, *, encoding):
        variable = f"OPEN4D_{self.id.upper()}_ROOT"
        root = Path(backend or os.environ.get(variable) or
                    Path(__file__).resolve().parents[1] / "codecs" / self.id).absolute()
        tools = root / ("TVMC" if self.id == "tvmc" else "tsmc")
        if (encoding or self.id == "tsmc") and not (tools / "get_displacements.py").is_file():
            raise CodecError(
                f"{self.id} research source is missing; pass backend= or set {variable} "
                "to its source directory, then run its setup.sh"
            )
        private = root / ".venv" / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
        interpreter = python or os.environ.get(f"OPEN4D_{self.id.upper()}_PYTHON")
        interpreter = interpreter or (private if private.is_file() else sys.executable)
        settings = {"codec": self.id, "backend": str(root),
                    "python": _executable(interpreter, "Python")}
        for name, selected in (("encoder", encoder), ("decoder", decoder)):
            if name == "encoder" and not encoding:
                continue
            executable = f"draco_{name}" + (".exe" if os.name == "nt" else "")
            selected = selected or os.environ.get(f"DRACO_{name.upper()}")
            if not selected:
                candidates = (root / "draco/build" / executable,
                              root / "draco/build/Release" / executable)
                selected = next((path for path in candidates if path.is_file()),
                                shutil.which(executable) or candidates[0])
            settings[name] = _executable(
                selected, f"{self.id} {name}; run {root / 'setup.sh'} or pass {name}="
            )
        return settings

    def encode(
        self, sequence: Sequence, destination: Path, *, backend=None, python=None,
        dotnet=None, encoder=None, decoder=None, centers=None, num_centers=2000,
        grid_resolution=512, key_frame=None, components=None, quantization=None,
        overwrite=False,
    ) -> Path:
        """Encode native payloads to a .vmesh file or legacy codec directory."""
        destination = Path(destination).absolute()
        container = destination.suffix.lower() == ".vmesh"
        if destination.exists() and not overwrite:
            raise FileExistsError(f"destination already exists: {destination}")
        if container and destination.is_dir():
            raise IsADirectoryError(destination)
        if not container and destination.exists() and not destination.is_dir():
            raise NotADirectoryError(destination)
        if len(sequence) < 2:
            raise CodecError(f"{self.id} requires at least two frames")
        _positive_integer(num_centers, "num_centers")
        _positive_integer(grid_resolution, "grid_resolution")
        if self.id == "tvmc":
            if components is not None:
                raise TypeError("components applies only to TSMC")
            quantization = 10 if quantization is None else quantization
            _positive_integer(quantization, "quantization")
            if quantization > 30:
                raise ValueError("quantization must not exceed 30 bits")
        else:
            if quantization is not None:
                raise TypeError("quantization applies only to TVMC")
            components = 5 if components is None else components
            _positive_integer(components, "components")
            if components > 3 * len(sequence):
                raise ValueError("components must not exceed three times the frame count")
        key_frame = len(sequence) // 2 if key_frame is None else key_frame
        if isinstance(key_frame, bool) or not isinstance(key_frame, int) or not 0 <= key_frame < len(sequence):
            raise ValueError("key_frame must be an index in the sequence")
        if self.id == "tsmc" and key_frame == 0:
            raise ValueError("TSMC requires a key_frame with a preceding frame")
        settings = self._settings(backend, python, encoder, decoder, encoding=True)
        settings.update(
            dotnet=_executable(dotnet or os.environ.get("DOTNET", "dotnet"), ".NET; run the codec setup.sh"),
            num_centers=num_centers, grid_resolution=grid_resolution,
            key_frame=key_frame, components=components, quantization=quantization,
        )
        if centers is not None:
            centers = Path(centers).absolute()
            if not centers.is_dir():
                raise FileNotFoundError(f"tracked center directory is missing: {centers}")
            settings["centers"] = str(centers)
        manifest = {
            "version": 1, "codec": self.id, "frames": [],
            "metadata": _json_value(sequence.metadata, "sequence"),
            "allow_nonmonotonic_timestamps": sequence.allow_nonmonotonic_timestamps,
        }
        destination.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix=f".{destination.name}-", dir=destination.parent) as directory:
            work = Path(directory)
            source = work / "input"
            source.mkdir()
            for index, frame in enumerate(sequence):
                mesh = frame.geometry
                if not isinstance(mesh, TriangleMesh):
                    raise TypeError(f"{self.id} accepts triangle meshes")
                if not len(mesh.positions) or not len(mesh.triangles):
                    raise CodecError(f"frame {index} has no mesh surface")
                if any((mesh.colors is not None, mesh.normals is not None,
                        mesh.texture_coordinates is not None, bool(mesh.attributes))):
                    raise CodecError(f"{self.id} geometry-only profile cannot preserve attributes")
                write_obj(source / f"mesh_{index:03d}.obj", mesh.positions, mesh.triangles)
                manifest["frames"].append({
                    "frame_index": frame.frame_index, "timestamp": frame.timestamp,
                    "metadata": _json_value(frame.metadata, f"frame {index}"),
                })
            result = work / "encoded"
            result.mkdir()
            settings.update(input=str(source), output=str(result), frames=len(sequence))
            request = work / "request.json"
            request.write_text(json.dumps(settings), encoding="utf-8")
            _run(settings["python"], "encode", request)
            required = ["reference.drc"]
            if self.id == "tvmc":
                required += [f"displacement_{index:06d}.{suffix}" for index in range(len(sequence))
                             for suffix in ("drc", "npy")]
            else:
                required += ["B_matrix.txt", "T_matrix.txt", "delta_trajectories_encoded.npy", "entropy_model.npz"]
            for name in required:
                if not (result / name).is_file() or not (result / name).stat().st_size:
                    raise CodecError(f"{self.id} encoder produced no {name}")
            (result / "metadata.json").write_text(json.dumps(manifest), encoding="utf-8")
            if container:
                return pack_vmesh(result, destination, overwrite=overwrite)
            previous = None
            if destination.exists():
                if not overwrite:
                    raise FileExistsError(f"destination already exists: {destination}")
                previous = Path(tempfile.mkdtemp(
                    prefix=f".{destination.name}.backup.", dir=destination.parent,
                ))
                previous.rmdir()
                destination.rename(previous)
            try:
                if overwrite:
                    result.rename(destination)
                else:
                    publish_directory(result, destination)
            except BaseException:
                if previous is not None:
                    try:
                        previous.rename(destination)
                    except OSError as error:
                        raise OSError(
                            f"Could not restore {destination}; original output remains at {previous}"
                        ) from error
                raise
            if previous is not None:
                if previous.is_symlink():
                    previous.unlink()
                else:
                    shutil.rmtree(previous)
        return destination

    def decode(self, source: Path, *, backend=None, python=None, decoder=None) -> Sequence:
        """Reconstruct frames from a .vmesh file or native codec directory."""
        source = Path(source).absolute()
        temporary = tempfile.TemporaryDirectory(prefix=f"open4d-{self.id}-decode-")
        decoded = None
        try:
            work = Path(temporary.name)
            if source.is_file() and source.suffix.lower() == ".vmesh":
                if probe_codec(source) != self.id:
                    raise CodecError(f".vmesh does not contain {self.id} payloads")
                source = unpack_vmesh(source, work / "native")
            manifest = _manifest(source, self.id)
            settings = self._settings(backend, python, None, decoder, encoding=False)
            output = work / "decoded"
            output.mkdir()
            settings.update(input=str(source), output=str(output), frames=len(manifest["frames"]))
            request = work / "request.json"
            request.write_text(json.dumps(settings), encoding="utf-8")
            _run(settings["python"], "decode", request)
            decoded = open_sequence(output)
            if len(decoded) != len(manifest["frames"]):
                raise CodecError(f"{self.id} decoded {len(decoded)} frames, expected {len(manifest['frames'])}")
            return Sequence(_DecodedProvider(temporary, decoded, manifest))
        except BaseException:
            try:
                if decoded is not None:
                    decoded.close()
            finally:
                temporary.cleanup()
            raise


TVMC_CODEC = TrackedMeshCodec("tvmc")
TSMC_CODEC = TrackedMeshCodec("tsmc")
