"""Keep the aggregate installation extra in sync with public features."""

from __future__ import annotations

from pathlib import Path

import pytest

try:
    import tomllib
except ModuleNotFoundError:
    import tomli as tomllib

pytestmark = pytest.mark.cpu


def test_all_extra_contains_every_runtime_feature_dependency():
    root = Path(__file__).resolve().parents[2]
    with (root / "pyproject.toml").open("rb") as stream:
        extras = tomllib.load(stream)["project"]["optional-dependencies"]

    feature_dependencies = set().union(*(
        dependencies
        for name, dependencies in extras.items()
        if name not in {"all", "dev"}
    ))

    assert feature_dependencies <= set(extras["all"])
