import json
from pathlib import Path
import subprocess
import sys

import pytest

from open4d.codecs._backends import _manager, _recipe, _registry


def test_recipe_catalog_is_pinned_or_explicitly_attach_only():
    for name in _recipe.names():
        recipe = _recipe.load(name)
        for profile in recipe["profiles"].values():
            assert all(len(source["commit"]) == 40 for source in profile.get("sources", []))


def test_attach_changes_runtime_lookup_and_doctor_checks_it(tmp_path, monkeypatch):
    from open4d._research_paths import default_runtime, managed_python
    source = tmp_path / "source"
    source.mkdir()
    (source / "compress.py").write_text("# fixture\n")
    store = tmp_path / "store"
    monkeypatch.setenv("OPEN4D_BACKEND_HOME", str(store))
    monkeypatch.delenv("OPEN4D_RESEARCH_ROOT", raising=False)
    result = _manager.attach("qndf", source, python=sys.executable)
    assert result["ok"] and not result["runtime_verified"]
    assert default_runtime("qndf") == source
    assert Path(managed_python("qndf")).samefile(sys.executable)
    source.rename(tmp_path / "gone")
    assert not _manager.doctor("qndf")["ok"]


def test_build_failure_is_not_registered(tmp_path, monkeypatch):
    recipe = _recipe.load("draco")
    recipe = {k: v for k, v in recipe.items() if not k.startswith("_")}
    import platform
    recipe["profiles"] = {"test": {"platform": {"system": platform.system(), "machine": [platform.machine()]},
         "unresolved": [], "steps": [{"name": "fail", "argv": ["{python}", "-c", "raise SystemExit(8)"]}]}}
    recipe_path = tmp_path / "recipe.json"
    recipe_path.write_text(json.dumps(recipe))
    store = tmp_path / "store"
    with pytest.raises(_recipe.RecipeError, match="command failed"):
        _manager.install("draco", recipe_path=recipe_path, store=store, accept_license=True)
    assert _registry.load(store)["backends"] == {}
    assert not (store / ".install.lock").exists()
    assert not list(store.glob(".draco-*"))


def test_tampered_binary_fails_doctor(tmp_path):
    binary = tmp_path / "tool"
    binary.write_bytes(b"original runtime")
    _registry.record({"name": "draco", "state": "installed", "components": {"draco": str(tmp_path)},
                      "hashes": {str(binary): _recipe.file_digest(binary)}}, tmp_path / "store")
    binary.write_bytes(b"modified runtime")
    assert not _manager.doctor("draco", store=tmp_path / "store")["ok"]


def test_explicit_vdmc_root_takes_precedence_over_registered_tools(tmp_path, monkeypatch):
    from open4d.codecs import vdmc as _o4d
    tool = tmp_path / "build/Release/bin/decode"
    tool.parent.mkdir(parents=True)
    tool.write_text("#!/bin/sh\nexit 0\n")
    tool.chmod(0o755)
    monkeypatch.setenv("OPEN4D_VDMC_ROOT", str(tmp_path))
    monkeypatch.delenv("OPEN4D_VDMC_DECODER", raising=False)
    monkeypatch.setattr(_o4d, "managed_tool", lambda *a: pytest.fail("explicit root must win"))
    assert _o4d._executable(None, "vdmc", "decoder") == tool
