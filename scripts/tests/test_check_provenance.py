"""Regression tests for fail-closed provenance component discovery."""

from __future__ import annotations

from pathlib import Path

import pytest

from check_provenance import (
    discover_required_ledger_paths,
    parse_gitmodule_paths,
    uncovered_component_paths,
)

pytestmark = pytest.mark.cpu


def test_gitmodule_paths_are_parsed_from_configuration_not_a_manual_list():
    gitmodules = """
[submodule "open4d/codecs/faster_vdmc"]
    path = open4d/codecs/faster_vdmc
    url = https://example.invalid/faster-vdmc.git
[submodule "vendor/tool"]
    path = vendor/tool
    url = https://example.invalid/tool.git
"""

    assert parse_gitmodule_paths(gitmodules) == {
        "open4d/codecs/faster_vdmc",
        "vendor/tool",
    }


def test_component_discovery_covers_new_directories_and_submodules(tmp_path: Path):
    (tmp_path / "open4d/codecs/existing").mkdir(parents=True)
    (tmp_path / "open4d/reconstruction/capture").mkdir(parents=True)
    (tmp_path / "integrations/unity").mkdir(parents=True)
    (tmp_path / ".gitmodules").write_text(
        """
[submodule "open4d/codecs/faster_vdmc"]
    path = open4d/codecs/faster_vdmc
    url = https://example.invalid/faster-vdmc.git
[submodule "vendor/tool"]
    path = vendor/tool
    url = https://example.invalid/tool.git
""",
        encoding="utf-8",
    )

    assert discover_required_ledger_paths(tmp_path) == {
        "integrations/unity",
        "open4d/codecs/existing",
        "open4d/codecs/faster_vdmc",
        "open4d/reconstruction/capture",
        "vendor/tool",
    }


def test_nested_submodules_are_covered_by_their_top_level_component(tmp_path: Path):
    (tmp_path / "open4d/codecs/qndf").mkdir(parents=True)
    (tmp_path / ".gitmodules").write_text(
        """
[submodule "open4d/codecs/qndf/ssp_remesh/libigl"]
    path = open4d/codecs/qndf/ssp_remesh/libigl
    url = https://example.invalid/libigl.git
""",
        encoding="utf-8",
    )

    assert discover_required_ledger_paths(tmp_path) == {
        "integrations/unity",
        "open4d/codecs/qndf",
    }


def test_an_unledgered_discovered_component_fails_coverage():
    required = {
        "open4d/codecs/draco",
        "open4d/codecs/faster_vdmc",
    }
    ledger = {"open4d/codecs/draco": "EXCLUDED"}

    assert uncovered_component_paths(required, ledger) == {
        "open4d/codecs/faster_vdmc"
    }
