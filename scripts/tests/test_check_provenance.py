"""Regression tests for fail-closed provenance component discovery."""

from __future__ import annotations

from pathlib import Path

import pytest

from check_provenance import (
    EXPLICIT_REQUIRED_LEDGER_PATHS,
    discover_required_ledger_paths,
    parse_component_ledger,
    parse_gitmodule_paths,
    release_decision_errors,
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
        *EXPLICIT_REQUIRED_LEDGER_PATHS,
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
        *EXPLICIT_REQUIRED_LEDGER_PATHS,
        "open4d/codecs/qndf",
    }


def test_component_discovery_ignores_generated_and_hidden_directories(tmp_path: Path):
    (tmp_path / "open4d/codecs/qndf").mkdir(parents=True)
    (tmp_path / "open4d/codecs/__pycache__").mkdir()
    (tmp_path / "open4d/codecs/.pytest_cache").mkdir()

    assert discover_required_ledger_paths(tmp_path) == {
        *EXPLICIT_REQUIRED_LEDGER_PATHS,
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


def test_reviewed_components_can_record_approval():
    ledger = """## Component ledger
| Path / component | Source | License | Decision | Reviewer |
| `open4d/codecs/reviewed` | revision | MIT | `APPROVED`; evidence recorded | Maintainer |
| `open4d/codecs/excluded` | revision | restricted | `EXCLUDED`; outside artifacts | Maintainer |
"""
    assert parse_component_ledger(ledger) == {
        "open4d/codecs/reviewed": "APPROVED",
        "open4d/codecs/excluded": "EXCLUDED",
    }


@pytest.mark.parametrize("ledger", [
    "", "## Release decision: pending",
    "## Release decision: blocked\n## Release decision: approved",
    "## Release decision: approved",
    "## Release decision: approved\nMaintainer approval: Unassigned (2026-10-07)",
    "## Release decision: approved\nMaintainer approval: Maintainer (2026-10-07)\n"
    "| `restricted` | source | license | `BLOCK`; unresolved | Unassigned |",
])
def test_publication_rejects_missing_or_inconsistent_clearance(ledger):
    from check_release_gate import blockers

    assert release_decision_errors(ledger)
    assert blockers(ledger)


def test_a_reviewed_ledger_can_pass_the_publication_gate():
    from check_release_gate import blockers

    ledger = "## Release decision: approved\nMaintainer approval: Maintainer (2026-10-07)"
    assert not release_decision_errors(ledger)
    assert not blockers(ledger)


def test_a_blocked_ledger_stays_blocked_even_without_component_rows():
    from check_release_gate import blockers

    ledger = "## Release decision: blocked"
    assert not release_decision_errors(ledger)
    assert blockers(ledger)
