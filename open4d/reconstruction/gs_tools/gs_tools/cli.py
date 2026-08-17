"""`gs-tools` -- one entry point for both Gaussian-splatting methods."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from . import env, paths, rast
from .data import layouts
from .io import manifest
from .methods import base, gstream, queen

METHODS = {"queen": queen, "3dgstream": gstream}


def _spec(args: argparse.Namespace) -> base.RunSpec:
    return base.RunSpec(
        scene=Path(args.scene).expanduser(),
        run_dir=Path(args.run).expanduser(),
        config=Path(args.config).expanduser() if getattr(args, "config", None) else None,
        passthrough=tuple(args.passthrough),
        dry_run=getattr(args, "dry_run", False),
    )


def _cmd_doctor(args: argparse.Namespace) -> int:
    status = env.doctor()
    print("\nrasterizers")
    for name, where in rast.probe().items():
        print(f"  {name:<30} {where or 'not built'}")
    return status


def _cmd_data(args: argparse.Namespace) -> int:
    scene = layouts.detect(Path(args.scene).expanduser())
    print(f"{scene.root}\n  {layouts.describe(scene)}")
    if args.layout and args.layout != scene.layout.value:
        print(f"  expected layout={args.layout}, found {scene.layout.value}")
        return 1
    if not scene.prepared:
        print(
            "  not ready to train: "
            + ("no COLMAP cameras found" if scene.colmap is None else "unrecognized layout")
        )
        return 1
    print("  ready")
    return 0


def _cmd_train(args: argparse.Namespace) -> int:
    spec = _spec(args)
    if args.method == "queen":
        return queen.train(spec)
    options = gstream.GstreamOptions(
        init_dir=Path(args.init).expanduser() if args.init else None,
        images=args.images,
        first_load_iteration=args.first_load_iteration,
        ntc_path=Path(args.ntc_path).expanduser() if args.ntc_path else None,
        ntc_conf_path=Path(args.ntc_conf).expanduser() if args.ntc_conf else None,
    )
    return gstream.train(spec, options, stage=args.stage)


def _cmd_render(args: argparse.Namespace) -> int:
    spec = _spec(args)
    if args.method == "queen":
        return queen.render(spec, compressed=not args.dense)
    return gstream.render(spec)


def _cmd_manifest(args: argparse.Namespace) -> int:
    data = manifest.read(Path(args.run).expanduser())
    if not data:
        print(f"no {manifest.MANIFEST_NAME} in {args.run}")
        return 1
    json.dump(data, sys.stdout, indent=2, sort_keys=True)
    print()
    return 0


def _cmd_depth_prior(args: argparse.Namespace) -> int:
    # Deliberately not implemented yet: it runs in the separate open4d-gs-midas
    # environment (timm==0.6.13), and wiring it before phase 1 has trained
    # anything would be guessing at the interface.
    print(
        "depth-prior is not wired up yet -- see README.md 'MiDaS depth priors'.\n"
        f"For now, run upstream directly from {paths.upstream('queen')}."
    )
    return 2


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="gs-tools",
        description="Gaussian-splatting FVV reconstruction (QUEEN, 3DGStream)",
        epilog="Anything after -- is passed through to the upstream trainer unchanged.",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    doctor = sub.add_parser("doctor", help="report the environment and built extensions")
    doctor.set_defaults(func=_cmd_doctor)

    data = sub.add_parser("data", help="inspect a scene directory")
    data.add_argument("-s", "--scene", required=True)
    data.add_argument("--layout", choices=[layout.value for layout in layouts.Layout])
    data.set_defaults(func=_cmd_data)

    depth = sub.add_parser("depth-prior", help="generate MiDaS depth maps (QUEEN)")
    depth.add_argument("-s", "--scene", required=True)
    depth.set_defaults(func=_cmd_depth_prior)

    train = sub.add_parser("train", help="train a method on a scene")
    train.add_argument("--method", required=True, choices=sorted(METHODS))
    train.add_argument("-s", "--scene", required=True)
    train.add_argument("-m", "--run", required=True, help="run directory (output)")
    train.add_argument("--config")
    train.add_argument(
        "--stage",
        default="frames",
        choices=("init", "frames"),
        help="3DGStream only: 'init' trains the timestep-0 model first",
    )
    train.add_argument("--init", help="3DGStream only: initial 3DGS dir (default <run>/init)")
    train.add_argument("--images", default="images", help="3DGStream only: image subdirectory")
    train.add_argument("--first-load-iteration", type=int, default=15000, dest="first_load_iteration")
    train.add_argument("--ntc-path", dest="ntc_path", help="3DGStream only: warmed NTC parameters")
    train.add_argument("--ntc-conf", dest="ntc_conf", help="3DGStream only: NTC config")
    train.add_argument(
        "--dry-run",
        action="store_true",
        dest="dry_run",
        help="print the translated upstream command without running it",
    )
    train.add_argument("passthrough", nargs="*", help=argparse.SUPPRESS)
    train.set_defaults(func=_cmd_train)

    render = sub.add_parser("render", help="render a trained run")
    render.add_argument("--method", required=True, choices=sorted(METHODS))
    render.add_argument("-s", "--scene", required=True)
    render.add_argument("-m", "--run", required=True)
    render.add_argument("--config")
    render.add_argument(
        "--dense",
        action="store_true",
        help="QUEEN only: render the dense model instead of the compressed one",
    )
    render.add_argument("--dry-run", action="store_true", dest="dry_run")
    render.add_argument("passthrough", nargs="*", help=argparse.SUPPRESS)
    render.set_defaults(func=_cmd_render)

    show = sub.add_parser("manifest", help="print a run's manifest")
    show.add_argument("-m", "--run", required=True)
    show.set_defaults(func=_cmd_manifest)

    return parser


def main(argv: list[str] | None = None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    # argparse cannot express "everything after --" for a subparser, so the split
    # happens here and the tail lands in `passthrough`.
    passthrough: list[str] = []
    if "--" in argv:
        cut = argv.index("--")
        argv, passthrough = argv[:cut], argv[cut + 1 :]

    parser = build_parser()
    args = parser.parse_args(argv)
    if not hasattr(args, "passthrough"):
        args.passthrough = []
    args.passthrough = list(args.passthrough) + passthrough
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
