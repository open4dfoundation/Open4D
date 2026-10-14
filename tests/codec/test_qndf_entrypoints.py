"""Dependency-complete checks for the QNDF command-line decoders."""

from __future__ import annotations

from tests import ROOT
from open4d.codec._research import research_module

import subprocess
import sys
from pathlib import Path

import pytest

pytest.importorskip("torch")
pytest.importorskip("tqdm")
pytestmark = pytest.mark.cpu


@pytest.mark.parametrize(
    "module", ("qndf.decode", "qndf_int8.decode")
)
def test_qndf_decoders_are_package_importable(module):
    research_module(module)


@pytest.mark.parametrize(
    "relative", ("codecs/qndf/decode.py", "codecs/qndf_int8/decode.py")
)
def test_qndf_decoders_run_directly_from_a_source_tree(relative, tmp_path):
    root = ROOT
    result = subprocess.run(
        [sys.executable, "-I", str(root / relative), "--help"],
        cwd=tmp_path, capture_output=True, text=True,
    )
    assert result.returncode == 0, result.stderr


def test_qndf_pruning_is_removed_before_a_plain_model_reload():
    from torch.nn.utils import prune
    compress = research_module("qndf.compress")
    MLP, _remove_pruning = compress.MLP, compress._remove_pruning

    model = MLP(6, 4, 3, 1)
    parameters = ((model.layers[0][0], "weight"),)
    prune.global_unstructured(
        parameters, pruning_method=prune.L1Unstructured, amount=0.25
    )

    _remove_pruning(parameters)

    MLP(6, 4, 3, 1).load_state_dict(model.state_dict())
