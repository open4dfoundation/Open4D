"""Import ReRF from anywhere, without upstream's wrapper script.

Upstream runs everything as ``python run.py`` from the ReRF root behind::

    LD_LIBRARY_PATH=./ac_dc:$LD_LIBRARY_PATH PYTHONPATH=./ac_dc/:$PYTHONPATH

which is not just convention. Importing ``lib.dvgo`` transitively imports
``codec``, and ``codec.encoder_jpeg``

* imports the prebuilt ``ncvv_ac_dc`` extension, whose ``NEEDED``
  ``libcode_library.so`` is only findable via ``LD_LIBRARY_PATH`` -- its
  recorded ``RUNPATH`` is ``/home/ubuntu/pybind11_numpy/build``, a path on the
  machine that built it; and
* evaluates ``gen_3d_quant_tbl()`` at import time, which does
  ``np.load("./codec/quant.npy")`` -- a *relative* path, so the process CWD
  has to be the ReRF root at that moment.

``LD_LIBRARY_PATH`` is read by the dynamic loader at exec and cannot be set
from inside a running process, so the obvious fix is to re-exec. Don't: under
pytest that silently restarts the whole session. Instead :func:`activate`
``dlopen``s the two libraries by absolute path with ``RTLD_GLOBAL``, which
puts their symbols in the global namespace so the extension's by-name lookup
resolves. Same effect, no re-exec, and it works when NeVo is imported as a
library rather than run as a script.

``ncvv_ac_dc`` ships only as a CPython 3.8 binary with no sources, which is
why this baseline lives in its own ``nevo`` conda environment rather than the
repo's 3.10+ one. See ``baselines/NeVo/README.md``.
"""
from __future__ import annotations

import contextlib
import ctypes
import os
import sys
from pathlib import Path

RERF_ROOT = Path(__file__).resolve().parents[1] / "rerf"

AC_DC_LIBRARIES = ("libcode_library.so", "libjfif_library.so")

DEFAULT_CUDA_HOME = "/usr/local/cuda-12.4"
DEFAULT_HOST_COMPILER = "/usr/bin/gcc-11"
"""ReRF JIT-compiles DVGO's CUDA kernels through torch.utils.cpp_extension on
first import. Ubuntu 24.04's default GCC 13 is newer than CUDA 12.4's nvcc
accepts, so point it at the 11 toolchain this box also has -- the same
workaround DeltaStream's renderer applies."""

_activated = False
_preloaded = []


def _prepend(name: str, value: str) -> None:
    parts = [part for part in os.environ.get(name, "").split(os.pathsep) if part]
    if value not in parts:
        os.environ[name] = os.pathsep.join([value] + parts)


def patch_dependencies() -> None:
    """Reconcile upstream's code with the dependency versions this env installs.

    Two upstream calls fail outright, and both are version drift rather than
    logic, so they are patched at runtime here instead of by editing ``rerf/``
    -- which stays byte-identical to upstream (see ``rerf/PATCHES.md``).

    ``np.bool`` and friends
        Removed in numpy 1.24. ``codec.compress_utils.decode_pca`` uses
        ``np.bool``, and *every* decode goes through it. Upstream's
        ``compress.py`` decodes each frame to build the next frame's reference,
        so without this it raises after writing frame 0 and the bitstream is
        silently truncated to one frame.

    ``imageio.imwrite`` on a ``(H, W, 1)`` array
        Newer imageio/Pillow raise "Can't write images with one color channel".
        ``rerf_render.py`` writes its depth maps that way, so it dies after the
        first frame. Squeezing the trailing axis is what older imageio did
        implicitly.

    Idempotent, and called by :func:`activate`.
    """
    import numpy

    for name, builtin in (("bool", bool), ("object", object), ("int", int),
                          ("float", float), ("complex", complex), ("str", str)):
        if not hasattr(numpy, name):
            setattr(numpy, name, builtin)

    try:
        import imageio
    except ImportError:            # only rerf_render.py needs it
        return
    if getattr(imageio.imwrite, "_nevo_squeezes_gray", False):
        return
    original = imageio.imwrite

    def imwrite(uri, image, **kwargs):
        array = numpy.asarray(image)
        if array.ndim == 3 and array.shape[-1] == 1:
            array = array[..., 0]
        return original(uri, array, **kwargs)

    imwrite._nevo_squeezes_gray = True
    imageio.imwrite = imwrite


def activate(*, cuda_home: str = None, arch_list: str = "8.9") -> Path:
    """Make ``import lib.dvgo`` work in this process. Idempotent."""
    global _activated
    root = RERF_ROOT
    if not (root / "run.py").is_file():
        raise RuntimeError(f"vendored ReRF is missing from {root}")
    if _activated:
        return root
    patch_dependencies()

    for name in AC_DC_LIBRARIES:
        path = root / "ac_dc" / name
        if not path.is_file():
            raise RuntimeError(f"ReRF's entropy coder is incomplete: {path} is missing")
        _preloaded.append(ctypes.CDLL(str(path), mode=ctypes.RTLD_GLOBAL))

    home = cuda_home or os.environ.get("CUDA_HOME") or DEFAULT_CUDA_HOME
    os.environ["CUDA_HOME"] = home
    _prepend("PATH", str(Path(home) / "bin"))
    os.environ.setdefault("TORCH_CUDA_ARCH_LIST", arch_list)
    if Path(DEFAULT_HOST_COMPILER).exists():
        os.environ.setdefault("CC", DEFAULT_HOST_COMPILER)
        os.environ.setdefault("CXX", DEFAULT_HOST_COMPILER.replace("gcc", "g++"))

    for path in (str(root), str(root / "ac_dc")):
        if path not in sys.path:
            sys.path.insert(0, path)
    _activated = True
    return root


@contextlib.contextmanager
def rerf_cwd():
    """Run a block with the CWD at the ReRF root.

    Needed around the *first* ``import lib.*``, because ``codec.quant`` reads
    ``./codec/quant.npy`` at import time, and around any later call into
    ``codec`` or ``run`` that resolves a relative path of its own.
    """
    previous = os.getcwd()
    os.chdir(activate())
    try:
        yield Path(previous)
    finally:
        os.chdir(previous)


def import_rerf():
    """Import and return ReRF's ``(dvgo, dvgo_video, utils)`` modules."""
    activate()
    with rerf_cwd():
        from lib import dvgo, dvgo_video, utils

    return dvgo, dvgo_video, utils
