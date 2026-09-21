"""Native training control-flow regressions; run with 3DGStream dependencies."""
from types import SimpleNamespace

import pytest

torch = pytest.importorskip("torch")
pytest.importorskip("tinycudann")
import train_frames as training
from ntc import NeuralTransformationCache


def test_no_test_cameras_produces_no_evaluation_result():
    scene = SimpleNamespace(getTestCameras=lambda: [])
    result = training.training_report(None, 1, None, None, None, None, 0, [1], scene, None, ())
    assert result is None


@pytest.mark.parametrize("start", [1, 2])
def test_default_temporal_training_loads_previous_saved_model(tmp_path, monkeypatch, start):
    video = tmp_path / "input"
    video.mkdir()
    for index in range(4):
        (video / f"frame{index:06d}").mkdir()
    args = SimpleNamespace(quiet=True, video_path=str(video), output_path=str(tmp_path/'output'),
                           model_path=str(tmp_path/'initial'), load_iteration=None,
                           first_load_iteration=30, frame_start=start, frame_end=4)
    calls = []
    monkeypatch.setattr(training, "safe_state", lambda _: None)
    monkeypatch.setattr(training, "train_one_frame", lambda lp, op, pp, a: calls.append((a.model_path, a.output_path, a.load_iteration)))
    training.train_frames(None, None, None, args)
    assert calls[0][2] == (30 if start == 1 else -1)
    for previous, current in zip(calls, calls[1:]):
        assert current[0] == previous[1]
        assert current[2] == -1


def test_warmed_cache_adapts_disjoint_scene_bounds_without_changing_weights():
    model = torch.nn.Linear(3, 8)
    original = {key: value.clone() for key, value in model.state_dict().items()}
    cache = NeuralTransformationCache(model, torch.tensor([-20., -15., 5.]), torch.tensor([15., 10., 23.]))
    xyz = torch.tensor([[0., 0., 0.], [1., 0., 0.], [-1., 0., 0.]])
    with pytest.warns(RuntimeWarning, match="bounds"):
        cache.ensure_scene_coverage(xyz)
    normalized = cache.get_contracted_xyz(xyz)
    assert torch.isfinite(normalized).all()
    assert ((normalized >= 0) & (normalized <= 1)).all()
    for key, value in model.state_dict().items():
        torch.testing.assert_close(value, original[key])
    bounds = cache.xyz_bound_min.clone(), cache.xyz_bound_max.clone()
    cache.ensure_scene_coverage(xyz)
    torch.testing.assert_close(cache.xyz_bound_min, bounds[0])
    torch.testing.assert_close(cache.xyz_bound_max, bounds[1])
