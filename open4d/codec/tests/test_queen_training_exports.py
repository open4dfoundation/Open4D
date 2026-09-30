import ast
from pathlib import Path
from types import SimpleNamespace

import pytest


def export_function():
    tree = ast.parse(Path(__file__).parents[2].joinpath("reconstruction/queen/train.py").read_text())
    function = next(node for node in tree.body
                    if isinstance(node, ast.FunctionDef) and node.name == "_save_frame_outputs")
    namespace = {}
    exec(compile(ast.Module(body=[function], type_ignores=[]), "train.py", "exec"), namespace)
    return namespace[function.name]


class Gate:
    training = True

    def eval(self):
        self.training = False

    def train(self, value):
        self.training = value


@pytest.mark.parametrize("frame_idx", [1, 2])
def test_dense_and_compressed_exports_use_final_state(frame_idx):
    gate = Gate()
    state = SimpleNamespace(points=4681, gate_atts=gate)
    saved = []

    def save(iteration, **kwargs):
        assert not gate.training
        assert kwargs == {"save_point_cloud": True}
        saved.append(("ply", state.points))

    def save_compressed(iteration, parameters):
        assert not gate.training
        saved.append(("compressed", state.points))

    scene = SimpleNamespace(save=save, save_compressed=save_compressed)
    export_function()(scene, state, SimpleNamespace(log_ply=True, log_compressed=True),
                      frame_idx, 70, None)
    expected = [("ply", 4681)] + ([("compressed", 4681)] if frame_idx > 1 else [])
    assert saved == expected
    assert gate.training


def test_failed_export_restores_gate_mode():
    gate = Gate()

    def fail(*args, **kwargs):
        raise OSError("disk full")

    with pytest.raises(OSError, match="disk full"):
        export_function()(SimpleNamespace(save=fail), SimpleNamespace(gate_atts=gate),
                          SimpleNamespace(log_ply=True, log_compressed=True), 1, 70, None)
    assert gate.training


def test_camera_iteration_does_not_publish_canonical_state():
    tree = ast.parse(Path(__file__).parents[2].joinpath("reconstruction/queen/train.py").read_text())
    training = next(node for node in tree.body
                    if isinstance(node, ast.FunctionDef) and node.name == "training")
    canonical = [node for node in ast.walk(training) if isinstance(node, ast.Call)
                 and any(key.arg == "save_point_cloud" for key in node.keywords)]
    assert canonical == []
