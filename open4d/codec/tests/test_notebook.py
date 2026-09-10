from __future__ import annotations

import ast
import json
import os
from pathlib import Path

import pytest


NOTEBOOK = Path(__file__).resolve().parents[3] / "examples/open4d_sequence_codec.ipynb"


def run_cells(tags):
    namespace = {"__name__": "__notebook__"}
    for cell in json.loads(NOTEBOOK.read_text())["cells"]:
        if cell["cell_type"] != "code":
            continue
        source = "".join(cell["source"])
        ast.parse(source)
        required = set(cell["metadata"].get("tags", ()))
        if required <= tags:
            exec(compile(source, str(NOTEBOOK), "exec"), namespace)
    return namespace


@pytest.mark.cpu
def test_notebook_sample_needs_no_dataset(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    values = run_cells({"cpu"})
    assert values["sequence"].closed
    assert not list(tmp_path.iterdir())


@pytest.mark.open3d
def test_notebook_depth_example_reconstructs_real_geometry(tmp_path, monkeypatch):
    pytest.importorskip("open3d")
    monkeypatch.chdir(tmp_path)
    values = run_cells({"cpu", "open3d"})
    with values["reconstructed"] as sequence:
        assert len(sequence) == 3
        assert len(sequence[0].geometry.triangles) > 0


@pytest.mark.slow
def test_notebook_native_vdmc_round_trip(tmp_path, monkeypatch):
    if not all(os.environ.get(name) for name in
               ("OPEN4D_VDMC_ENCODER", "OPEN4D_VDMC_DECODER")):
        pytest.skip("configure the native V-DMC encoder and decoder")
    monkeypatch.chdir(tmp_path)
    values = run_cells({"cpu", "native-vdmc"})
    assert values["encoded"].is_file()
    assert values["decoded"].closed
    assert values["decoded"].topology.value == "unknown"
    assert values["decoded"].has_constant_vertex_count is None
    assert values["decoded"].has_vertex_correspondence is None
