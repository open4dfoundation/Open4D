"""Dependency-complete checks for the QNDF command-line decoders."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import pytest

pytest.importorskip("torch")
pytest.importorskip("tqdm")
pytestmark = pytest.mark.cpu


@pytest.mark.parametrize(
    "module", ("open4d.codecs.qndf.decode", "open4d.codecs.qndf_int8.decode")
)
def test_qndf_decoders_are_package_importable(module):
    __import__(module)


@pytest.mark.parametrize(
    "relative", ("open4d/codecs/qndf/decode.py", "open4d/codecs/qndf_int8/decode.py")
)
def test_qndf_decoders_run_directly_from_a_source_tree(relative, tmp_path):
    root = Path(__file__).resolve().parents[3]
    result = subprocess.run(
        [sys.executable, "-I", str(root / relative), "--help"],
        cwd=tmp_path, capture_output=True, text=True,
    )
    assert result.returncode == 0, result.stderr


def test_qndf_pruning_is_removed_before_a_plain_model_reload():
    from torch.nn.utils import prune
    from open4d.codecs.qndf.compress import MLP, _remove_pruning

    model = MLP(6, 4, 3, 1)
    parameters = ((model.layers[0][0], "weight"),)
    prune.global_unstructured(
        parameters, pruning_method=prune.L1Unstructured, amount=0.25
    )

    _remove_pruning(parameters)

    MLP(6, 4, 3, 1).load_state_dict(model.state_dict())
