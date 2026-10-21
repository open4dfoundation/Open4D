"""Keep TVMC's setup entry point aligned with the shared codec environment."""

from __future__ import annotations

from tests import ROOT

from pathlib import Path

import pytest


pytestmark = pytest.mark.cpu


def test_tvmc_setup_accepts_the_repository_python_version():
    root = ROOT
    setup = (root / "open4d/codecs/tvmc/source/setup.sh").read_text(encoding="utf-8")

    assert "python3.12" in setup
    assert "Python 3.8-3.11 is required" not in setup
    assert "Open3D 0.18 requires Python 3.8-3.11" not in setup
    assert '"$VENV_PYTHON" -c' in setup
    assert "remove it and rerun setup" in setup
