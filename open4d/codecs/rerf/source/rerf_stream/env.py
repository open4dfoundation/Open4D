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
    ``ncvv_ac_dc`` ships as ``*.cpython-38-x86_64-linux-gnu.so`` with no
    sources published. There is no way to rebuild it for a newer interpreter or
    another platform, which is why this package runs in its own Python 3.8
    environment rather than Open4D's 3.10+ one, and why ReRF's frames are
    decoded server-side and delivered as pixels.

DVGO's CUDA kernels are JIT-compiled on first import
    ``lib/dvgo.py`` calls ``torch.utils.cpp_extension.load`` at import time.
    That module reads ``CUDA_HOME`` once, when it is first imported, and the
    build then needs an ``nvcc`` whose major version matches the CUDA that torch
    itself was built with, a host compiler that ``nvcc`` accepts, and an
    architecture list the toolkit can target. :func:`plan` derives all of that
    from the installed torch and the actual GPU -- not from fixed defaults --
    and :func:`activate` applies it before anything imports the extension
    loader. Builds are cached per runtime combination, so switching torch,
    toolkit, GPU or compiler never reuses a stale ``.so``.

Run ``python -m rerf_stream.env`` to print the plan, or the reasons there is
none. Overrides, all optional:

``OPEN4D_RERF_CUDA_HOME``
    The toolkit to compile with. Must match torch's CUDA major version.
``CC`` / ``CXX``
    The host compiler. Checked against what the chosen ``nvcc`` accepts.
``TORCH_CUDA_ARCH_LIST``
    Taken as given instead of being read off the GPU.
``OPEN4D_RERF_EXTENSIONS_ROOT``
    Where the per-combination ``TORCH_EXTENSIONS_DIR`` directories live.

Upstream stays byte-identical to its published tree. Everything that would
otherwise be a patch lives here.
"""
from __future__ import annotations

import contextlib
import ctypes
import glob
import importlib.util
import json
import os
import platform
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Dict, List, Mapping, Optional, Sequence, Tuple

#: Upstream's root: a pruned clone of https://github.com/aoliao12138/ReRF.
UPSTREAM = Path(__file__).resolve().parents[1] / "upstream"

#: Preloaded with RTLD_GLOBAL so the entropy coder's own NEEDED lookups resolve.
AC_DC_LIBRARIES = ("libcode_library.so", "libjfif_library.so")
#: The entropy coder itself. Prebuilt, without sources, for exactly this ABI.
AC_DC_EXTENSION = "ncvv_ac_dc.cpython-38-x86_64-linux-gnu.so"

#: Modules upstream imports on the decode and training paths. ``tkinter``
#: because ``lib/dvgo_video.py`` does ``from turtle import forward``.
REQUIRED_MODULES = (
    "torch", "torchvision", "torch_scatter", "mmcv", "bitarray", "cv2", "imageio",
    "lpips", "pytorch_msssim", "scipy", "matplotlib", "ipdb", "einops", "tqdm",
    "PIL", "tkinter",
)

#: Directories searched for ``cuda-*`` toolkits.
TOOLKIT_ROOTS = ("/usr/local",)
#: Directories searched for versioned host compilers (``gcc-11``, ...).
COMPILER_DIRS = ("/usr/bin", "/usr/local/bin")

#: ``(toolkit major, minor) -> newest GCC major its nvcc accepts``, from each
#: release's ``host_config.h``. Read as "from this version on".
_MAX_GCC = (((11, 0), 9), ((11, 1), 10), ((11, 4), 11), ((12, 0), 12),
            ((12, 4), 13), ((12, 8), 14))
#: ``(toolkit major, minor) -> newest compute capability it compiles for``.
_MAX_ARCH = (((11, 0), (8, 0)), ((11, 1), (8, 6)), ((11, 8), (9, 0)), ((12, 8), (12, 0)))

Version = Tuple[int, int]

_activated = False
_preloaded: list = []
#: The :class:`BuildPlan` the last :func:`activate` applied, for reporting.
PLAN: Optional["BuildPlan"] = None


# ----------------------------------------------------------------- versions ---


def parse_version(text) -> Optional[Version]:
    """``"11.8"``, ``"11.8.89"``, ``"V11.8.89"`` or ``"cuda-11.8"`` -> ``(11, 8)``."""
    match = re.search(r"(\d+)\.(\d+)", str(text or ""))
    return (int(match.group(1)), int(match.group(2))) if match else None


def _dotted(version: Version) -> str:
    return "%d.%d" % version


def _lookup(table, version: Version):
    found = None
    for since, value in table:
        if version >= since:
            found = value
    return found


def max_gcc(cuda: Version) -> Optional[int]:
    """Newest GCC major the given toolkit's nvcc accepts, or None if unknown."""
    return _lookup(_MAX_GCC, cuda)


def max_arch(cuda: Version) -> Optional[Version]:
    """Newest compute capability the given toolkit compiles native code for."""
    return _lookup(_MAX_ARCH, cuda)


# ----------------------------------------------------------------- toolkits ---


@dataclass(frozen=True)
class Toolkit:
    home: Path
    version: Optional[Version]
    #: Where it was found: an environment variable or a search directory.
    source: str

    @property
    def nvcc(self) -> Path:
        return self.home / "bin" / "nvcc"

    def describe(self) -> str:
        version = _dotted(self.version) if self.version else "unknown version"
        return "%s (CUDA %s, from %s)" % (self.home, version, self.source)


def _run_text(run, command) -> str:
    try:
        result = run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                     universal_newlines=True, timeout=30)
    except (OSError, subprocess.SubprocessError):
        return ""
    return result.stdout or ""


def toolkit_version(home, *, run: Callable = subprocess.run) -> Optional[Version]:
    """A toolkit's CUDA version, without trusting its directory name first.

    ``version.json`` (11.1 onwards) and ``version.txt`` (older) are what the
    installers write; conda's ``cuda-nvcc`` packages write neither, so ``nvcc
    --version`` is asked next, and the directory name is the last resort.
    """
    home = Path(home)
    try:
        info = json.loads((home / "version.json").read_text())
        for key in ("cuda", "cuda_nvcc", "cuda_cudart"):
            version = parse_version(info.get(key, {}).get("version"))
            if version:
                return version
    except (OSError, ValueError, AttributeError):
        pass
    try:
        version = parse_version((home / "version.txt").read_text())
        if version:
            return version
    except OSError:
        pass
    nvcc = home / "bin" / "nvcc"
    if nvcc.is_file():
        match = re.search(r"release (\d+\.\d+)", _run_text(run, [str(nvcc), "--version"]))
        if match:
            return parse_version(match.group(1))
    return parse_version(home.name) if home.name.startswith("cuda") else None


def cuda_toolkits(environ: Optional[Mapping[str, str]] = None, *,
                  roots: Sequence = TOOLKIT_ROOTS,
                  run: Callable = subprocess.run) -> List[Toolkit]:
    """Every CUDA toolkit with an ``nvcc``, in the order a choice prefers them.

    ``CUDA_HOME`` and ``CUDA_PATH`` are *candidates*, not instructions: shells
    on a shared GPU server commonly export one toolkit for everything, and the
    torch in this environment may have been built against another. Only
    ``OPEN4D_RERF_CUDA_HOME`` (see :func:`select_toolkit`) is binding.
    """
    environ = os.environ if environ is None else environ
    candidates = []
    for variable in ("OPEN4D_RERF_CUDA_HOME", "CUDA_HOME", "CUDA_PATH"):
        if environ.get(variable):
            candidates.append((Path(environ[variable]).expanduser(), variable))
    if environ.get("CONDA_PREFIX"):
        candidates.append((Path(environ["CONDA_PREFIX"]), "CONDA_PREFIX"))
    for root in roots:
        found = sorted(glob.glob(os.path.join(str(root), "cuda-*")), reverse=True)
        candidates += [(Path(path), str(root)) for path in found]
        candidates.append((Path(root) / "cuda", str(root)))

    toolkits, seen = [], set()
    for home, source in candidates:
        try:
            key = home.resolve()
        except OSError:
            continue
        if key in seen or not (home / "bin" / "nvcc").is_file():
            continue
        seen.add(key)
        toolkits.append(Toolkit(home, toolkit_version(home, run=run), source))
    return toolkits


def _toolkit_help(torch_cuda: Version) -> str:
    dotted = _dotted(torch_cuda)
    return (
        "Install a CUDA %s toolkit into this environment, e.g.\n"
        "      conda install -c nvidia/label/cuda-%s.0 cuda-nvcc cuda-cudart-dev cuda-cccl\n"
        "    or point OPEN4D_RERF_CUDA_HOME at an existing CUDA %s install. "
        "Alternatively install a torch built for a toolkit you already have."
        % (dotted, dotted, dotted)
    )


def select_toolkit(torch_cuda: Version, toolkits: Sequence[Toolkit], *,
                   explicit=None, notes: Optional[List[str]] = None) -> Toolkit:
    """The toolkit torch's extension builder will accept.

    ``torch.utils.cpp_extension`` refuses an ``nvcc`` whose *major* version
    differs from torch's CUDA and only warns on a minor difference, so an exact
    match is preferred, then the nearest minor of the same major. ``explicit``
    (``OPEN4D_RERF_CUDA_HOME`` or ``activate(cuda_home=...)``) wins outright,
    but is still checked, because a mismatched one fails minutes later inside
    a ninja log instead of here.
    """
    notes = [] if notes is None else notes
    dotted = _dotted(torch_cuda)
    if explicit:
        home = Path(explicit).expanduser()
        matching = [t for t in toolkits if t.home == home]
        chosen = matching[0] if matching else Toolkit(home, toolkit_version(home), "explicit")
        if not chosen.nvcc.is_file():
            raise RuntimeError("the requested CUDA toolkit %s has no bin/nvcc" % home)
        if chosen.version and chosen.version[0] != torch_cuda[0]:
            raise RuntimeError(
                "the requested CUDA toolkit %s is CUDA %s, but this torch was built "
                "with CUDA %s; torch will refuse to compile ReRF's kernels with it. %s"
                % (home, _dotted(chosen.version), dotted, _toolkit_help(torch_cuda)))
        if chosen.version != torch_cuda:
            notes.append("using requested toolkit %s for torch CUDA %s" % (chosen.describe(), dotted))
        return chosen

    exact = [t for t in toolkits if t.version == torch_cuda]
    if exact:
        return exact[0]
    same_major = [t for t in toolkits if t.version and t.version[0] == torch_cuda[0]]
    if same_major:
        chosen = min(same_major, key=lambda t: abs(t.version[1] - torch_cuda[1]))
        notes.append("no CUDA %s toolkit found; using %s (same major version, "
                     "torch only warns)" % (dotted, chosen.describe()))
        return chosen
    found = ", ".join(t.describe() for t in toolkits) or "none"
    raise RuntimeError(
        "this torch was built with CUDA %s, and no CUDA %d.x toolkit is installed "
        "(found: %s). torch.utils.cpp_extension refuses to build with a different "
        "major version. %s" % (dotted, torch_cuda[0], found, _toolkit_help(torch_cuda)))


# ---------------------------------------------------------------- compilers ---


@dataclass(frozen=True)
class Compiler:
    cc: str
    cxx: str
    major: Optional[int]
    source: str

    def describe(self) -> str:
        version = "GCC %d" % self.major if self.major else "unknown version"
        return "%s / %s (%s, from %s)" % (self.cc, self.cxx, version, self.source)


def compiler_major(path, *, run: Callable = subprocess.run) -> Optional[int]:
    """A GCC's major version: from ``gcc-11`` in the name, else ``-dumpversion``."""
    match = re.search(r"(?:gcc|g\+\+|cc|c\+\+)-(\d+)$", os.path.basename(str(path)))
    if match:
        return int(match.group(1))
    text = _run_text(run, [str(path), "-dumpversion"]).strip()
    match = re.match(r"(\d+)", text)
    return int(match.group(1)) if match else None


def _counterpart(cc: str) -> str:
    head, tail = os.path.split(cc)
    for c_name, cxx_name in (("gcc", "g++"), ("clang", "clang++"), ("cc", "c++")):
        if c_name in tail:
            return os.path.join(head, tail.replace(c_name, cxx_name, 1))
    return cc


def select_compiler(cuda: Version, environ: Optional[Mapping[str, str]] = None, *,
                    search: Sequence = COMPILER_DIRS, run: Callable = subprocess.run,
                    notes: Optional[List[str]] = None) -> Compiler:
    """The host compiler nvcc will accept for this toolkit.

    ``CC``/``CXX`` from the environment win but are checked. Otherwise the
    newest ``gcc-N`` that the toolkit allows -- GCC 11 for any CUDA 11.x --
    and only then the unversioned ``gcc``. Ubuntu 24.04's default GCC 13 is
    too new for CUDA 11.x and 12.0-12.3.
    """
    environ = os.environ if environ is None else environ
    notes = [] if notes is None else notes
    ceiling = max_gcc(cuda)
    if environ.get("CC"):
        cc = environ["CC"]
        cxx = environ.get("CXX") or _counterpart(cc)
        major = compiler_major(cc, run=run)
        if ceiling and major and major > ceiling:
            raise RuntimeError(
                "CC=%s is GCC %d, but CUDA %s's nvcc accepts at most GCC %d. Unset CC/CXX "
                "to let this pick one, or install gcc-%d g++-%d (apt) / "
                "gxx_linux-64=%d (conda-forge)." % (cc, major, _dotted(cuda), ceiling,
                                                    ceiling, ceiling, ceiling))
        return Compiler(cc, cxx, major, "CC/CXX")

    versioned = []
    for directory in search:
        if not directory:
            continue
        for path in glob.glob(os.path.join(str(directory), "gcc-[0-9]*")):
            major = compiler_major(path, run=run)
            cxx = _counterpart(path)
            if major and os.path.isfile(cxx) and (ceiling is None or major <= ceiling):
                versioned.append((major, path, cxx))
    if versioned:
        major, cc, cxx = max(versioned)
        return Compiler(cc, cxx, major, "versioned compiler")
    for directory in search:
        cc = os.path.join(str(directory), "gcc") if directory else ""
        if cc and os.path.isfile(cc):
            major = compiler_major(cc, run=run)
            if ceiling is None or (major and major <= ceiling):
                return Compiler(cc, _counterpart(cc), major, "default compiler")
            notes.append("%s is GCC %s, newer than CUDA %s allows" % (cc, major, _dotted(cuda)))
    limit = ceiling or 11
    raise RuntimeError(
        "no host compiler that CUDA %s's nvcc accepts (GCC <= %d) was found in %s. "
        "Install gcc-%d g++-%d (apt) or conda install -c conda-forge gxx_linux-64=%d, "
        "or set CC and CXX." % (_dotted(cuda), limit, ", ".join(str(d) for d in search if d),
                               limit, limit, limit))


# ------------------------------------------------------------- architectures ---


def select_arch_list(capability: Version, cuda: Version,
                     environ: Optional[Mapping[str, str]] = None,
                     notes: Optional[List[str]] = None) -> str:
    """``TORCH_CUDA_ARCH_LIST`` for the GPU this process will run on.

    Read off the device rather than assumed: compiling only for 8.9 and then
    running on an 8.6 card fails at the first kernel launch. A GPU newer than
    the toolkit gets the toolkit's newest architecture plus PTX, which the
    driver JIT-compiles for the actual card.
    """
    environ = os.environ if environ is None else environ
    notes = [] if notes is None else notes
    if environ.get("TORCH_CUDA_ARCH_LIST"):
        return environ["TORCH_CUDA_ARCH_LIST"]
    newest = max_arch(cuda)
    if newest and tuple(capability) > newest:
        notes.append("GPU compute capability %s is newer than CUDA %s targets; "
                     "compiling %s+PTX" % (_dotted(capability), _dotted(cuda), _dotted(newest)))
        return _dotted(newest) + "+PTX"
    return _dotted(capability)


def extensions_root(environ: Optional[Mapping[str, str]] = None) -> Path:
    environ = os.environ if environ is None else environ
    if environ.get("OPEN4D_RERF_EXTENSIONS_ROOT"):
        return Path(environ["OPEN4D_RERF_EXTENSIONS_ROOT"]).expanduser()
    cache = environ.get("XDG_CACHE_HOME") or os.path.join(
        environ.get("HOME") or os.path.expanduser("~"), ".cache")
    return Path(cache) / "open4d" / "rerf-torch-extensions"


def extensions_dir(root, *, python: Version, torch_version: str, cuda: Version,
                   arch_list: str, compiler: Optional[int]) -> Path:
    """One build cache per (Python, torch, toolkit, architectures, compiler)."""
    arch = re.sub(r"[^0-9A-Za-z]+", "_", arch_list).strip("_")
    name = "py%d%d-torch%s-cuda%s-sm%s-gcc%s" % (
        python[0], python[1], torch_version, _dotted(cuda), arch,
        compiler if compiler else "unknown")
    return Path(root) / re.sub(r"[^0-9A-Za-z._+-]+", "_", name)


# --------------------------------------------------------------------- plan ---


@dataclass
class BuildPlan:
    """Everything ``torch.utils.cpp_extension`` reads, decided before it loads."""

    torch_version: str
    torch_cuda: Version
    device: str
    capability: Version
    toolkit: Toolkit
    compiler: Compiler
    arch_list: str
    extensions_dir: Path
    notes: List[str] = field(default_factory=list)

    @property
    def environment(self) -> Dict[str, str]:
        """The variables :func:`apply` sets (``PATH`` and ``LIBRARY_PATH`` are prepended)."""
        return {
            "CUDA_HOME": str(self.toolkit.home),
            "CC": self.compiler.cc,
            "CXX": self.compiler.cxx,
            "TORCH_CUDA_ARCH_LIST": self.arch_list,
            "TORCH_EXTENSIONS_DIR": str(self.extensions_dir),
        }

    def describe(self) -> str:
        lines = [
            "torch            %s (CUDA %s)" % (self.torch_version, _dotted(self.torch_cuda)),
            "GPU              %s, compute capability %s" % (self.device, _dotted(self.capability)),
            "CUDA toolkit     %s" % self.toolkit.describe(),
            "host compiler    %s" % self.compiler.describe(),
            "arch list        %s" % self.arch_list,
            "extensions dir   %s" % self.extensions_dir,
        ]
        lines += ["note             %s" % note for note in self.notes]
        return "\n".join(lines)


def plan(*, torch_module=None, environ: Optional[Mapping[str, str]] = None,
         cuda_home=None, arch_list: Optional[str] = None,
         roots: Sequence = TOOLKIT_ROOTS, compiler_dirs: Optional[Sequence] = None,
         run: Callable = subprocess.run, python: Optional[Version] = None) -> BuildPlan:
    """Decide CUDA_HOME, CC/CXX, the arch list and the build cache.

    Imports torch (which does not import the extension loader) to read the
    CUDA it was built with and the visible GPU. Raises ``RuntimeError`` with
    the fix when no consistent combination exists.
    """
    environ = os.environ if environ is None else environ
    if torch_module is None:
        try:
            import torch as torch_module
        except ImportError as error:
            raise RuntimeError("torch is not importable in this environment (%s). "
                               "Install the CUDA build ReRF's environment file pins." % error)
    notes: List[str] = []
    torch_cuda = parse_version(getattr(torch_module.version, "cuda", None))
    if torch_cuda is None:
        raise RuntimeError(
            "torch %s is a CPU-only build; ReRF needs a CUDA build (for example "
            "torch==2.0.1+cu118 from https://download.pytorch.org/whl/cu118)."
            % torch_module.__version__)
    if not torch_module.cuda.is_available():
        visible = environ.get("CUDA_VISIBLE_DEVICES")
        raise RuntimeError(
            "torch %s cannot see a CUDA device%s. Check nvidia-smi, the driver, and "
            "that CUDA_VISIBLE_DEVICES names an existing GPU."
            % (torch_module.__version__,
               " (CUDA_VISIBLE_DEVICES=%r)" % visible if visible is not None else ""))
    index = torch_module.cuda.current_device()
    capability = tuple(int(v) for v in torch_module.cuda.get_device_capability(index))
    device = str(torch_module.cuda.get_device_name(index))

    explicit = cuda_home or environ.get("OPEN4D_RERF_CUDA_HOME")
    toolkit = select_toolkit(torch_cuda, cuda_toolkits(environ, roots=roots, run=run),
                             explicit=explicit, notes=notes)
    toolkit_cuda = toolkit.version or torch_cuda
    if compiler_dirs is None:
        compiler_dirs = [os.path.join(environ["CONDA_PREFIX"], "bin")] if environ.get("CONDA_PREFIX") else []
        compiler_dirs += list(COMPILER_DIRS)
    compiler = select_compiler(toolkit_cuda, environ, search=compiler_dirs, run=run, notes=notes)
    if arch_list:
        chosen_arch = arch_list
    else:
        chosen_arch = select_arch_list(capability, toolkit_cuda, environ, notes)
    python = python or tuple(sys.version_info[:2])
    directory = extensions_dir(extensions_root(environ), python=python,
                               torch_version=str(torch_module.__version__), cuda=toolkit_cuda,
                               arch_list=chosen_arch, compiler=compiler.major)
    return BuildPlan(str(torch_module.__version__), torch_cuda, device, capability,
                     toolkit, compiler, chosen_arch, directory, notes)


def apply(build: BuildPlan, environ=None) -> None:
    """Export a plan. Must run before ``torch.utils.cpp_extension`` is imported.

    If something imported it earlier, its module-level ``CUDA_HOME`` -- read
    once at import -- is corrected too, since every build reads that global.
    """
    environ = os.environ if environ is None else environ
    for name, value in build.environment.items():
        environ[name] = value
    _prepend("PATH", str(build.toolkit.home / "bin"), environ)
    # Conda's cuda-cudart-dev installs libcudart into lib/, not lib64/, and torch
    # only passes -L$CUDA_HOME/lib64; the linker also reads LIBRARY_PATH.
    for name in ("lib", "lib64"):
        if (build.toolkit.home / name).is_dir():
            _prepend("LIBRARY_PATH", str(build.toolkit.home / name), environ)
    Path(build.extensions_dir).mkdir(parents=True, exist_ok=True)
    loader = sys.modules.get("torch.utils.cpp_extension")
    if loader is not None and environ is os.environ:
        loader.CUDA_HOME = str(build.toolkit.home)


# ---------------------------------------------------------------- preflight ---


def platform_problems(*, system: Optional[str] = None, machine: Optional[str] = None,
                      version: Optional[Sequence[int]] = None) -> List[str]:
    system = platform.system() if system is None else system
    machine = platform.machine() if machine is None else machine
    version = tuple(sys.version_info[:2]) if version is None else tuple(version[:2])
    problems = []
    if system != "Linux" or machine not in ("x86_64", "AMD64"):
        problems.append(
            "ReRF's entropy coder is a prebuilt %s; it cannot load on %s %s. Decode "
            "on an x86-64 Linux machine." % (AC_DC_EXTENSION, system, machine))
    if version != (3, 8):
        problems.append(
            "this is Python %d.%d, but %s only imports into CPython 3.8 and has no "
            "sources to rebuild. Use the Python 3.8 environment in "
            "codecs/rerf/environment-cu118.yml." % (version[0], version[1], AC_DC_EXTENSION))
    return problems


def entropy_coder_problems(upstream=None) -> List[str]:
    upstream = Path(UPSTREAM if upstream is None else upstream)
    missing = [name for name in AC_DC_LIBRARIES + (AC_DC_EXTENSION,)
               if not (upstream / "ac_dc" / name).is_file()]
    if not missing:
        return []
    return ["ReRF's entropy coder is incomplete: %s missing from %s. It ships only as a "
            "prebuilt binary, so restore it from upstream rather than rebuilding."
            % (", ".join(missing), upstream / "ac_dc")]


def module_problems(modules: Sequence[str] = REQUIRED_MODULES,
                    find_spec: Callable = importlib.util.find_spec) -> List[str]:
    missing = []
    for name in modules:
        try:
            if find_spec(name) is None:
                missing.append(name)
        except (ImportError, ValueError):
            missing.append(name)
    if not missing:
        return []
    return ["modules upstream imports are missing: %s. Create the environment from "
            "codecs/rerf/environment-cu118.yml." % ", ".join(missing)]


def preflight(*, cuda_home=None, arch_list: Optional[str] = None, **options) -> BuildPlan:
    """Check this process can decode ReRF, and plan the kernel build.

    Collects every problem rather than stopping at the first, because each
    fix on a GPU server is a separate install and a one-at-a-time loop costs
    a rebuild per round.
    """
    problems = platform_problems() + entropy_coder_problems() + module_problems()
    build = None
    if not problems:
        try:
            build = plan(cuda_home=cuda_home, arch_list=arch_list, **options)
        except RuntimeError as error:
            problems.append(str(error))
    if problems:
        raise RuntimeError("ReRF runtime preflight failed:\n  - " + "\n  - ".join(problems))
    return build


# ----------------------------------------------------------------- activate ---


def _prepend(name: str, value: str, environ=None) -> None:
    environ = os.environ if environ is None else environ
    parts = [part for part in environ.get(name, "").split(os.pathsep) if part]
    if value not in parts:
        environ[name] = os.pathsep.join([value] + parts)


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


def activate(*, cuda_home: Optional[str] = None, arch_list: Optional[str] = None) -> Path:
    """Make ``import lib.dvgo`` work in this process. Idempotent.

    Runs :func:`preflight` first, so a wrong interpreter, a CPU-only torch or a
    toolkit torch will not build with fails here with the fix, instead of as a
    loader error or a ninja log several minutes later.
    """
    global _activated, PLAN
    if not (UPSTREAM / "run.py").is_file():
        raise RuntimeError(
            f"upstream ReRF is missing from {UPSTREAM}; clone "
            "https://github.com/aoliao12138/ReRF there (see ../README.md)"
        )
    if _activated:
        return UPSTREAM
    build = preflight(cuda_home=cuda_home, arch_list=arch_list)
    patch_dependencies()
    apply(build)
    PLAN = build

    for name in AC_DC_LIBRARIES:
        _preloaded.append(ctypes.CDLL(str(UPSTREAM / "ac_dc" / name), mode=ctypes.RTLD_GLOBAL))

    for path in (str(UPSTREAM), str(UPSTREAM / "ac_dc")):
        if path not in sys.path:
            sys.path.insert(0, path)
    _activated = True
    print("ReRF runtime:\n" + build.describe(), file=sys.stderr, flush=True)
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


def main(argv=None) -> int:
    """Print the build plan, or every reason there is none."""
    try:
        build = preflight()
    except RuntimeError as error:
        print(error, file=sys.stderr)
        return 1
    print(build.describe())
    for name, value in build.environment.items():
        print("export %s=%s" % (name, value))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
