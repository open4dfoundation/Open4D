"""Runnable tutorial contracts, using isolated outputs and no public network."""

import json
import os
from pathlib import Path
import subprocess
import sys

import pytest


from tests import ROOT
TUTORIALS = ROOT / "examples/tutorials"


def run(name, *args, cwd, check=True):
    # Source tests use this revision even when the interpreter has an older
    # package installed. Fresh-wheel verification is performed separately.
    environment = dict(os.environ, PYTHONPATH=str(ROOT))
    result = subprocess.run([sys.executable, str(TUTORIALS / name), *map(str, args)],
                            cwd=cwd, env=environment, text=True, capture_output=True, timeout=30)
    if check:
        assert result.returncode == 0, result.stdout + result.stderr
    return result


def test_core_roundtrip_produces_all_three_representations(tmp_path):
    output = tmp_path / "core"
    result = run("core_roundtrip.py", "--output", output, cwd=tmp_path)
    document = json.loads(result.stdout)
    assert document["synthetic"]
    assert [entry["representation"] for entry in document["artifacts"]] == ["mesh", "points", "gaussians"]
    assert all(entry["frames"] == 8 for entry in document["artifacts"])
    for entry in document["artifacts"]:
        assert Path(entry["path"]).is_file()
    rejected = run("core_roundtrip.py", "--output", output, cwd=tmp_path, check=False)
    assert rejected.returncode != 0


def test_data_cache_defaults_to_synthetic_and_rejects_incomplete_download(tmp_path):
    result = run("data_cache.py", "--cache", tmp_path / "cache", cwd=tmp_path)
    document = json.loads(result.stdout)
    assert document["synthetic"] and document["license"] == "MIT"
    assert all(Path(path).is_dir() for path in document["paths"])
    rejected = run("data_cache.py", "--cache", tmp_path / "external",
                   "--url", "https://invalid.example/asset", cwd=tmp_path, check=False)
    assert rejected.returncode != 0 and "requires --sha256" in rejected.stderr
    assert not (tmp_path / "external").exists()


def test_tcp_tutorial_records_frames(tmp_path):
    output = tmp_path / "recording.o4d"
    result = run("tcp_stream.py", "--output", output, cwd=tmp_path)
    document = json.loads(result.stdout)
    assert document["frames"] == 4 and document["wire_bytes"] > document["payload_bytes"] > 0
    assert output.is_file()


@pytest.mark.parametrize("name", [
    "core_roundtrip.py", "data_cache.py", "rgbd_capture.py", "train_generic.py",
    "tcp_stream.py", "browser_stream.py", "compare_visualization.py",
])
def test_optional_examples_describe_their_cli_without_optional_imports(tmp_path, name):
    result = run(name, "--help", cwd=tmp_path)
    assert "usage:" in result.stdout
