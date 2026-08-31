import torch

from vega.culling import aabb_intersects_frustum, early_cull_from_boxes
from vega.cameras import Camera, look_at_RT
import math


def _cam(device="cuda"):
    R, T = look_at_RT(eye=torch.tensor([0., 0., -5.]).numpy(), target=torch.tensor([0., 0., 0.]).numpy())
    return Camera(R=R, T=T, fovx=math.radians(60), fovy=math.radians(60), width=128, height=128, device=device)


def test_box_at_target_is_visible():
    cam = _cam()
    planes = cam.frustum_planes_world()
    box_min = torch.tensor([-0.1, -0.1, -0.1])
    box_max = torch.tensor([0.1, 0.1, 0.1])
    assert aabb_intersects_frustum(box_min, box_max, planes) is True


def test_box_far_to_the_side_is_culled():
    cam = _cam()
    planes = cam.frustum_planes_world()
    box_min = torch.tensor([100.0, -0.1, -0.1])
    box_max = torch.tensor([100.2, 0.1, 0.1])
    assert aabb_intersects_frustum(box_min, box_max, planes) is False


def test_box_behind_camera_is_culled():
    cam = _cam()
    planes = cam.frustum_planes_world()
    box_min = torch.tensor([-0.1, -0.1, -10.1])
    box_max = torch.tensor([0.1, 0.1, -9.9])
    assert aabb_intersects_frustum(box_min, box_max, planes) is False


def test_early_cull_from_boxes_partitions_all_objects():
    cam = _cam()
    boxes = {
        0: (torch.tensor([-0.1, -0.1, -0.1]), torch.tensor([0.1, 0.1, 0.1])),  # visible (at target)
        1: (torch.tensor([500.0, -0.1, -0.1]), torch.tensor([500.2, 0.1, 0.1])),  # culled (far to the side)
    }
    visible, culled = early_cull_from_boxes(boxes, cam)
    assert visible == {0}
    assert culled == {1}
