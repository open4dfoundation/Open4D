"""3DGStream, wrapped.

Upstream's interface is further from Open4D's than QUEEN's, because the method is
two-stage and says so in its arguments:

    python train.py -s <frame000000> -m <init_dir> --sh_degree 1
    python train_frames.py --read_config --config_path <cfg> \
        -o <output> -m <init_dir> -v <scene> --image <images> \
        --first_load_iteration <n>

`-m` is the *initial* 3DGS trained on timestep 0, not the run directory -- `-o`
is the run directory. Conflating the two is the easiest mistake to make here, so
the adapter keeps them separate and defaults the initial model to `<run>/init`,
which is where `gs-tools train --stage init` puts it.

Not yet wrapped: NTC warm-up, which upstream ships only as
`scripts/cache_warmup.ipynb`. Pass `--ntc-path` to reuse a warmed cache; a
`gs-tools ntc-warmup` verb needs that notebook turned into a script first.
"""

from __future__ import annotations

import sys
from dataclasses import dataclass, field
from pathlib import Path

from .. import paths
from .base import RunSpec, run

name = "3dgstream"
upstream = "3dgstream"


@dataclass
class GstreamOptions:
    """The arguments 3DGStream needs and QUEEN does not."""

    init_dir: Path | None = None
    images: str = "images"
    first_load_iteration: int = 15000
    ntc_path: Path | None = None
    ntc_conf_path: Path | None = None
    extra: tuple[str, ...] = field(default_factory=tuple)

    def resolved_init(self, run_dir: Path) -> Path:
        return Path(self.init_dir).resolve() if self.init_dir else (run_dir / "init").resolve()


#: The hash-grid configuration matching the NTC checkpoint upstream ships
#: (`ntc/flame_steak_ntc_params_F_4.pth`), and the paper's default.
DEFAULT_NTC_CONF = "cache/cache_F_4.json"


def _config(spec: RunSpec) -> Path | None:
    """3DGStream's config is a JSON dump of its own argparse namespace.

    Upstream ships none for DyNeRF -- `cfg_args.json` is written *by* a run -- so
    unlike QUEEN there is no default to fall back to. Without `--config`, the
    command simply omits `--read_config` and upstream's argument defaults apply.
    """
    return Path(spec.config).resolve() if spec.config is not None else None


def init_command(spec: RunSpec, options: GstreamOptions) -> list[str]:
    """Stage one: the static 3DGS for timestep 0, at sh_degree 1 as upstream requires."""
    frame0 = spec.scene / "frame000000"
    source = frame0 if frame0.exists() else spec.scene
    return [
        sys.executable,
        "train.py",
        "-s",
        str(source.resolve()),
        "-m",
        str(options.resolved_init(spec.run_dir)),
        "--sh_degree",
        "1",
        *options.extra,
        *spec.passthrough,
    ]


def train_command(spec: RunSpec, options: GstreamOptions | None = None) -> list[str]:
    """Stage two: per-timestep training over the rest of the sequence."""
    options = options or GstreamOptions()
    command = [sys.executable, "train_frames.py"]
    config = _config(spec)
    if config is not None:
        command += ["--read_config", "--config_path", str(config)]
    command += [
        "-o",
        str(spec.run_dir.resolve()),
        "-m",
        str(options.resolved_init(spec.run_dir)),
        "-v",
        str(spec.scene.resolve()),
        "--image",
        options.images,
        "--first_load_iteration",
        str(options.first_load_iteration),
    ]
    if options.ntc_path:
        command += ["--ntc_path", str(Path(options.ntc_path).resolve())]
    # Upstream's default is the empty string, which fails at NTC construction, so
    # a default that matches the shipped checkpoint is more useful than none.
    ntc_conf = options.ntc_conf_path or paths.upstream_configs("3dgstream") / DEFAULT_NTC_CONF
    command += ["--ntc_conf_path", str(Path(ntc_conf).resolve())]
    return command + [*options.extra, *spec.passthrough]


def render_command(spec: RunSpec) -> list[str]:
    """Upstream's FVV extraction, which is also how it renders a finished run."""
    return [
        sys.executable,
        "scripts/extract_fvv.py",
        "-o",
        str(spec.run_dir.resolve()),
        *spec.passthrough,
    ]


def train(spec: RunSpec, options: GstreamOptions | None = None, *, stage: str = "frames") -> int:
    options = options or GstreamOptions()
    module = sys.modules[__name__]
    if stage == "init":
        return run(module, spec, init_command(spec, options), verb="train")
    return run(module, spec, train_command(spec, options), verb="train")


def render(spec: RunSpec) -> int:
    return run(sys.modules[__name__], spec, render_command(spec), verb="render")
