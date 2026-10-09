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
import inspect
import math

#: Extension import names, in the order the unification will collapse them.
KNOWN = (
    "gaussian_rasterization_grad",  # QUEEN's superset fork; the intended survivor
    "diff_gaussian_rasterization",  # inria's, as vendored by QUEEN
    "gstream_rasterization",  # 3DGStream's, renamed by our patch to avoid the clash
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


def near_plane_check(depth: float = 1.0, names: tuple[str, ...] = KNOWN) -> dict[str, bool | None]:
    """Whether each importable rasterizer draws a splat ``depth`` units ahead.

    QUEEN's plain fork once culled everything nearer than 4 units, which blanks
    renders of rigs such as ORBIT's, about 3.4 m from the subject. The vendored
    sources now cull at 0.2 like the others, but an extension built before that
    fix still culls at 4; rebuild it with scripts/setup.sh. ``None`` means the
    extension is missing or no CUDA device is visible.
    """
    try:
        import torch
    except Exception:
        return {name: None for name in names}
    if not torch.cuda.is_available():
        return {name: None for name in names}
    cuda = {"device": "cuda", "dtype": torch.float32}
    tan = math.tan(math.radians(30))
    near, far = 0.01, 100.0
    projection = torch.zeros(4, 4, **cuda)
    projection[0, 0] = projection[1, 1] = 1 / tan
    projection[2, 2], projection[2, 3], projection[3, 2] = far / (far - near), -far * near / (far - near), 1
    values = {
        "image_height": 32, "image_width": 32, "tanfovx": tan, "tanfovy": tan,
        "bg": torch.zeros(3, **cuda), "scale_modifier": 1.0,
        # 3DGS passes row-vector matrices; the camera sits at the origin looking along +z.
        "viewmatrix": torch.eye(4, **cuda), "projmatrix": projection.T.contiguous(),
        "sh_degree": 0, "campos": torch.zeros(3, **cuda), "prefiltered": False, "debug": False,
    }
    results: dict[str, bool | None] = {}
    for name in names:
        try:
            module = importlib.import_module(name)
        except Exception:
            results[name] = None
            continue
        fields = module.GaussianRasterizationSettings._fields
        settings = module.GaussianRasterizationSettings(**{f: values.get(f, False) for f in fields})
        means = torch.tensor([[0.0, 0.0, depth]], **cuda)
        arguments = {
            "means3D": means, "means2D": torch.zeros_like(means),
            # Degree-0 SH, as every caller passes; DC 1.0 is a mid-grey splat.
            "opacities": torch.ones(1, 1, **cuda), "shs": torch.ones(1, 1, 3, **cuda),
            "scales": torch.full((1, 3), 0.1, **cuda), "rotations": torch.tensor([[1.0, 0, 0, 0]], **cuda),
        }
        if "flow3D" in inspect.signature(module.GaussianRasterizer.forward).parameters:
            arguments["flow3D"] = torch.zeros_like(means)
        with torch.no_grad():
            image = module.GaussianRasterizer(settings)(**arguments)[0]
        results[name] = bool(image.max() > 0)
    return results
