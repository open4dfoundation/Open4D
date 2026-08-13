"""What is actually installed, for `gs-tools doctor` and for run manifests.

Both upstreams were tested on configurations we are not using -- QUEEN declares
Python 3.11, 3DGStream was tested on 3.8 with torch 2.0.1+cu118 -- so the
environment is a standing suspect whenever a run misbehaves, and no result should
be recorded without it.
"""

from __future__ import annotations

import importlib
import platform
import shutil
import subprocess
import sys
from typing import Any

from . import paths

# Import name -> what breaks without it. Absence is reported, never raised: only
# 3DGStream needs tinycudann, and timm belongs to the separate MiDaS environment,
# so a partial environment is a legitimate state to be in.
OPTIONAL_IMPORTS = {
    "torch": "everything",
    "torchvision": "everything",
    "cv2": "both trainers",
    "plyfile": "point-cloud IO",
    "kornia": "3DGStream training",
    "torchac": "QUEEN entropy coding",
    "torchmetrics": "QUEEN depth loss",
    "einops": "QUEEN",
    "tinycudann": "3DGStream neural transformation cache",
    "timm": "MiDaS depth priors (expected absent; see requirements-midas.txt)",
    "gaussian_rasterization_grad": "unified rasterizer (built by scripts/setup.sh)",
    "simple_knn": "point-cloud init (built by scripts/setup.sh)",
}


def _version(name: str) -> str | None:
    try:
        module = importlib.import_module(name)
    except Exception:
        # Broad on purpose: an extension built against the wrong torch raises
        # ImportError, OSError, or RuntimeError, and all three mean "unusable".
        return None
    return str(getattr(module, "__version__", "unknown"))


def _nvcc() -> str | None:
    exe = shutil.which("nvcc")
    if exe is None:
        return None
    try:
        out = subprocess.run(
            [exe, "--version"], capture_output=True, text=True, timeout=30, check=False
        ).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    return next((ln.strip() for ln in out.splitlines() if "release" in ln), exe)


def _git(tree, *args: str) -> str:
    try:
        return subprocess.run(
            ["git", "-C", str(tree), *args],
            capture_output=True,
            text=True,
            timeout=30,
            check=False,
        ).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return ""


def _upstream_commit(name: str) -> str | None:
    tree = paths.upstream(name)
    if not tree.exists():
        return None
    sha = _git(tree, "rev-parse", "HEAD")
    if not sha:
        return None
    # A dirty upstream tree means vendored code was edited in place, which the
    # patch discipline exists to prevent. Record it rather than reporting a
    # commit that is not what ran.
    return f"{sha}-dirty" if _git(tree, "status", "--porcelain") else sha


def report() -> dict[str, Any]:
    """Collect the environment description used by `doctor` and by manifests."""
    rep: dict[str, Any] = {
        "python": sys.version.split()[0],
        "platform": platform.platform(),
        "torch": None,
        "torch_cuda": None,
        "cuda_devices": [],
        "nvcc": _nvcc(),
        "imports": {name: _version(name) for name in OPTIONAL_IMPORTS},
        "upstream": {name: _upstream_commit(name) for name in ("queen", "3dgstream")},
    }
    try:
        import torch

        rep["torch"] = torch.__version__
        rep["torch_cuda"] = torch.version.cuda
        if torch.cuda.is_available():
            rep["cuda_devices"] = [
                torch.cuda.get_device_name(i) for i in range(torch.cuda.device_count())
            ]
    except Exception:
        pass
    return rep


def doctor() -> int:
    """Print the environment report. Returns a process exit status."""
    rep = report()
    print(f"python    {rep['python']}")
    print(f"platform  {rep['platform']}")
    print(f"torch     {rep['torch'] or 'MISSING'}  (cuda {rep['torch_cuda'] or '-'})")
    print(f"nvcc      {rep['nvcc'] or 'not on PATH'}")
    for i, dev in enumerate(rep["cuda_devices"]):
        print(f"  device {i}  {dev}")
    if not rep["cuda_devices"]:
        print("  device    none visible")

    print("\nimports")
    for name, why in OPTIONAL_IMPORTS.items():
        print(f"  {name:<30} {rep['imports'][name] or '-':<12} {why}")

    print("\nupstream")
    for name, sha in rep["upstream"].items():
        print(f"  {name:<30} {sha or 'not checked out -- run scripts/setup.sh'}")

    # Only torch is a hard failure. Reporting a missing tinycudann as an error
    # would make `doctor` useless for QUEEN-only work.
    missing = [n for n in ("torch", "torchvision") if not rep["imports"][n]]
    if missing:
        print(f"\nFAIL: {', '.join(missing)} not importable")
        return 1
    if platform.system() != "Linux":
        print("\nnote: training needs Linux and an NVIDIA GPU; this host is neither")
    return 0
