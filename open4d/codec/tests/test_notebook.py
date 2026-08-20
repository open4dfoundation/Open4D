from __future__ import annotations

import json
from pathlib import Path

import pytest

pytestmark = [pytest.mark.cpu, pytest.mark.slow]


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
    assert [row[0] for row in namespace["results"]] == [
        "raw", "deflate", "bzip2", "lzma", "rle"
    ]
    assert all((tmp_path / f"rafa-{codec}.o4d").is_file()
               for codec in namespace["CODECS"])
