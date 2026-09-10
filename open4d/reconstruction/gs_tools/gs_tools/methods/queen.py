"""QUEEN training and camera-path rendering commands."""

from __future__ import annotations

import sys
from pathlib import Path

from .. import paths
from .base import RunSpec, run

name = "queen"
upstream = "queen"


def _config(spec: RunSpec) -> Path:
    """The run config, defaulting to upstream's DyNeRF one."""
    if spec.config is not None:
        return Path(spec.config).resolve()
    return paths.upstream_configs("queen") / "dynerf.yaml"


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


def train(spec: RunSpec) -> int:
    return run(sys.modules[__name__], spec, train_command(spec), verb="train")


def render(spec: RunSpec, *, compressed: bool = True) -> int:
    command = render_command(spec, compressed=compressed)
    return run(sys.modules[__name__], spec, command, verb="render")
