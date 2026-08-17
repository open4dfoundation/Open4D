"""QUEEN, wrapped.

Upstream's interface is close to what Open4D wants already:

    python train.py --config configs/dynerf.yaml -s <scene> -m <run_dir>
    python render.py -s <scene> -m <run_dir>
    python metrics_video.py -m <run_dir>

so the translation is mostly path resolution. Two things need care: the config
must be an absolute path because the child runs with cwd set to the checkout, and
`render_fvv_compressed.py` -- not `render.py` -- is what evaluates the compressed
representation, which is the interesting output of this method.
"""

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
    """Render from the trained model.

    `compressed=True` runs the decode-side path, which is the one that
    corresponds to what a viewer would receive; the dense path is the fallback
    when a run predates quantization.
    """
    if compressed:
        return [
            sys.executable,
            "render_fvv_compressed.py",
            "--config",
            str(_config(spec)),
            "-s",
            str(spec.scene.resolve()),
            "-m",
            str(spec.run_dir.resolve()),
            *spec.passthrough,
        ]
    return [
        sys.executable,
        "render.py",
        "-s",
        str(spec.scene.resolve()),
        "-m",
        str(spec.run_dir.resolve()),
        *spec.passthrough,
    ]


def train(spec: RunSpec) -> int:
    return run(sys.modules[__name__], spec, train_command(spec), verb="train")


def render(spec: RunSpec, *, compressed: bool = True) -> int:
    command = render_command(spec, compressed=compressed)
    return run(sys.modules[__name__], spec, command, verb="render")
