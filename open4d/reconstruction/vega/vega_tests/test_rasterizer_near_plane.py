"""Exercise the compiled rasterizer at normal capture distances."""
import math

import numpy as np
import pytest

torch = pytest.importorskip("torch")
if not torch.cuda.is_available():
    pytest.skip("CUDA rasterizer required", allow_module_level=True)
pytest.importorskip("diff_gaussian_rasterization")
from vega.cameras import Camera
from vega.gaussians import GaussianSet
from vega.rasterize import render


@pytest.mark.parametrize("distance", [.5, 2., 5.])
def test_visible_gaussian_within_four_units_has_pixels_and_gradients(distance):
    cam = Camera(np.eye(3, dtype=np.float32), np.zeros(3, dtype=np.float32),
                 math.radians(60), math.radians(60), 64, 64, device="cuda")
    gs = GaussianSet(
        xyz=torch.tensor([[0., 0., distance]], device="cuda", requires_grad=True),
        scale_raw=torch.full((1, 3), math.log(.05), device="cuda"),
        rot_raw=torch.tensor([[1., 0., 0., 0.]], device="cuda"),
        opacity_raw=torch.full((1, 1), 3., device="cuda"),
        sh_dc=torch.zeros(1, 1, 3, device="cuda"),
        sh_rest=torch.zeros(1, 0, 3, device="cuda"),
        object_id=torch.zeros(1, dtype=torch.long, device="cuda"), sh_degree=0)
    output = render(cam, gs, torch.zeros(3, device="cuda"))
    assert output['radii'].item() > 0
    assert output['render'].max().item() > .1
    output['render'].sum().backward()
    assert torch.isfinite(gs.xyz.grad).all()
    assert gs.xyz.grad.abs().sum().item() > 0
