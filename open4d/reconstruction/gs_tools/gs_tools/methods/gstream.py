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

import json
import shutil
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
    #: Image subdirectory inside each timestep. Left unset by default: upstream's
    #: own default applies, and DyNeRF-derived scenes keep their views directly in
    #: `frameNNNNNN/` with no subdirectory at all, so passing one breaks the load.
    images: str | None = None
    #: Must match the iteration the initial 3DGS was saved at.
    first_load_iteration: int = 15000
    #: Timestep range, 1-based and half-open at the end, as upstream defines it.
    #: None leaves upstream's own defaults (1 to 150) in place.
    frame_start: int | None = None
    frame_end: int | None = None
    #: 1, not upstream's argparse default of 3. 3DGStream's README requires the
    #: initial 3DGS to be trained at `--sh_degree 1`, and the frames stage loads
    #: that ply with an assertion on the spherical-harmonic count -- so leaving
    #: the default in place fails inside `load_ply` with a bare AssertionError.
    sh_degree: int = 1
    ntc_path: Path | None = None
    ntc_conf_path: Path | None = None
    extra: tuple[str, ...] = field(default_factory=tuple)

    def resolved_init(self, run_dir: Path) -> Path:
        return Path(self.init_dir).resolve() if self.init_dir else (run_dir / "init").resolve()


#: The hash-grid configuration matching the NTC checkpoint upstream ships
#: (`ntc/flame_steak_ntc_params_F_4.pth`), and the paper's default.
DEFAULT_NTC_CONF = "cache/cache_F_4.json"


def _config(spec: RunSpec) -> Path:
    """3DGStream's config is a JSON dump of its own argparse namespace.

    Upstream ships no config for DyNeRF, only a run's dump in
    `test/flame_steak_suite/cfg_args.json`, and its argument defaults are a
    static 3DGS schedule: 30,000 iterations for every frame. The default here is
    that dump's training hyperparameters -- 150 + 100 iterations per frame, as in
    the paper -- without its dataset paths, image folder or frame range. The
    config supplies defaults; the arguments this adapter passes win over it.
    """
    if spec.config is not None:
        return Path(spec.config).resolve()
    return paths.module_root() / "configs" / "3dgstream" / "paper.json"


def _ntc_conf(options: GstreamOptions) -> Path:
    """The NTC hash-grid configuration for the frames stage.

    Upstream's default is the empty string, which fails at NTC construction, so
    a default that matches the shipped checkpoint is more useful than none.
    """
    ntc_conf = options.ntc_conf_path or paths.upstream_configs("3dgstream") / DEFAULT_NTC_CONF
    return Path(ntc_conf).resolve()


def init_command(spec: RunSpec, options: GstreamOptions) -> list[str]:
    """Stage one: the static 3DGS for timestep 0, at sh_degree 1 as upstream requires.

    When the frames-stage config holds out test views (``eval``), stage one holds
    out the same ones, so no frame is trained on a view it is scored against.
    """
    frame0 = spec.scene / "frame000000"
    source = frame0 if frame0.exists() else spec.scene
    holds_out = bool(json.loads(_config(spec).read_text(encoding="utf-8")).get("eval"))
    return [
        sys.executable,
        "train.py",
        "-s",
        str(source.resolve()),
        "-m",
        str(options.resolved_init(spec.run_dir)),
        "--sh_degree",
        str(options.sh_degree),
        *(["--eval"] if holds_out else []),
        *options.extra,
        *spec.passthrough,
    ]


def train_command(spec: RunSpec, options: GstreamOptions | None = None) -> list[str]:
    """Stage two: per-timestep training over the rest of the sequence."""
    options = options or GstreamOptions()
    command = [
        sys.executable,
        "train_frames.py",
        "--read_config",
        "--config_path",
        str(_config(spec)),
        "-o",
        str(spec.run_dir.resolve()),
        "-m",
        str(options.resolved_init(spec.run_dir)),
        "-v",
        str(spec.scene.resolve()),
        "--first_load_iteration",
        str(options.first_load_iteration),
        "--sh_degree",
        str(options.sh_degree),
    ]
    if options.images:
        command += ["--image", options.images]
    if options.frame_start is not None:
        command += ["--frame_start", str(options.frame_start)]
    if options.frame_end is not None:
        command += ["--frame_end", str(options.frame_end)]
    if options.ntc_path:
        command += ["--ntc_path", str(Path(options.ntc_path).resolve())]
    command += ["--ntc_conf_path", str(_ntc_conf(options))]
    return command + [*options.extra, *spec.passthrough]


def render_command(spec: RunSpec) -> list[str]:
    raise NotImplementedError("3DGStream rendering requires its separate native viewer")


def train(spec: RunSpec, options: GstreamOptions | None = None, *, stage: str = "frames") -> int:
    options = options or GstreamOptions()
    module = sys.modules[__name__]
    if stage == "init":
        return run(module, spec, init_command(spec, options), verb="train")
    status = run(module, spec, train_command(spec, options), verb="train")
    if status == 0 and not spec.dry_run:
        # Native import reads the NTC architecture from the run, never from the
        # paths recorded in cfg_args.json, so keep the one this run used.
        shutil.copyfile(_ntc_conf(options), spec.run_dir / "ntc_config.json")
    return status


def render(spec: RunSpec) -> int:
    return run(sys.modules[__name__], spec, render_command(spec), verb="render")
