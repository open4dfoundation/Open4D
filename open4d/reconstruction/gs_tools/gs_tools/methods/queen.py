"""QUEEN training and camera-path rendering commands."""

from __future__ import annotations

import sys
from pathlib import Path

from .. import paths
from .base import RunSpec, run

name = "queen"
upstream = "queen"


#: Written into the run directory when no config is given, or when the held-out
#: cameras change, so rendering and native import read the configuration the
#: run was trained with.
DEFAULT_CONFIG_NAME = "queen_config.yaml"


def _config(spec: RunSpec) -> Path:
    """The run config, defaulting to upstream's DyNeRF one without depth priors.

    Upstream's dynerf.yaml sets depth_init, which imports MiDaS and therefore
    timm==0.6.13; the training environment deliberately has neither, and
    `gs-tools depth-prior` is not wired up. The default keeps every other
    upstream value. Pass upstream's dynerf.yaml explicitly where MiDaS exists.
    A config written into the run by `train` takes precedence.
    """
    derived = spec.run_dir.resolve() / DEFAULT_CONFIG_NAME
    if derived.is_file():
        return derived
    if spec.config is not None:
        return Path(spec.config).resolve()
    if not spec.run_dir.exists():
        return derived
    # A run trained before the default was derived used upstream's file directly.
    return paths.upstream_configs("queen") / "dynerf.yaml"


def _write_config(spec: RunSpec, test_indices: list[int] | None, *, depth_priors: bool = False) -> None:
    """Write the run's config: the given one, or the default, with held-out cameras.

    QUEEN declares ``test_indices`` as a list without ``nargs``, so it cannot be
    set on the command line; the config file is the only way to change it.
    """
    import yaml

    source = Path(spec.config) if spec.config is not None else paths.upstream_configs("queen") / "dynerf.yaml"
    config = yaml.safe_load(source.read_text()) or {}
    model = config.setdefault("model_params", {})
    if spec.config is None and not depth_priors:
        model.update(depth_init=False, lambda_depth=0.0, lambda_depthssim=0.0)
    if test_indices is not None:
        model["test_indices"] = list(test_indices)
    spec.run_dir.mkdir(parents=True, exist_ok=True)
    (spec.run_dir / DEFAULT_CONFIG_NAME).write_text(yaml.safe_dump(config, sort_keys=False))


def train_command(spec: RunSpec) -> list[str]:
    return [
        sys.executable,
        "train.py",
        "--config",
        str(_config(spec)),
        "-s",
        str(spec.scene.resolve()),
        "-m",
        str(spec.run_dir.resolve()),
        *spec.passthrough,
    ]


def render_command(spec: RunSpec, *, compressed: bool = True) -> list[str]:
    """Render the same camera path from either compressed or dense frames."""
    return [
        sys.executable,
        "render_fvv_compressed.py",
        "--config",
        str(_config(spec)),
        "-s",
        str(spec.scene.resolve()),
        "-m",
        str(spec.run_dir.resolve()),
        *(["--render_compressed"] if compressed else []),
        *spec.passthrough,
    ]


def missing_depth_priors(scene: Path) -> list[str]:
    """Cameras of a QUEEN scene with no cached depth prior."""
    return [path.name for path in sorted(Path(scene).glob("cam[0-9]*"))
            if (path / "images").is_dir() and not (Path(scene) / "depth_priors" / f"{path.name}.npy").is_file()]


def train(spec: RunSpec, *, test_indices: list[int] | None = None, depth_priors: bool = False) -> int:
    if depth_priors:
        missing = missing_depth_priors(spec.scene)
        if missing:
            print(f"no cached depth priors for {', '.join(missing)}; run `gs-tools depth-prior` first")
            return 1
    if (spec.config is None or test_indices is not None) and not spec.dry_run:
        _write_config(spec, test_indices, depth_priors=depth_priors)
    return run(sys.modules[__name__], spec, train_command(spec), verb="train")


def render(spec: RunSpec, *, compressed: bool = True) -> int:
    command = render_command(spec, compressed=compressed)
    return run(sys.modules[__name__], spec, command, verb="render")
