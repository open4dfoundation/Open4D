"""Put the example's flat modules on `sys.path` for the tests.

`examples/visualization` is a directory of flat modules rather than a package —
the programs import `frame_sources`, not `examples.visualization.frame_sources` —
so the tests have to reach them the same way the programs do. `_common` then puts
the repository root on the path, which is what makes `open4d` importable in an
uninstalled clone.
"""

from __future__ import annotations

import sys
from pathlib import Path

EXAMPLE_ROOT = Path(__file__).resolve().parents[1]

if str(EXAMPLE_ROOT) not in sys.path:
    sys.path.insert(0, str(EXAMPLE_ROOT))
