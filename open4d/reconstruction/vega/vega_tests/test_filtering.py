import torch

from vega.filtering import apply_filtering, plan_filtering
from vega.gaussians import GaussianSet


def _make_gs(object_id, xyz_val, device="cuda"):
    n = object_id.shape[0]
    xyz = torch.full((n, 3), xyz_val, device=device)
    scale_raw = torch.zeros(n, 3, device=device)
    rot_raw = torch.zeros(n, 4, device=device)
    rot_raw[:, 0] = 1.0
    opacity_raw = torch.zeros(n, 1, device=device)
    sh_dc = torch.zeros(n, 1, 3, device=device)
    sh_rest = torch.zeros(n, 0, 3, device=device)
    return GaussianSet(xyz=xyz, scale_raw=scale_raw, rot_raw=rot_raw, opacity_raw=opacity_raw,
                        sh_dc=sh_dc, sh_rest=sh_rest, object_id=object_id, sh_degree=0)


def test_plan_filtering_partitions_objects():
    plan = plan_filtering(all_object_ids=[0, 1, 2, 3], dynamic_objects={1, 3})
    assert plan.transmitted_objects == {1, 3}
    assert plan.reused_objects == {0, 2}


def test_static_objects_reuse_key_frame_attrs():
    object_id = torch.tensor([0, 0, 1, 1], device="cuda")
    key_gs = _make_gs(object_id, xyz_val=1.0)       # key frame: everything at x=1
    residual_gs = _make_gs(object_id, xyz_val=9.0)   # residual frame: everything moved to x=9

    # object 0 is static (reused from key), object 1 is dynamic (kept from residual)
    plan = plan_filtering([0, 1], dynamic_objects={1})
    recon = apply_filtering(residual_gs, key_gs, plan)

    # apply_filtering reassembles by concatenating per-object slices, so row
    # order isn't guaranteed to match the input — index via recon's own ids.
    assert recon.xyz.shape[0] == 4
    assert torch.all(recon.xyz[recon.object_mask(0)] == 1.0)   # reused key-frame position
    assert torch.all(recon.xyz[recon.object_mask(1)] == 9.0)   # kept residual (dynamic) position
