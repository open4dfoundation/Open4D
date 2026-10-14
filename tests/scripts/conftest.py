"""Make the repository's standalone validation scripts importable in tests."""

from __future__ import annotations

from tests import ROOT

import sys
from pathlib import Path


SCRIPTS = ROOT / "scripts"
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))
