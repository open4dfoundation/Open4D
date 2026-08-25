from __future__ import annotations

import json
from importlib.util import find_spec
from pathlib import Path

import pytest

pytestmark = [pytest.mark.cpu, pytest.mark.slow]


def test_notebook_default_artifacts_are_repository_relative(tmp_path, monkeypatch):
    root = Path(__file__).resolve().parents[3]
    notebook_path = root / "examples/open4d_sequence_codec.ipynb"
    notebook = json.loads(notebook_path.read_text(encoding="utf-8"))
    first_code = next(
        "".join(cell["source"])
        for cell in notebook["cells"]
        if cell["cell_type"] == "code"
    )
    monkeypatch.chdir(root)
    monkeypatch.setenv("OPEN4D_DATASET", str(tmp_path / "capture"))
    monkeypatch.delenv("OPEN4D_ARTIFACT_DIR", raising=False)
    namespace = {"__name__": "__notebook_path_test__"}

    exec(compile(first_code, str(notebook_path), "exec"), namespace)

    assert namespace["ARTIFACT_DIR"] == root / ".context/rafa_codecs"


def test_sequence_codec_notebook_executes_real_data_headlessly(
    tmp_path, monkeypatch
):
    root = Path(__file__).resolve().parents[3]
    if not (root / "4d_files/Rafa_Approves_hd_4k").is_dir():
        pytest.skip("Rafa_Approves_hd_4k is not available")
    notebook_path = root / "examples/open4d_sequence_codec.ipynb"
    notebook = json.loads(notebook_path.read_text(encoding="utf-8"))
    monkeypatch.chdir(root)
    monkeypatch.setenv("OPEN4D_ARTIFACT_DIR", str(tmp_path))
    monkeypatch.setenv("OPEN4D_DEMO_FRAMES", "2")
    monkeypatch.setenv("OPEN4D_NOTEBOOK_HEADLESS", "1")

    namespace = {"__name__": "__notebook_test__"}
    for cell in notebook["cells"]:
        if cell["cell_type"] == "code":
            source = "".join(cell["source"])
            exec(compile(source, str(notebook_path), "exec"), namespace)

    assert namespace["info"].frame_count == 157
    assert len(namespace["demo"]) == 2
    rows = {row["codec"]: row for row in namespace["results"]}
    assert set(rows) == set(namespace["CODECS"]) == set(namespace["CODEC_INFOS"])
    reference = {"raw", "deflate", "bzip2", "lzma", "rle", "npz"}
    assert all(rows[codec]["status"] == "ok" for codec in reference)
    assert not {"tvmc", "tsmc"} & set(rows)
    for codec, row in rows.items():
        if row["status"] == "ok":
            suffix = namespace["CODEC_INFOS"][codec].suffixes[0]
            assert (tmp_path / f"rafa-{codec}{suffix}").is_file()
            assert row["surface_rms"] < namespace["QUALITY_RMS_LIMIT"]
            assert row["components"] <= namespace["QUALITY_COMPONENT_LIMIT"]
            assert row["triangles"] >= namespace["QUALITY_MIN_TRIANGLES"]
    if all(find_spec(module) for module in (
        "torch", "trimesh", "skimage", "point_cloud_utils"
    )):
        assert rows["n4mc"]["status"] == "ok", rows["n4mc"]
