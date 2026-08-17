"""The one rasterizer both methods will call.

Phase 2 of docs/plan.md. The extension itself is QUEEN's
`gaussian-rasterization-grad`, which measurement showed is already a functional
superset of 3DGStream's fork: 3DGStream's entire delta over inria is a depth
output plus depth gradients, and QUEEN's fork has depth forward and backward
already, plus 2D flow, per-Gaussian influence and count, alpha backward, and
pixel/color/cov/update masks. So unifying is a Python-side repointing, not a CUDA
merge.

What this module will hold, once the parity test in docs/plan.md passes:

  - `Settings`, a superset of the three upstream `GaussianRasterizationSettings`,
    including the near-plane and NDC-bounds flags that `in_frustum` differs on
    (inria and 3DGStream cull on `z <= 0.2` alone; QUEEN's grad fork also culls
    against NDC bounds; QUEEN's plain fork uses `z <= 4.0`). Silently adopting one
    of those changes which Gaussians render, so it is configuration, not a
    constant.
  - `render()`, returning a named result rather than a positional 7-tuple, so a
    caller that wants only colour does not have to know the flow slots exist.

Until then `probe()` reports which extensions are importable, which is what the
parity test and `gs-tools doctor` need.
"""

from __future__ import annotations

import importlib

#: Extension import names, in the order the unification will collapse them.
KNOWN = (
    "gaussian_rasterization_grad",  # QUEEN's superset fork; the intended survivor
    "diff_gaussian_rasterization",  # inria's, or 3DGStream's -- same import name
)


def probe() -> dict[str, str | None]:
    """Which rasterizer extensions this environment can import."""
    found: dict[str, str | None] = {}
    for name in KNOWN:
        try:
            module = importlib.import_module(name)
        except Exception:
            found[name] = None
            continue
        found[name] = str(getattr(module, "__file__", "unknown"))
    return found
