"""Make upstream ReRF importable from a normal Python process.

Upstream expects to be launched by its own wrapper::

    LD_LIBRARY_PATH=./ac_dc:$LD_LIBRARY_PATH PYTHONPATH=./ac_dc/:$PYTHONPATH \\
        python run.py ...

from inside its root. That is not a convention to be tidied away -- three
separate things depend on it, and each fails differently:

``ac_dc`` is a binary with a stale RUNPATH
    Importing ``lib.dvgo`` transitively imports ``codec``, which imports the
    prebuilt ``ncvv_ac_dc`` extension. Its ``NEEDED`` ``libcode_library.so`` is
    findable only through ``LD_LIBRARY_PATH``, because the RUNPATH recorded in
    it is ``/home/ubuntu/pybind11_numpy/build`` -- a directory on the machine
    that built it. ``LD_LIBRARY_PATH`` is read by the dynamic loader at exec and
    cannot be set from inside a running process, so :func:`activate` ``dlopen``s
    the libraries by absolute path with ``RTLD_GLOBAL`` instead. Their symbols
    land in the global namespace and the extension's by-name lookup resolves.
    Same effect, no re-exec -- and re-exec is not an option, because under
    pytest it silently restarts the session.

``codec.quant`` reads a relative path at import time
    ``np.load("./codec/quant.npy")``, evaluated while the module is being
    imported, so the process CWD has to be upstream's root at that moment. Hence
    :func:`upstream_cwd`.

The extension is Python 3.8 only
    ``ncvv_ac_dc`` ships as ``*.cpython-38-*.so`` with no sources published.
    There is no way to rebuild it for a newer interpreter, which is why this
    package runs in its own Python 3.8 environment rather than Open4D's 3.10+
    one, and why ReRF's frames are decoded server-side and delivered as pixels.

Upstream stays byte-identical to its published tree. Everything that would
otherwise be a patch lives here.
"""
from __future__ import annotations

import contextlib
import ctypes
import os
import sys
from pathlib import Path

#: Upstream's root: a pruned clone of https://github.com/aoliao12138/ReRF.
UPSTREAM = Path(__file__).resolve().parents[1] / "upstream"

#: Preloaded with RTLD_GLOBAL so the entropy coder's own NEEDED lookups resolve.
AC_DC_LIBRARIES = ("libcode_library.so", "libjfif_library.so")

DEFAULT_CUDA_HOME = "/usr/local/cuda-12.4"
DEFAULT_HOST_COMPILER = "/usr/bin/gcc-11"
"""ReRF JIT-compiles DVGO's CUDA kernels on first import. Ubuntu 24.04's
default GCC 13 is newer than CUDA 12.4's nvcc accepts, so point it at the 11
toolchain."""

_activated = False
_preloaded: list = []


def _prepend(name: str, value: str) -> None:
    parts = [part for part in os.environ.get(name, "").split(os.pathsep) if part]
    if value not in parts:
        os.environ[name] = os.pathsep.join([value] + parts)


def patch_dependencies() -> None:
    """Reconcile upstream's code with the versions this environment installs.

    Two upstream calls fail outright on current numpy and imageio. Both are
    version drift rather than logic, so they are patched at runtime rather than
    by editing ``upstream/``:

    ``np.bool`` and friends
        Removed in numpy 1.24. ``codec.compress_utils.decode_pca`` uses
        ``np.bool``, and *every* decode goes through it. Upstream's
        ``compress.py`` decodes each frame to build the next one's reference, so
        without this it raises after frame 0 and truncates the bitstream to a
        single frame -- silently, because the exception lands after the write.

    ``imageio.imwrite`` on a ``(H, W, 1)`` array
        Newer imageio and Pillow raise "Can't write images with one color
        channel". Upstream writes its depth maps that way, so it dies after the
        first frame. Squeezing the trailing axis is what older imageio did.

    Idempotent, and called by :func:`activate`.
    """
    import numpy

    for name, builtin in (("bool", bool), ("object", object), ("int", int),
                          ("float", float), ("complex", complex), ("str", str)):
        if name not in vars(numpy):
            setattr(numpy, name, builtin)

    try:
        import imageio
    except ImportError:            # only upstream's render script needs it
        return
    if getattr(imageio.imwrite, "_squeezes_gray", False):
        return
    original = imageio.imwrite

    def imwrite(uri, image, **kwargs):
        array = numpy.asarray(image)
        if array.ndim == 3 and array.shape[-1] == 1:
            array = array[..., 0]
        return original(uri, array, **kwargs)

    imwrite._squeezes_gray = True
    imageio.imwrite = imwrite


def activate(*, cuda_home: str = None, arch_list: str = "8.9") -> Path:
    """Make ``import lib.dvgo`` work in this process. Idempotent."""
    global _activated
    if not (UPSTREAM / "run.py").is_file():
        raise RuntimeError(
            f"upstream ReRF is missing from {UPSTREAM}; clone "
            "https://github.com/aoliao12138/ReRF there (see ../README.md)"
        )
    if _activated:
        return UPSTREAM
    patch_dependencies()

    for name in AC_DC_LIBRARIES:
        path = UPSTREAM / "ac_dc" / name
        if not path.is_file():
            raise RuntimeError(
                f"ReRF's entropy coder is incomplete: {path} is missing. It ships "
                "only as a prebuilt binary, so it cannot be rebuilt from source."
            )
        _preloaded.append(ctypes.CDLL(str(path), mode=ctypes.RTLD_GLOBAL))

    home = cuda_home or os.environ.get("CUDA_HOME") or DEFAULT_CUDA_HOME
    os.environ["CUDA_HOME"] = home
    _prepend("PATH", str(Path(home) / "bin"))
    os.environ.setdefault("TORCH_CUDA_ARCH_LIST", arch_list)
    if Path(DEFAULT_HOST_COMPILER).exists():
        os.environ.setdefault("CC", DEFAULT_HOST_COMPILER)
        os.environ.setdefault("CXX", DEFAULT_HOST_COMPILER.replace("gcc", "g++"))

    for path in (str(UPSTREAM), str(UPSTREAM / "ac_dc")):
        if path not in sys.path:
            sys.path.insert(0, path)
    _activated = True
    return UPSTREAM


@contextlib.contextmanager
def upstream_cwd():
    """Run a block with the CWD at upstream's root.

    Needed around the *first* ``import lib.*`` because ``codec.quant`` reads
    ``./codec/quant.npy`` while importing, and around any later call into
    ``codec`` that resolves a relative path of its own.
    """
    previous = os.getcwd()
    os.chdir(activate())
    try:
        yield Path(previous)
    finally:
        os.chdir(previous)
