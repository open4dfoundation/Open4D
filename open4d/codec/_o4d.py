"""Sequence-level adapters for the native V-DMC implementations."""

from __future__ import annotations

import json
import math
from numbers import Real
import os
from pathlib import Path
import tempfile
from types import MappingProxyType

import numpy as np

from open4d._research_paths import default_runtime, managed_tool
from open4d.core import Frame, Sequence, TopologyMode, TriangleMesh, MeshTexture, TextureImage
from open4d.io import Open4DError, open_sequence
from open4d.io._mesh import write_obj

from ._metadata import _json_value, _validate_manifest, require_o4d_destination
from ._protocol import CodecError
from ._native import run as _run
from ._o4d_format import contains_codec, pack_o4d, probe_codec, unpack_o4d

_POSITION_BIT_DEPTH = 12
_DEFAULT_RAW_FPS = 30.0


def _executable(value: str | os.PathLike[str] | None, identifier: str, role: str) -> Path:
    """Find a V-DMC encoder or decoder that the user built separately.

    The order is the keyword argument, OPEN4D_<ID>_ENCODER or _DECODER, then a
    build inside OPEN4D_<ID>_ROOT (or this checkout's submodule) at
    build/Release/bin, the layout of upstream build.sh and build_vdmc_macos.sh.
    """
    prefix = f"OPEN4D_{identifier.upper()}"
    variable = f"{prefix}_{role.upper()}"
    selected = value or os.environ.get(variable)
    if not selected and not (os.environ.get(f"{prefix}_ROOT") or os.environ.get("OPEN4D_RESEARCH_ROOT")):
        selected = managed_tool(identifier, role)
    if not selected:
        root = Path(os.environ.get(f"{prefix}_ROOT") or
                    default_runtime(identifier)).expanduser()
        name = {"encoder": "encode", "decoder": "decode"}[role] + (".exe" if os.name == "nt" else "")
        built = [path for path in (root / "build/Release/bin" / name, root / "bin" / name)
                 if path.is_file()]
        if not built:
            raise CodecError(
                f"{identifier} {role} not found; pass {role}=, set {variable}, or set "
                f"{prefix}_ROOT to a V-DMC source tree built with upstream build.sh or "
                "scripts/build_vdmc_macos.sh"
            )
        selected = built[0]
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
        if self.manifest["native"]["profile"].endswith("/2"):
            from dataclasses import replace
            from open4d.io._mesh import _read_texture_image
            texture = Path(self.temporary.name) / "decoded" / f"texture_{index:06d}.png"
            if geometry.texture_coordinates is None or not texture.is_file():
                raise CodecError("native textured V-DMC decode omitted UVs or its texture image")
            geometry = replace(geometry, texture=MeshTexture((TextureImage(_read_texture_image(texture)),)))
        if self.normalization is not None:
            (lower, upper), limit = self.normalization
            positions = lower + geometry.positions / limit * (upper - lower)
            from dataclasses import replace
            geometry = replace(geometry, positions=positions)
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
                "format": ".o4d",
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


class O4DCodec:
    """Invoke one external V-Mesh process per sequence direction."""

    suffixes = (".o4d",)
    backend = "native-sequence"
    lossless = False
    preserves = ("positions", "triangles")

    def __init__(self, identifier: str) -> None:
        self.id = identifier
        self._environment = identifier.upper()

    def can_decode(self, source: Path) -> bool:
        return contains_codec(source, self.id)

    def encode(
        self,
        sequence: Sequence,
        destination: Path,
        *,
        encoder: str | os.PathLike[str] | None = None,
        encoder_config: str | os.PathLike[str] | None = None,
        decoder_config: str | os.PathLike[str] | None = None,
        attributes: str = "geometry",
        texture_encoder_config: str | os.PathLike[str] | None = None,
        texture_qp: int = 24,
        overwrite: bool = False,
    ) -> Path:
        """Encode geometry, or one native RGB texture per frame.

        ``attributes="texture"`` uses the runtime's UV and texture-video
        encoder, and requires its HM/VTM texture configuration explicitly.
        This profile is lossy and can remesh/reparameterize UVs. Other custom
        attributes, vertex colors, normals and multiple materials are rejected.
        """
        if attributes not in ("geometry", "texture"):
            raise ValueError("attributes must be 'geometry' or 'texture'")
        textured = attributes == "texture"
        if type(texture_qp) is not int or not 0 <= texture_qp <= 51:
            raise ValueError("texture_qp must be an integer from 0 to 51")
        if textured and texture_encoder_config is None:
            raise CodecError("native texture coding requires texture_encoder_config")
        destination = require_o4d_destination(destination)
        executable = _executable(encoder, self.id, "encoder")
        configs = [Path(value).absolute() for value in (encoder_config, decoder_config, texture_encoder_config) if value]
        for config in configs:
            if not config.is_file():
                raise CodecError(f"native codec configuration is missing: {config}")
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
            "version": 1, "codec": self.id,
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
                if any((mesh.colors is not None, mesh.normals is not None, bool(mesh.attributes))):
                    raise CodecError(f"{self.id} cannot preserve colors, normals or custom attributes")
                if not textured and (mesh.texture_coordinates is not None or mesh.texture is not None):
                    raise CodecError(f"{self.id} geometry-only profile cannot preserve attributes")
                if textured:
                    if mesh.texture is None or len(mesh.texture.images) != 1:
                        raise CodecError("native V-DMC texture coding requires one RGB texture per frame")
                    image = mesh.texture.images[0]
                    if (image.pixels.dtype != np.uint8 or image.channels != 3 or image.color_space != "srgb"
                            or image.wrap != "repeat" or image.filter != "linear"):
                        raise CodecError("native V-DMC texture coding requires uint8 sRGB images with three channels")
                    if np.any(mesh.texture_coordinates < 0) or np.any(mesh.texture_coordinates > 1):
                        raise CodecError("native V-DMC UV coordinates must be in [0, 1]")
                    if index and image.pixels.shape != sequence[0].geometry.texture.images[0].pixels.shape:
                        raise CodecError("native V-DMC texture dimensions must be constant")
                normalized = np.divide(
                    mesh.positions - lower, extent,
                    out=np.zeros_like(mesh.positions), where=extent != 0,
                )
                positions = np.rint(normalized * limit).clip(0, limit)
                # The reference runtime consumes prequantized OBJ coordinates,
                # including UVs. Fractional UVs would be divided by 4095 a
                # second time by its color-transfer stage and sample one pixel.
                uv = np.rint(mesh.texture_coordinates * limit) if textured else None
                write_obj(work / f"frame_{index:06d}.obj", positions, mesh.triangles,
                          texture_coordinates=uv, uv_format="%d" if textured else None)
                if textured:
                    from open4d.io._png import write_png
                    write_png(work / f"texture_{index:06d}.png", image.pixels)
                manifest["frames"].append({
                    "frame_index": frame.frame_index, "timestamp": frame.timestamp,
                    "metadata": _json_value(frame.metadata, f"frame {index}"),
                })
            stream = work / "sequence.vmesh"
            command = [str(executable)]
            if encoder_config:
                command.append(f"--config={Path(encoder_config).absolute()}")
            command.extend([
                f"--srcMesh={work / 'frame_%06d.obj'}",
                "--encodeDisplacements=1", f"--positionBitDepth={_POSITION_BIT_DEPTH}",
                "--startFrameIndex=0", f"--frameCount={len(sequence)}",
                f"--minPosition={','.join(map(str, lower))}",
                f"--maxPosition={','.join(map(str, upper))}",
                f"--compressed={stream}",
            ])
            if textured:
                command.extend([f"--srcAttrs={work / 'texture_%06d.png'}",
                    "--videoAttributeCount=1", "--textureMapCount=1", "--encodeTextureVideo=1",
                    "--textureParameterizationType=0", "--useRawUV=0",
                    f"--texCoordBitDepth={_POSITION_BIT_DEPTH}",
                    f"--textureVideoEncoderConfig={Path(texture_encoder_config).absolute()}",
                    f"--textureVideoWidth={image.width}", f"--textureVideoHeight={image.height}",
                    f"--textureVideoQP={texture_qp}"])
            else:
                command.extend(["--srcTex=", "--videoAttributeCount=0", "--textureMapCount=0",
                                "--textureParameterizationType=-1", "--encodeTextureVideo=0"])
            _run(command, f"{self.id} encoder")
            if not stream.is_file() or not stream.stat().st_size:
                raise CodecError(f"{self.id} encoder produced no bitstream")
            manifest["native"] = {"profile": f"{self.id}/{2 if textured else 1}", "decoder_config": bool(decoder_config)}
            (work / "metadata.json").write_text(json.dumps(manifest), encoding="utf-8")
            if decoder_config:
                import shutil
                shutil.copyfile(Path(decoder_config).absolute(), work / "decoder.cfg")
            return pack_o4d(work, destination, overwrite=overwrite)

    def decode(
        self,
        source: Path,
        *,
        decoder: str | os.PathLike[str] | None = None,
        decoder_config: str | os.PathLike[str] | None = None,
        fps: float | None = None,
    ) -> Sequence:
        source = Path(source).absolute()
        if source.suffix.lower() != ".o4d":
            raise CodecError("V-DMC decode requires O4D; use migrate_legacy for older artifacts")
        carried = probe_codec(source)
        if carried is not None and carried != self.id:
            raise CodecError(f".o4d contains {carried}, not {self.id}")
        raw = source.suffix.lower() == ".o4d" and carried is None
        if not raw and fps is not None:
            raise TypeError("fps applies only to manifest-free .o4d bitstreams")
        raw_rate = _raw_fps(fps) if raw else None
        executable = _executable(decoder, self.id, "decoder")
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
                native = unpack_o4d(source, work / "native")
                manifest = json.loads((native / "metadata.json").read_text())
                _validate_manifest(manifest, schema=None, codec=self.id)
                _position_normalization(manifest)
                stream = native / "sequence.vmesh"
            elif raw:
                stream = source
                manifest = None
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


VDMC_CODEC = O4DCodec("vdmc")
FASTER_VDMC_CODEC = O4DCodec("faster_vdmc")
