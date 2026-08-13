"""Gaussian-splatting FVV reconstruction for Open4D.

Wraps two pinned upstream methods -- QUEEN and 3DGStream -- behind one
environment, one CUDA rasterizer, and one CLI. See ../README.md, and
../docs/plan.md for what is built and what is still gated.

Not MIT: derived from QUEEN under the NVIDIA License, non-commercial research or
evaluation only. See ../THIRD_PARTY.md.
"""

__version__ = "0.1.0"
