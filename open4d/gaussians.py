"""Gaussian splat data and native reconstruction runs."""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Sequence

import numpy as np
from numpy.typing import NDArray

from ._files import publish_directory


@dataclass(frozen=True, eq=False)
class _GaussianGeometry:

    positions: NDArray
    scales: NDArray
    rotations: NDArray
    opacities: NDArray

    def __post_init__(self) -> None:
        arrays = {}
        for name in ("positions", "scales", "rotations", "opacities"):
            value = np.asarray(getattr(self, name))
            if value.dtype.kind not in "fiu":
                raise TypeError(f"{name} must contain real numbers")
            with np.errstate(over="ignore"):
                value = np.asarray(value, dtype=np.float32)
            if not np.isfinite(value).all():
                raise ValueError(f"{name} must contain finite float32 values")
            arrays[name] = value
        positions = arrays["positions"]
        if positions.ndim != 2 or positions.shape[1:] != (3,):
            raise ValueError("positions must have shape (N, 3)")
        count = len(positions)
        for name, shape in (("scales", (count, 3)), ("rotations", (count, 4)),
                            ("opacities", (count,))):
            if arrays[name].shape != shape:
                raise ValueError(f"{name} must have shape {shape}")
        if np.any(arrays["scales"] <= 0):
            raise ValueError("scales must be positive")
        if np.any((arrays["opacities"] < 0) | (arrays["opacities"] > 1)):
            raise ValueError("opacities must be between 0 and 1")
        norms = np.linalg.norm(arrays["rotations"].astype(np.float64), axis=1)
        if np.any(norms == 0):
            raise ValueError("rotations must have nonzero length")
        arrays["rotations"] = (arrays["rotations"] / norms[:, None]).astype(np.float32)
        for name, value in arrays.items():
            object.__setattr__(self, name, value)

    def __len__(self) -> int:
        return len(self.positions)


@dataclass(frozen=True, eq=False)
class GaussianSplats(_GaussianGeometry):
    """One frame: linear scales and opacity, wxyz rotations, and (N, K, 3) SH.

    SH coefficients use the Graphdeco convention. Arrays are float32; rotations
    are normalized. These values do not imply correspondence between frames.
    """

    spherical_harmonics: NDArray

    def __post_init__(self) -> None:
        super().__post_init__()
        sh = np.asarray(self.spherical_harmonics)
        if sh.dtype.kind not in "fiu":
            raise TypeError("spherical_harmonics must contain real numbers")
        with np.errstate(over="ignore"):
            sh = np.asarray(sh, dtype=np.float32)
        if not np.isfinite(sh).all():
            raise ValueError("spherical_harmonics must contain finite float32 values")
        if sh.ndim != 3 or sh.shape[0] != len(self) or sh.shape[2] != 3:
            raise ValueError("spherical_harmonics must have shape (N, K, 3)")
        degree = int(np.sqrt(sh.shape[1])) - 1
        if degree < 0 or (degree + 1) ** 2 != sh.shape[1]:
            raise ValueError("SH coefficient count must be a positive square")
        object.__setattr__(self, "spherical_harmonics", sh)

    @property
    def sh_degree(self) -> int:
        return int(np.sqrt(self.spherical_harmonics.shape[1])) - 1


def load_gaussians(path: str | Path) -> GaussianSplats:
    """Read a Graphdeco-style Gaussian PLY, including its colour coefficients."""
    try:
        from plyfile import PlyData
    except ModuleNotFoundError as exc:
        if exc.name != "plyfile":
            raise
        raise ImportError("Reading splats requires pip install 'open4d[gaussians]'") from exc

    vertex = PlyData.read(Path(path))["vertex"].data
    names = set(vertex.dtype.names or ())
    required = {"x", "y", "z", "opacity"}
    required.update(f"{prefix}_{i}" for prefix, count in
                    (("scale", 3), ("rot", 4), ("f_dc", 3)) for i in range(count))
    if missing := required - names:
        raise ValueError(f"Not a Gaussian PLY; missing: {', '.join(sorted(missing))}")
    rest_names = [name for name in names if name.startswith("f_rest_")]
    expected = [f"f_rest_{i}" for i in range(len(rest_names))]
    if set(rest_names) != set(expected) or len(expected) % 3:
        raise ValueError("Gaussian PLY has incomplete SH coefficients")

    def columns(fields: Sequence[str]) -> NDArray:
        return np.column_stack([vertex[name] for name in fields]).astype(np.float64)

    positions = columns(("x", "y", "z"))
    dc = columns([f"f_dc_{i}" for i in range(3)])[:, None, :]
    rest = (columns(expected).reshape(len(vertex), 3, len(expected) // 3)
            .transpose(0, 2, 1)) if expected else np.empty((len(vertex), 0, 3))
    raw_opacity = np.asarray(vertex["opacity"], dtype=np.float64)
    with np.errstate(over="ignore", under="ignore"):
        scales = np.exp(columns([f"scale_{i}" for i in range(3)]))
        opacity = np.exp(-np.logaddexp(0, -raw_opacity))
    return GaussianSplats(
        positions=positions,
        scales=scales,
        rotations=columns([f"rot_{i}" for i in range(4)]),
        opacities=opacity,
        spherical_harmonics=np.concatenate((dc, rest), axis=1),
    )


def _runtime(path: str | Path | None) -> Path:
    candidate = path or os.environ.get("OPEN4D_GS_ROOT")
    root = (Path(candidate).expanduser() if candidate else
            Path(__file__).parent / "reconstruction" / "gs_tools").resolve()
    if not (root / "gs_tools" / "cli.py").is_file():
        raise FileNotFoundError(
            f"Gaussian runtime not found at {root}. Pass runtime pointing to "
            "open4d/reconstruction/gs_tools in a configured source checkout."
        )
    return root


def _run(runtime: Path, python: str, arguments: Sequence[str]) -> None:
    child_env = dict(os.environ, PYTHONPATH=str(runtime), OPEN4D_GS_ROOT=str(runtime))
    subprocess.run([python, "-m", "gs_tools.cli", *arguments], cwd=runtime,
                   env=child_env, check=True)


@dataclass(frozen=True)
class GaussianRun:
    """A native QUEEN or 3DGStream run, including its encoded dependencies."""

    method: str
    path: Path
    source: Path
    runtime: Path
    python: str
    config: Path | None = None
    initial_model: Path | None = None

    def __post_init__(self) -> None:
        if self.method not in ("queen", "3dgstream"):
            raise ValueError("method must be 'queen' or '3dgstream'")
        for name in ("path", "source", "runtime", "config", "initial_model"):
            value = getattr(self, name)
            if value is not None:
                object.__setattr__(self, name, Path(value).expanduser().resolve())
        object.__setattr__(self, "python", str(self.python))

    @property
    def frame_paths(self) -> tuple[Path, ...]:
        """Dense PLY exports, in frame order; not decoded compressed residuals."""
        if self.method == "queen":
            frames = sorted((path for path in (self.path / "frames").glob("[0-9]*")
                             if path.is_dir() and path.name.isdigit()),
                            key=lambda path: int(path.name))
        else:
            frames = sorted((path for path in self.path.glob("frame[0-9]*")
                             if path.is_dir() and path.name[5:].isdigit()),
                            key=lambda path: int(path.name[5:]))
        result = []
        if self.method == "3dgstream" and self.initial_model is not None:
            result.append(self.initial_model)
        for frame in frames:
            canonical = frame / "point_cloud.ply"
            if canonical.is_file():
                result.append(canonical)
                continue
            snapshots = [path for path in (frame / "point_cloud").glob("iteration_*")
                         if path.name[10:].isdigit() and (path / "point_cloud.ply").is_file()]
            if not snapshots:
                raise FileNotFoundError(f"No dense Gaussian PLY was saved for {frame}")
            result.append(max(snapshots, key=lambda path: int(path.name[10:])) / "point_cloud.ply")
        return tuple(result)

    def load_frame(self, index: int) -> GaussianSplats:
        """Read a saved dense frame without importing the CUDA training runtime."""
        return load_gaussians(self.frame_paths[index])

    def render(self, *, compressed: bool = True, options: Sequence[str] = ()) -> Path:
        """Render QUEEN's camera path to PNG frames and MP4 using its runtime."""
        if self.method != "queen":
            raise NotImplementedError("3DGStream rendering requires its separate native viewer")
        command = ["render", "--method", self.method, "-s", str(self.source),
                   "-m", str(self.path)]
        if self.config is not None:
            command += ["--config", str(self.config)]
        if not compressed:
            command.append("--dense")
        command += ["--", *_options(options)]
        _run(self.runtime, self.python, command)
        result = self.path / ("spiral_compressed" if compressed else "spiral_rendered")
        if not (result / "output.mp4").is_file():
            raise RuntimeError(f"QUEEN did not produce {result / 'output.mp4'}")
        return result / "output.mp4"


def _options(options: Sequence[str]) -> tuple[str, ...]:
    if isinstance(options, (str, bytes)):
        raise TypeError("options must be a sequence of command-line strings")
    options = tuple(options)
    if not all(isinstance(item, str) for item in options):
        raise TypeError("options must be a sequence of command-line strings")
    managed = ("--source_path", "--model_path", "--output_path", "--video_path",
               "--config", "--config_path", "--read_config")
    for item in options:
        flag = item.split("=", 1)[0]
        if (flag == "--" or (flag.startswith("--") and any(name.startswith(flag) for name in managed))
                or (item.startswith("-") and not item.startswith("--") and item[1:2] in ("s", "m", "o", "v"))):
            raise ValueError(f"options cannot override managed paths or config: {item}")
    return options


def reconstruct_gaussians(
    source: str | Path,
    output: str | Path,
    *,
    method: str = "queen",
    runtime: str | Path | None = None,
    python: str | Path | None = None,
    config: str | Path | None = None,
    options: Sequence[str] = (),
    initial_model: str | Path | None = None,
    initial_iterations: int = 15000,
    ntc: str | Path | None = None,
) -> GaussianRun:
    """Reconstruct calibrated multiview video with QUEEN or 3DGStream.

    Install the native method's CUDA dependencies in ``python`` first. ``runtime``
    points to the checkout's gs_tools directory. The result keeps each method's
    native files. ``options`` contains extra upstream training arguments.
    """
    if method not in ("queen", "3dgstream"):
        raise ValueError("method must be 'queen' or '3dgstream'")
    extras = _options(options)
    source, output = Path(source).expanduser().resolve(), Path(output).expanduser().resolve()
    if not source.is_dir():
        raise FileNotFoundError(source)
    if output.exists():
        raise FileExistsError(f"Output already exists: {output}")
    root = _runtime(runtime)
    executable = str(python or sys.executable)
    config_path = Path(config).expanduser().resolve() if config else None
    if config_path is not None and not config_path.is_file():
        raise FileNotFoundError(config_path)
    common = ["train", "--method", method, "-s", str(source), "-m", str(output)]
    if config_path is not None:
        common += ["--config", str(config_path)]
    initial_ply = None
    commands = []
    if method == "queen":
        if initial_model is not None or ntc is not None:
            raise ValueError("initial_model and ntc apply only to 3DGStream")
        commands.append([*common, "--", "--log_ply", "--log_compressed", *extras])
    else:
        if isinstance(initial_iterations, bool) or not isinstance(initial_iterations, int) or initial_iterations < 1:
            raise ValueError("initial_iterations must be a positive integer")
        if not (source / "frame000000").is_dir():
            raise FileNotFoundError("3DGStream needs a calibrated frame000000 directory")
        frame_count = sum(path.is_dir() and path.name[5:].isdigit() for path in source.glob("frame[0-9]*"))
        if frame_count < 2:
            raise ValueError("3DGStream needs frame000000 and at least one later frame")
        cache = (Path(ntc).expanduser().resolve() if ntc else
                 root.parent / "3dgstream" / "ntc" / "flame_steak_ntc_params_F_4.pth")
        if not cache.is_file():
            raise FileNotFoundError(f"3DGStream NTC initialization checkpoint not found: {cache}")
        init_dir = Path(initial_model).expanduser().resolve() if initial_model else output / "init"
        initial_ply = init_dir / "point_cloud" / f"iteration_{initial_iterations}" / "point_cloud.ply"
        if initial_model is not None and not initial_ply.is_file():
            raise FileNotFoundError(initial_ply)
        if initial_model is None:
            commands.append([*common, "--stage", "init", "--", "--iterations",
                             str(initial_iterations), "--save_iterations", str(initial_iterations)])
        commands.append([*common, "--stage", "frames", "--init", str(init_dir),
                         "--first-load-iteration", str(initial_iterations), "--ntc-path", str(cache),
                         "--frame-end", str(frame_count), "--", *extras])
    output.mkdir(parents=True)
    try:
        for command in commands:
            _run(root, executable, command)
            if (method == "3dgstream" and command[command.index("--stage") + 1] == "init"
                    and not initial_ply.is_file()):
                raise RuntimeError(f"3DGStream did not save its initial model: {initial_ply}")
        result = GaussianRun(method, output, source, root, executable, config_path, initial_ply)
        paths = result.frame_paths
        if not paths or (method == "3dgstream" and len(paths) != frame_count):
            raise RuntimeError(f"{method} finished without saving every Gaussian frame in {output}")
        return result
    except BaseException:
        shutil.rmtree(output)
        raise


def _vega_runtime(path: str | Path | None) -> Path:
    root = Path(path or os.environ.get("OPEN4D_VEGA_ROOT") or
                Path(__file__).parent / "reconstruction" / "vega").expanduser().resolve()
    if not (root / "vega" / "encoder.py").is_file():
        raise FileNotFoundError(f"Vega runtime not found at {root}; pass runtime pointing to its source directory")
    return root


def _vega_command(runtime: Path, python: str, request: dict, work: Path) -> None:
    request_path = work / "request.json"
    request_path.write_text(json.dumps(request), encoding="utf-8")
    subprocess.run([python, str(Path(__file__).with_name("_gaussian_worker.py")),
                    str(runtime), str(request_path)], cwd=runtime,
                   env=dict(os.environ, PYTHONPATH=str(runtime)), check=True)


@dataclass(frozen=True)
class NeuralAppearance:
    """Vega's retained colour model for a decoded frame."""

    run: VegaRun
    frame_index: int

    def colors(self, directions: NDArray) -> NDArray:
        """Evaluate RGB for unit camera-to-splat directions, one per splat."""
        values = np.asarray(directions, dtype=np.float32)
        if values.ndim != 2 or values.shape[1] != 3 or not np.isfinite(values).all():
            raise ValueError("directions must have shape (N, 3) and finite values")
        norms = np.linalg.norm(values.astype(np.float64), axis=1)
        if np.any(norms == 0):
            raise ValueError("directions must have nonzero length")
        values = (values / norms[:, None]).astype(np.float32)
        with tempfile.TemporaryDirectory(prefix="open4d-vega-") as folder:
            work = Path(folder)
            np.save(work / "directions.npy", values)
            _vega_command(self.run.runtime, self.run.python,
                          {"operation": "colors", "source": str(self.run.path),
                           "output": str(work), "frame": self.frame_index}, work)
            result = np.load(work / "colors.npy", allow_pickle=False)
        if result.shape != values.shape or not np.isfinite(result).all():
            raise RuntimeError("Vega returned invalid colour values")
        return result


@dataclass(frozen=True, eq=False)
class NeuralGaussianFrame(_GaussianGeometry):
    """Decoded Gaussian geometry whose view-dependent colour comes from a model."""

    appearance: NeuralAppearance


@dataclass(frozen=True)
class VegaRun:
    """A native Vega bitstream directory; its colour model stays with the data."""

    path: Path
    runtime: Path
    python: str
    _owner: object = field(default=None, repr=False, compare=False)

    def __post_init__(self) -> None:
        object.__setattr__(self, "path", Path(self.path).expanduser().resolve())
        object.__setattr__(self, "runtime", Path(self.runtime).expanduser().resolve())
        object.__setattr__(self, "python", str(self.python))

    def decode(self) -> tuple[NeuralGaussianFrame, ...]:
        """Decode a native bitstream with the configured CUDA runtime."""
        from ._gaussian_worker import _read_manifest

        entries = _read_manifest(self.path)
        with tempfile.TemporaryDirectory(prefix="open4d-vega-") as folder:
            work = Path(folder)
            _vega_command(self.runtime, self.python,
                          {"operation": "decode", "source": str(self.path), "output": str(work)}, work)
            paths = sorted(work.glob("frame_*.npz"))
            if [path.name for path in paths] != [f"frame_{index:06d}.npz" for index in range(len(entries))]:
                raise RuntimeError("Vega did not decode every frame")
            frames = []
            for path in paths:
                with np.load(path, allow_pickle=False) as data:
                    frames.append(NeuralGaussianFrame(
                        **{name: data[name] for name in ("positions", "scales", "rotations", "opacities")},
                        appearance=NeuralAppearance(self, int(path.stem[6:])),
                    ))
            if not frames:
                raise RuntimeError("Vega decoded no frames")
            return tuple(frames)


def encode_gaussians(
    frames: Sequence[GaussianSplats], output: str | Path, *, codec: str = "vega",
    runtime: str | Path | None = None, python: str | Path | None = None,
    key_iterations: int = 300, residual_iterations: int = 150,
) -> VegaRun:
    """Encode splat frames with the local Vega adaptation and its CUDA runtime.

    Each frame is treated as one object. The native writer stores one colour
    model; runs that require multiple groups are rejected instead of saving
    earlier frames with the wrong model.
    """
    if codec != "vega":
        raise ValueError("Gaussian array encoding currently supports codec='vega'")
    frames = tuple(frames)
    if len(frames) < 2:
        raise ValueError("Vega encoding needs at least two frames")
    if not all(isinstance(frame, GaussianSplats) and len(frame) for frame in frames):
        raise ValueError("frames must contain nonempty GaussianSplats")
    if any(frame.sh_degree > 3 for frame in frames):
        raise ValueError("Vega supports SH degrees 0 through 3")
    if any(np.any((frame.opacities <= 0) | (frame.opacities >= 1)) for frame in frames):
        raise ValueError("Vega training requires opacities strictly between 0 and 1")
    for value in (key_iterations, residual_iterations):
        if isinstance(value, bool) or not isinstance(value, int) or value < 1:
            raise ValueError("training iteration counts must be positive integers")
    destination = Path(output).expanduser().resolve()
    if destination.exists():
        raise FileExistsError(destination)
    root, executable = _vega_runtime(runtime), str(python or sys.executable)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".open4d-vega-", dir=destination.parent) as folder:
        work = Path(folder)
        for index, frame in enumerate(frames):
            np.savez(work / f"frame_{index:06d}.npz", **{
                name: getattr(frame, name) for name in
                ("positions", "scales", "rotations", "opacities", "spherical_harmonics")
            })
        staged = work / "encoded"
        _vega_command(root, executable,
                      {"operation": "encode", "source": str(work), "output": str(staged),
                       "key_iterations": key_iterations, "residual_iterations": residual_iterations}, work)
        from ._gaussian_worker import _read_manifest

        _read_manifest(staged, expected_count=len(frames))
        publish_directory(staged, destination)
    return VegaRun(destination, root, executable)


def decode_gaussians(
    source: str | Path, *, codec: str = "vega",
    runtime: str | Path | None = None, python: str | Path | None = None,
) -> tuple[NeuralGaussianFrame, ...]:
    """Decode a Vega bitstream, preserving its neural colour model."""
    if codec != "vega":
        raise ValueError("Gaussian array decoding currently supports codec='vega'")
    source = Path(source).expanduser().resolve()
    if not (source / "manifest.json").is_file() or not (source / "color_model.pt").is_file():
        raise FileNotFoundError(f"No native Vega bitstream at {source}")
    return VegaRun(source, _vega_runtime(runtime), str(python or sys.executable)).decode()


class _VegaCodec:
    id = "vega"
    suffixes = (".vega", ".vmesh")
    representation = "gaussian_splats"
    backend = "research-subprocess"
    lossless = False
    preserves = ("positions", "scales", "rotations", "opacities", "neural_appearance")

    def can_decode(self, source):
        if Path(source).suffix.lower() == ".vmesh":
            from .codec._v3c import probe_codec
            return probe_codec(source) == self.id
        return Path(source).is_dir() and (Path(source) / "manifest.json").is_file()

    def encode(self, sequence, destination: Path, **options) -> Path:
        if destination.suffix.lower() == ".vmesh":
            from .native import NativeSequence, import_native, save_native
            from .codec._native_temporal import encode_native
            if isinstance(sequence, (NativeSequence, VegaRun, str, os.PathLike)):
                return encode_native(sequence, destination, codec="vega", **options)
            overwrite = options.pop("overwrite", False)
            if destination.exists() and not overwrite:
                raise FileExistsError(destination)
            timeline = {key: options.pop(key) for key in ("fps", "timestamps", "frame_indices", "metadata", "frame_metadata") if key in options}
            with tempfile.TemporaryDirectory(prefix="open4d-vega-encode-") as folder:
                encoded = encode_gaussians(sequence, Path(folder) / "native", **options)
                with import_native(encoded, **timeline) as native:
                    return save_native(native, destination, overwrite=overwrite)
        if "overwrite" in options:
            if options.pop("overwrite"):
                raise ValueError("Vega directory encoding does not support overwrite; use a new directory or .vmesh")
        return encode_gaussians(sequence, destination, **options).path

    def decode(self, source: Path, **options) -> tuple[NeuralGaussianFrame, ...]:
        if source.suffix.lower() == ".vmesh":
            from .codec._native_temporal import NativeTemporalCodec
            return NativeTemporalCodec("vega").decode(source, **options)
        return decode_gaussians(source, **options)


VEGA_CODEC = _VegaCodec()
