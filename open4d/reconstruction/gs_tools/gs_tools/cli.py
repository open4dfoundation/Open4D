"""`gs-tools` -- one entry point for both Gaussian-splatting methods."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
from pathlib import Path

from streamer import bundle
from streamer import server as view

from . import env, outputs, paths, rast
from .data import layouts
from .io import manifest
from .methods import base, capture, gaussian, gstream, queen, rerf, vega

METHODS = {"queen": queen, "3dgstream": gstream}

#: Exporter name -> the module implementing it. Which *kinds* each claims lives
#: in `gs_tools.outputs.EXPORTER_FOR`, next to the Kind enum, so that
#: `Detected.viewable` can answer "is there a path for this" without importing
#: this module -- and so the answer cannot disagree with what `--method auto`
#: picks.
EXPORTER_MODULES = {
    "vega": vega,
    "rerf": rerf,
    "captured": capture,
    "gaussian": gaussian,
}

#: Kinds each exporter claims, derived so the two definitions cannot drift.
EXPORTERS = {
    name: (module, tuple(k for k, v in outputs.EXPORTER_FOR.items() if v == name))
    for name, module in EXPORTER_MODULES.items()
}


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
        frame_start=args.frame_start,
        frame_end=args.frame_end,
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


def _cmd_inspect(args: argparse.Namespace) -> int:
    found = [outputs.detect(Path(entry).expanduser()) for entry in args.input]
    for detected in found:
        print(f"{detected.root}\n  {outputs.describe(detected)}")
    return 0 if all(detected.viewable for detected in found) else 1


def _default_bundle_dir(sources: list[Path], kinds: list[outputs.Detected]) -> Path:
    """Where a bundle goes when the user did not say.

    Not beside the sources: these outputs live on data mounts that are shared,
    read-mostly, or (for a Vega catalog) somebody else's results directory, and
    an export is derived data that can be regenerated. The cache directory is
    keyed by the absolute source paths, so re-exporting the same set reuses the
    same place instead of accumulating copies, and a different set gets its own.
    """
    cache = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache"))
    digest = hashlib.sha1("\n".join(str(source) for source in sources).encode()).hexdigest()[:8]
    if len(sources) == 1:
        name = f"{kinds[0].kind.value}-{sources[0].name}-{digest}"
    else:
        name = f"mixed-{len(sources)}-sources-{digest}"
    return cache / "open4d-gs-tools" / "view" / name


def _exporter(found: outputs.Detected, requested: str):
    """The module that can export this output, or None if it needs none."""
    if found.kind is outputs.Kind.BUNDLE:
        return None
    if requested != "auto":
        return EXPORTER_MODULES[requested]
    claimed = outputs.EXPORTER_FOR.get(found.kind)
    return EXPORTER_MODULES.get(claimed) if claimed else None


def _options_for(module, args: argparse.Namespace):
    """Translate the shared flag set into the exporter's own options."""
    if module is capture:
        return capture.CaptureOptions(
            objects=tuple(args.objects or ()),
            views=tuple(int(v) for v in args.views) if args.views else (),
            frames=args.frames,
            max_width=args.capture_width,
            fps=args.fps,
        )
    if module is gaussian:
        return gaussian.GaussianExportOptions(
            frames=args.frames,
            frame_format=args.frame_format,
            scene=args.scene_name,
            method=args.method_name,
            fps=args.fps,
        )
    if module is vega:
        return vega.VegaExportOptions(
            objects=tuple(args.objects or ()),
            frames=args.frames,
            bake_azimuth_deg=args.bake_azimuth,
            device=args.device,
            fps=args.fps,
        )
    return rerf.RerfRenderOptions(
        frames=args.frames,
        depth=not args.no_depth,
        bitstream=args.bitstream,
        pca=None if args.pca is None else args.pca,
        pca_chs=tuple(int(n) for n in args.pca_chs.split(",")) if args.pca_chs else None,
        group_size=args.group_size,
        config=Path(args.config).expanduser() if args.config else None,
        fps=args.fps,
        dry_run=getattr(args, "dry_run", False),
    )


def _export(args: argparse.Namespace) -> tuple[Path, list[outputs.Detected]]:
    """Build (or reuse) one viewable bundle covering every `args.input`.

    Several sources land in one bundle rather than one each because comparing
    them is the point: a Vega object and the ReRF render of the same subject are
    two clips in one viewer, not two browser tabs.
    """
    sources = [Path(entry).expanduser().resolve() for entry in args.input]
    found = [outputs.detect(source) for source in sources]
    for detected in found:
        print(f"{detected.root}\n  {outputs.describe(detected)}")

    unusable = [d.root for d in found if not d.viewable]
    if unusable:
        raise SystemExit("nothing viewable at " + ", ".join(str(path) for path in unusable))

    bundles = [d for d in found if d.kind is outputs.Kind.BUNDLE]
    if bundles:
        if len(found) > 1:
            raise SystemExit(
                f"{bundles[0].root} is already a bundle; it cannot be combined with "
                "other sources. Re-export from the original outputs instead."
            )
        if getattr(args, "output", None):
            print("  already a bundle; --output ignored")
        return sources[0], found

    modules = []
    for detected in found:
        module = _exporter(detected, args.method)
        if module is None:
            raise SystemExit(
                f"no exporter for {detected.kind.value} at {detected.root}; --method takes "
                + ", ".join(sorted(EXPORTERS))
            )
        modules.append(module)

    out_dir = (
        Path(args.output).expanduser().resolve() if args.output
        else _default_bundle_dir(sources, found)
    )
    existing = outputs.detect(out_dir)
    if existing.kind is outputs.Kind.BUNDLE and not args.force:
        # `view` calls this on every invocation, and decoding a Vega sequence or
        # rendering a ReRF one is not something to repeat for a second look.
        print(f"  reusing bundle at {out_dir} ({outputs.describe(existing)}); --force to rebuild")
        return out_dir, found

    titles: list[str] = []
    clips: list[bundle.Clip] = []
    detail: dict[str, object] = {}
    for source, module in zip(sources, modules):
        print(f"  exporting {source.name} with {module.name} -> {out_dir}")
        title, produced, produced_detail = module.build_clips(
            source, out_dir, _options_for(module, args)
        )
        titles.append(title)
        clips += produced
        detail.setdefault("sources", []).append(
            {"path": str(source), "exporter": module.name, "detail": produced_detail}
        )

    if getattr(args, "dry_run", False) and not clips:
        return out_dir, found

    # The rigs come from the corpus, not from any exporter: a shared camera is
    # only shared if every method is handed the same one.
    scenes = sorted({clip.scene for clip in clips if clip.scene})
    rigs = capture.rigs_for(args.corpus, scenes) if scenes else {}
    missing = [scene for scene in scenes if scene not in rigs]
    if missing:
        print(
            f"  note: no rig in {args.corpus} for {', '.join(missing)} — those scenes "
            "get no shared camera, so they are explore-only"
        )
    bundle.write(
        out_dir,
        title=titles[0] if len(titles) == 1 else f"{len(clips)} clips from {len(sources)} sources",
        source=", ".join(str(source) for source in sources),
        clips=clips,
        fps=args.fps,
        scenes=rigs,
        detail=detail,
    )
    return out_dir, found


def _cmd_export(args: argparse.Namespace) -> int:
    out_dir, _ = _export(args)
    if getattr(args, "dry_run", False):
        return 0
    print(f"\nbundle: {out_dir}\nview it with: gs-tools view -i {out_dir}")
    return 0


def _cmd_view(args: argparse.Namespace) -> int:
    out_dir, _ = _export(args)
    print()
    view.serve(
        out_dir,
        host=args.host,
        port=args.port,
        open_browser=args.browser,
    )
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
    train.add_argument("--images", help="3DGStream only: image subdirectory inside each timestep")
    train.add_argument("--first-load-iteration", type=int, default=15000, dest="first_load_iteration")
    train.add_argument("--frame-start", type=int, dest="frame_start", help="3DGStream only: first timestep")
    train.add_argument("--frame-end", type=int, dest="frame_end", help="3DGStream only: last timestep")
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

    inspect = sub.add_parser("inspect", help="report what kind of output a directory holds")
    inspect.add_argument("-i", "--input", required=True, nargs="+")
    inspect.set_defaults(func=_cmd_inspect)

    def add_export_arguments(target: argparse.ArgumentParser) -> None:
        target.add_argument("-i", "--input", required=True, nargs="+",
                            help="one or more of: a Vega bitstream/catalog, a ReRF run or "
                                 "bitstream, a rendered image sequence, or (alone) an existing "
                                 "bundle. Several sources become clips in one bundle.")
        target.add_argument("-o", "--output", help="bundle directory (default: a cache directory)")
        target.add_argument("--method", default="auto", choices=["auto", *sorted(EXPORTERS)])
        target.add_argument("--frames", type=int, help="export only the first N frames")
        target.add_argument("--fps", type=int, default=30, help="playback rate recorded in the bundle")
        # Vega
        target.add_argument("--objects", nargs="+",
                            help="object names to export, for a Vega catalog or an ORBIT "
                                 "corpus (default: all of them)")
        target.add_argument("--corpus", default=str(capture.DEFAULT_CORPUS),
                            help="ORBIT corpus the capture rigs are read from, which is what "
                                 "gives every method a shared camera")
        target.add_argument("--views", nargs="+",
                            help="captured only: rig view ids to export (default: all 8)")
        target.add_argument("--capture-width", type=int, default=1024, dest="capture_width",
                            help="captured only: longest edge of the exported images")
        target.add_argument("--bake-azimuth", type=float, default=0.0, dest="bake_azimuth",
                            help="Vega only: camera azimuth in degrees that colour is baked from")
        # 3DGS runs
        target.add_argument("--frame-format", default="ply", choices=gaussian.FORMATS,
                            dest="frame_format",
                            help="3DGS runs only: 'ply' copies the run's own frames; "
                                 "'splat' re-encodes to 32 bytes per Gaussian, several "
                                 "times smaller but degree-0 colour only")
        target.add_argument("--scene-name", dest="scene_name",
                            help="3DGS runs only: subject name, so this clip lines up in "
                                 "Compare with another method's clip of the same subject "
                                 "(default: the run directory's name)")
        target.add_argument("--method-name", dest="method_name",
                            help="3DGS runs only: method label in the viewer "
                                 "(default: the run manifest's, else 'gaussian')")
        target.add_argument("--device", help="Vega only: torch device for the colour decode")
        # ReRF
        target.add_argument("--force", action="store_true",
                            help="rebuild the bundle even if one is already there")
        target.add_argument("--no-depth", action="store_true", dest="no_depth",
                            help="ReRF only: skip ReRF's depth maps")
        target.add_argument("--bitstream",
                            help="ReRF only: which bitstream directory in the run to consider, "
                                 "when a run holds several and none has been rendered. "
                                 "Rendering one is `python -m rerf_stream.export`.")
        target.add_argument("--config", help="ReRF only: ReRF config (default: <run>/config.py)")
        target.add_argument("--group-size", type=int, dest="group_size",
                            help="ReRF only: override the inferred ReRF key-frame interval")
        target.add_argument("--pca-chs", dest="pca_chs",
                            help="ReRF only: override the inferred PCA channel split, e.g. 7,13")
        target.add_argument("--pca", dest="pca", action="store_true", default=None,
                            help="ReRF only: force PCA decode on")
        target.add_argument("--no-pca", dest="pca", action="store_false",
                            help="ReRF only: force PCA decode off")
        target.add_argument("--dry-run", action="store_true", dest="dry_run",
                            help="ReRF only: print the render command without running it")
        target.add_argument("passthrough", nargs="*", help=argparse.SUPPRESS)

    export = sub.add_parser(
        "export",
        help="convert a method's output into a viewable bundle (3DGS PLY or images)",
    )
    add_export_arguments(export)
    export.set_defaults(func=_cmd_export)

    viewer = sub.add_parser("view", help="export if needed, then serve the bundle to a browser")
    add_export_arguments(viewer)
    viewer.add_argument("--port", type=int, default=view.DEFAULT_PORT)
    viewer.add_argument("--host", default="127.0.0.1",
                        help="0.0.0.0 to reach it from another machine (no authentication)")
    viewer.add_argument("--browser", action="store_true", help="open a browser here")
    viewer.set_defaults(func=_cmd_view)

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
