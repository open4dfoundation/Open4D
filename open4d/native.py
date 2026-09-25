"""Portable native temporal runs and lossless USDC / V3C interchange.

Opening a run never executes its models. ``decode`` and ``render`` explicitly
invoke the selected research runtime. USD interchange retains the compressed
representation; it does not turn an image-trained field into a mesh codec.
"""
from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass
import json
import math
import os
from pathlib import Path
import shutil
import sys
import tempfile
from types import MappingProxyType

import numpy as np

from .codec._native import run
from .codec._native_profiles import NEURAL_CODECS, layout
from .codec._npz import _json_value
from .codec._protocol import CodecError
from .codec._v3c import _json, inspect_vmesh, pack_vmesh, unpack_vmesh
from ._files import publish_file


def _read_json(path):
    path = Path(path)
    if path.is_symlink() or not path.is_file() or path.stat().st_size > 16 * 1024 * 1024:
        raise CodecError(f"missing, linked or oversized native configuration: {path}")
    return _json(path.read_bytes())


def _copy(source, destination):
    source = Path(source)
    if source.is_symlink() or not source.is_file() or not source.stat().st_size:
        raise CodecError(f"missing, linked or empty native payload: {source}")
    # Reject directory symlinks too: a temporal import has a closed payload set.
    if any(parent.is_symlink() for parent in source.parents):
        raise CodecError(f"native payload has a linked parent: {source}")
    shutil.copyfile(source, destination)


def _latest_ply(root, *, added=False):
    root = Path(root)
    direct = root / ("added.ply" if added else "point_cloud.ply")
    if direct.is_file():
        return direct
    suffix = "added/point_cloud.ply" if added else "point_cloud.ply"
    candidates = [p for p in (root / "point_cloud").glob("iteration_*")
                  if p.name[10:].isdigit() and (p / suffix).is_file()]
    if not candidates:
        raise CodecError(f"no {'added' if added else 'initial'} Gaussian payload in {root}")
    return max(candidates, key=lambda p: int(p.name[10:])) / suffix


def _configuration(config):
    if isinstance(config, Mapping):
        return _json_value(dict(config), "native configuration")
    if config is None:
        return {}
    path = Path(config)
    if path.suffix.lower() in (".yaml", ".yml"):
        try:
            import yaml
        except ImportError as error:
            raise ImportError("Pass configuration as a dictionary, or install PyYAML to read YAML") from error
        if path.is_symlink() or path.stat().st_size > 16 * 1024 * 1024:
            raise CodecError("linked or oversized native configuration")
        value = yaml.safe_load(path.read_text())
    else:
        value = _read_json(path)
    if not isinstance(value, dict):
        raise CodecError("native configuration must be a JSON/YAML object")
    return _json_value(value, "native configuration")


def _stage_native(source, destination, codec, config, initial_model, ntc_config):
    """Copy only actual decoder dependencies, returning native indices/profile."""
    cfg = _configuration(config)
    profile = {"profile": f"{codec}/1"}
    if codec == "vega":
        from ._gaussian_worker import _read_manifest
        entries = _read_manifest(source)
        indices = [e["frame_idx"] for e in entries]
        for name in ["manifest.json", "color_model.pt", *(e["file"] for e in entries)]:
            _copy(source / name, destination / name)
    elif codec == "queen":
        frames = sorted((p for p in (source / "frames").iterdir() if p.is_dir() and p.name.isdigit()), key=lambda p: int(p.name))
        indices = [int(p.name) for p in frames]
        if len(frames) < 2 or indices != list(range(1, len(frames) + 1)):
            raise CodecError("QUEEN requires initial frame 1 and contiguous compressed later frames")
        model, quant = cfg.get("model_params", cfg), cfg.get("quantize_params", cfg)
        if "sh_degree" not in model:
            raise CodecError("QUEEN import needs config with sh_degree and gate settings used to encode the run")
        gates = quant.get("gate_params", [quant.get(f"{name}_gate_params", "none") for name in ("xyz", "f_dc", "f_rest", "sc", "rot", "op", "flow")])
        profile.update(sh_degree=model["sh_degree"], gate_params=gates)
        _copy(initial_model or _latest_ply(frames[0]), destination / "initial.ply")
        for index, frame in enumerate(frames[1:], 1):
            _copy(frame / "compressed/point_cloud.pkl", destination / f"frame_{index:06d}.pkl")
    elif codec == "3dgstream":
        if not cfg:
            cfg = _read_json(source / "cfg_args.json")
        frames = sorted((p for p in source.glob("frame[0-9]*") if p.is_dir() and p.name[5:].isdigit()), key=lambda p: int(p.name[5:]))
        indices = [0] + [int(p.name[5:]) for p in frames]
        if len(frames) < 1 or indices != list(range(len(frames) + 1)):
            raise CodecError("3DGStream requires initial frame 0 and contiguous NTC frames starting at 1")
        profile.update(sh_degree=cfg.get("sh_degree"), rotate_sh=cfg.get("rotate_sh"), only_mlp=cfg.get("only_mlp"), added=[False])
        _copy(initial_model or _latest_ply(source / "init"), destination / "initial.ply")
        # Never follow arbitrary paths in cfg_args.json. The architecture is an explicit input.
        architecture = _configuration(ntc_config or source / "ntc_config.json")
        if not isinstance(architecture.get("network"), dict) or (not profile["only_mlp"] and not isinstance(architecture.get("encoding"), dict)):
            raise CodecError("3DGStream needs the NTC network/encoding configuration used for training")
        (destination / "ntc_config.json").write_text(json.dumps(architecture, allow_nan=False))
        stage_two = cfg.get("iterations_s2", 0)
        if type(stage_two) is not int or stage_two < 0:
            raise CodecError("invalid 3DGStream second-stage iteration count")
        for index, frame in enumerate(frames, 1):
            _copy(frame / "NTC.pth", destination / f"ntc_{index:06d}.pth")
            added = stage_two > 0 or any((frame / "point_cloud").glob("iteration_*/added/point_cloud.ply"))
            profile["added"].append(bool(added))
            if added:
                _copy(_latest_ply(frame, added=True), destination / f"added_{index:06d}.ply")
    elif codec == "rerf":
        if not cfg:
            cfg = _read_json(source / "decoder_config.json")
        native = cfg.get("native", {})
        profile.update(group_size=native.get("group_size"), pca=native.get("pca"),
                       pca_channels=native.get("pca_channels", [7, 13]), frames=[])
        required = ("voxel_size", "fine_model_and_render", "data", "render")
        if not all(key in cfg for key in required):
            raise CodecError("ReRF needs a resolved decoder JSON configuration including render near/far; no training-directory fallback")
        indices = sorted(int(p.stem[7:]) for p in source.glob("header_*.json") if p.stem[7:].isdigit())
        for index in indices:
            header = _read_json(source / f"header_{index}.json")
            headers = header.get("headers") if isinstance(header, dict) else None
            if not isinstance(headers, list) or not headers or not isinstance(headers[0], dict):
                raise CodecError("invalid ReRF frame header")
            motion = (source / f"deform_{index}.npy").is_file()
            if motion != (source / f"deform_mask_{index}.rerf").is_file():
                raise CodecError("incomplete ReRF motion payload")
            channels = []
            quality = headers[0].get("quality")
            for half, header_part in enumerate(headers):
                size = header_part.get("size") if isinstance(header_part, dict) else None
                if (not isinstance(size, list) or len(size) != 5 or type(quality) is not int
                        or header_part.get("quality") != quality - half):
                    raise CodecError("invalid ReRF entropy header shape/quality")
                channels.append(size[1])
            profile["frames"].append(dict(id=index, quality=quality, motion=motion, channels=channels))
        # Layout validation detects missing keys, gaps, PCA halves and motion.
        for name, _ in layout(codec, len(indices), profile)[1:]:
            if name != "decoder_config.json":
                _copy(source / name, destination / name)
        (destination / "decoder_config.json").write_text(json.dumps(cfg, allow_nan=False))
    else:
        raise CodecError(f"{codec!r} is not a native neural temporal profile")
    layout(codec, len(indices), profile)
    return indices, profile


@dataclass(frozen=True)
class NeuralFieldFrame:
    """Decoded ReRF density/features; appearance remains in the native RGB model."""
    frame_index: int
    timestamp: float
    density: np.ndarray
    features: np.ndarray
    xyz_min: np.ndarray
    xyz_max: np.ndarray


class NativeSequence:
    """An owned temporal representation, preserving native compressed bytes.

    Use as a context manager. ``decode`` requires the method's configured
    runtime, while inspection and USDC round trips require no CUDA runtime.
    """
    def __init__(self, path, *, temporary=None, runtime=None, python=None):
        self.path = Path(path).absolute()
        self.manifest = inspect_vmesh(self.path)
        self.codec = self.manifest["codec"]
        self.representation = self.manifest["representation"]
        self.metadata = MappingProxyType(self.manifest["sequence"].get("metadata", {}))
        self.timestamps = tuple(f["timestamp"] for f in self.manifest["sequence"]["frames"])
        self.frame_indices = tuple(f["frame_index"] for f in self.manifest["sequence"]["frames"])
        self._temporary, self._extracted = temporary, None
        self._runtime, self._python = runtime, python
        self._closed = False

    def __len__(self):
        return len(self.timestamps)

    def __enter__(self):
        self._check_open()
        return self

    def __exit__(self, *_):
        self.close()

    def _check_open(self):
        if self._closed:
            raise ValueError("native sequence is closed")

    def close(self):
        if not self._closed:
            if self._extracted is not None:
                self._extracted.cleanup()
            if self._temporary is not None:
                self._temporary.cleanup()
            self._closed = True

    def unpack(self, destination):
        self._check_open()
        return unpack_vmesh(self.path, destination)

    def _native_directory(self):
        self._check_open()
        if self._extracted is None:
            temporary = tempfile.TemporaryDirectory(prefix=f"open4d-{self.codec}-native-")
            try:
                unpack_vmesh(self.path, Path(temporary.name) / "native")
            except BaseException:
                temporary.cleanup()
                raise
            self._extracted = temporary
        return Path(self._extracted.name) / "native"

    def _settings(self, runtime, python):
        variable = "OPEN4D_" + self.codec.upper()
        root = Path(runtime or self._runtime or os.environ.get(variable + "_ROOT") or
                    Path(__file__).parent / "reconstruction" / self.codec).absolute()
        executable = str(python or self._python or os.environ.get(variable + "_PYTHON") or sys.executable)
        return root, executable

    def decode(self, *, runtime=None, python=None):
        """Evaluate actual temporal models, returning Gaussian or field frames.

        Native QUEEN pickles and ReRF checkpoints are executable research
        formats. Call this only for trusted native runs, in their configured
        runtime. Generic inspection/load/save never deserializes these models.
        """
        source = self._native_directory()
        root, executable = self._settings(runtime, python)
        if self.codec == "vega":
            from .gaussians import VegaRun
            # Frame appearances retain this owner so its extraction outlives decode.
            run = VegaRun(source, root, executable, _owner=self)
            return run.decode()
        if self.codec not in NEURAL_CODECS:
            from .codec import decode_sequence
            return decode_sequence(self.path)
        with tempfile.TemporaryDirectory(prefix=f"open4d-{self.codec}-decode-") as directory:
            work = Path(directory)
            self._worker("decode", source, work, root, executable)
            paths = sorted(work.glob("frame_*.npz"))
            if [p.name for p in paths] != [f"frame_{i:06d}.npz" for i in range(len(self))]:
                raise CodecError(f"{self.codec} did not decode every native frame")
            result = []
            for index, path in enumerate(paths):
                with np.load(path, allow_pickle=False) as data:
                    if self.codec == "rerf":
                        values = {name: data[name] for name in ("density", "features", "xyz_min", "xyz_max")}
                        if not all(np.isfinite(v).all() for v in values.values()):
                            raise CodecError("ReRF decoded nonfinite field values")
                        result.append(NeuralFieldFrame(self.frame_indices[index], self.timestamps[index], **values))
                    else:
                        from .gaussians import GaussianSplats
                        result.append(GaussianSplats(**{name: data[name] for name in
                            ("positions", "scales", "rotations", "opacities", "spherical_harmonics")}))
            return tuple(result)

    def _worker(self, operation, source, work, root, executable, **options):
        request = dict(operation=operation, codec=self.codec, source=str(source), output=str(work), runtime=str(root), **options)
        path = work / "request.json"
        path.write_text(json.dumps(request, allow_nan=False))
        run([executable, str(Path(__file__).with_name("_native_worker.py")), str(path)],
            f"{self.codec} {operation}", cwd=work)

    def render(self, camera, *, runtime=None, python=None):
        """Render a ReRF sequence from an explicit calibrated camera dictionary."""
        if self.codec != "rerf":
            raise TypeError("NativeSequence.render currently accepts ReRF; Gaussian decode returns splat arrays")
        root, executable = self._settings(runtime, python)
        with tempfile.TemporaryDirectory(prefix="open4d-rerf-render-") as directory:
            work = Path(directory)
            self._worker("render", self._native_directory(), work, root, executable,
                         camera=_json_value(camera, "camera"))
            paths = sorted(work.glob("image_*.npy"))
            if len(paths) != len(self):
                raise CodecError("ReRF did not render every frame")
            return tuple(np.load(p, allow_pickle=False) for p in paths)


def import_native(source, *, codec=None, config=None, initial_model=None, ntc_config=None,
                  fps=30.0, timestamps=None, frame_indices=None, metadata=None,
                  frame_metadata=None, runtime=None, python=None):
    """Import Vega, QUEEN, 3DGStream or ReRF native output, excluding dense exports.

    ``source`` can also be a GaussianRun/VegaRun. All dependencies are copied
    into an owned .vmesh, so the returned object is independent of the run.
    """
    if hasattr(source, "path"):
        codec = codec or getattr(source, "method", "vega")
        config = config if config is not None else getattr(source, "config", None)
        initial_model = initial_model or getattr(source, "initial_model", None)
        python = python or getattr(source, "python", None)
        source_runtime = getattr(source, "runtime", None)
        if source_runtime is not None:
            runtime = runtime or (source_runtime if codec == "vega" else source_runtime.parent / codec)
        source = source.path
    if codec not in NEURAL_CODECS:
        raise ValueError(f"codec must be one of {sorted(NEURAL_CODECS)}")
    source = Path(source).absolute()
    if isinstance(fps, bool) or not isinstance(fps, (float, int)) or not math.isfinite(fps) or fps <= 0:
        raise ValueError("fps must be finite and positive")
    temporary = tempfile.TemporaryDirectory(prefix=f"open4d-import-{codec}-")
    try:
        root = Path(temporary.name)
        native = root / "native"
        native.mkdir()
        indices, profile = _stage_native(source, native, codec, config, initial_model, ntc_config)
        count = len(indices)
        times = list(timestamps) if timestamps is not None else [i / fps for i in range(count)]
        ids = list(frame_indices) if frame_indices is not None else indices
        metas = list(frame_metadata) if frame_metadata is not None else [{} for _ in indices]
        if not len(times) == len(ids) == len(metas) == count:
            raise ValueError("timestamps, frame_indices and frame_metadata must match the native frame count")
        record = dict(version=1, codec=codec, native=profile, metadata=_json_value(metadata or {}, "sequence"),
                      frames=[dict(frame_index=i, timestamp=t, metadata=_json_value(m, "frame")) for i, t, m in zip(ids, times, metas)],
                      allow_nonmonotonic_timestamps=False)
        (native / "metadata.json").write_text(json.dumps(record, allow_nan=False))
        artifact = pack_vmesh(native, root / "sequence.vmesh")
        shutil.rmtree(native)
        return NativeSequence(artifact, temporary=temporary, runtime=runtime, python=python)
    except BaseException:
        temporary.cleanup()
        raise


def save_native(sequence, destination, *, overwrite=False):
    """Save native temporal state as V3C .vmesh or a self-contained O4D USD."""
    if not isinstance(sequence, NativeSequence):
        raise TypeError("sequence must be a NativeSequence")
    if not isinstance(overwrite, bool):
        raise TypeError("overwrite must be bool")
    sequence._check_open()
    path = Path(destination).absolute()
    if path.suffix.lower() in (".usd", ".usda", ".usdc"):
        from .io._native_usd import write_native_usd
        return write_native_usd(sequence, path, overwrite=overwrite)
    if path.suffix.lower() != ".vmesh":
        raise ValueError("native output requires .vmesh, .usd, .usda or .usdc")
    if path.exists() and not overwrite:
        raise FileExistsError(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=f".{path.name}-", dir=path.parent) as folder:
        staged = Path(folder) / "sequence.vmesh"
        shutil.copyfile(sequence.path, staged)
        inspect_vmesh(staged)
        publish_file(staged, path, overwrite=overwrite)
    return path
