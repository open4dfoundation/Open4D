"""Run a vendored ReRF script with this repo's environment already set up.

Upstream's README asks you to launch its scripts as

    LD_LIBRARY_PATH=./ac_dc:$LD_LIBRARY_PATH PYTHONPATH=./ac_dc/:$PYTHONPATH \\
        python codec/compress.py --model_path ...

from inside ``rerf/``. That is easy to get wrong and it is not enough on its own:
the entropy coder has to be preloaded, the CWD has to be the ReRF root (``codec``
resolves ``./codec/quant.npy`` at import), and two upstream calls break against
modern numpy/imageio. :mod:`nevo.rerf_env` handles all of it, so this just wires
it to a command line and execs the script in-process.

Compress a trained sequence into ReRF's own bitstream, then render it back:

    python -m orbitnevo.rerf_cli codec/compress.py \\
        --model_path ~/nevo_runs/g_basketball --expr_name rerf \\
        --quality 99 --pca --pca_chs 7,13 --frame_num 30

    python -m orbitnevo.rerf_cli rerf_render.py \\
        --config configs/nevo/g_basketball.py \\
        --compression_path ~/nevo_runs/g_basketball/rerf \\
        --render_360 30 --pca --pca_chs 7,13

Two things upstream will not tell you:

* ``--pca``/``--group_size`` must match between compress and render, per
  upstream's README. PCA is ~13% smaller here and part of ReRF's published
  method, so it is worth passing at both ends.
* ``--render_360`` must not exceed the number of *compressed* frames. Frame ids
  wrap on ``cfg.frame_num``, but the decode stream is pulled sequentially, so
  asking for more frames than were compressed exhausts the iterator.

Paths in arguments are expanded and made absolute before the CWD changes, so
``~`` and relative paths behave the way the shell led you to expect.

Runs in the ``nevo`` environment (Python 3.8), like everything else that touches
a ReRF model.
"""
from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

MODULE_ROOT = Path(__file__).resolve().parents[1]
if str(MODULE_ROOT) not in sys.path:
    sys.path.insert(0, str(MODULE_ROOT))

from nevo import rerf_env  # noqa: E402

SCRIPTS = ("codec/compress.py", "rerf_render.py", "run.py", "tools/vis_volume.py")


def _absolute(argument: str) -> str:
    """Expand a path-looking argument while the CWD is still the caller's."""
    if argument.startswith("-") or "/" not in argument:
        return argument
    expanded = Path(argument).expanduser()
    # Only rewrite things that exist, or whose parent does: leaves values like
    # `configs/nevo/x.py` (relative to the ReRF root) and `7,13` alone.
    if expanded.exists() or (expanded.parent.exists() and expanded.is_absolute()):
        return str(expanded.resolve())
    if expanded.is_absolute():
        return str(expanded)
    return argument


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__.split("\n", 1)[0],
        usage="%(prog)s <script> [script arguments ...]",
    )
    parser.add_argument("script", help=f"path under rerf/, e.g. one of {SCRIPTS}")
    parser.add_argument("arguments", nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)

    rewritten = [_absolute(argument) for argument in args.arguments]

    root = rerf_env.activate()
    script = root / args.script
    if not script.is_file():
        print(f"no such script: {script}", file=sys.stderr)
        print(f"expected one of {SCRIPTS} (relative to {root})", file=sys.stderr)
        return 2

    with rerf_env.rerf_cwd():
        sys.argv = [str(script)] + rewritten
        print(f"$ (in {os.getcwd()}) python {args.script} {' '.join(rewritten)}",
              flush=True)
        code = compile(script.read_text(), str(script), "exec")
        # __name__ = "__main__" so upstream's module-level argparse blocks run.
        exec(code, {"__name__": "__main__", "__file__": str(script)})
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
