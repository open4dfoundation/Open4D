"""Render a ReRF bitstream at the capture rig's own cameras. Runs under Python 3.8.

Executed as a subprocess by `gs_tools.methods.rerf.render_at_rig`, never
imported by the rest of this module: it needs ReRF, and ReRF needs the entropy
coder that only exists as a CPython 3.8 binary.

`rerf_render.py` has exactly one render path, `--render_360`, which synthesises
its own orbit and advances the decode stream once per image. Neither is what a
comparison wants:

* The orbit is nowhere the scene was captured, so there is no photograph to put
  beside it -- and `angle = 2*pi*i/360` means 30 frames sweep 29 degrees, with
  camera and time welded together.
* One decode per image makes rendering V views of one instant cost V decodes,
  and the stream is sequential, so it cannot be rewound to do it at all.

Both are fixed by substituting two things upstream computes rather than by
reimplementing anything around them. The poses become `data_dict['poses']` at
the training views -- which *are* the rig cameras, already in ReRF's own
normalised frame, so nothing is converted and nothing can be misregistered --
and the model callback advances the decode once per timestep instead of once per
image, so every view of one instant renders from the same volume.

The substitution is two single-line replacements made on the source text at load
time. The vendored tree stays byte-identical, which is the same discipline
`nevo/rerf_env.py` applies to its own upstream fixes; anchoring on one line each
is what keeps a pin bump from silently changing what this renders.
"""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path

#: The line after which upstream's poses and callback are ours to replace.
CALLBACK_ANCHOR = "    render_viewpoints_kwargs['model_callback'] = model_callback\n"
OUTPUT_ANCHOR = (
    "    testsavedir = os.path.join(cfg.basedir, cfg.expname, "
    "f'render_360_rerf_{args.render_360}')\n"
)

# Placed *after* upstream's own pose loop, so whatever it computed is simply
# discarded -- cheaper to reason about than excising the loop, and it leaves the
# surrounding setup (near/far, stepsize, the first decoded frame) untouched.
INJECTION = '''
    # ---- injected by gs_tools.methods._rerf_rig_render ----------------------
    import json as _json
    _plan = _json.load(open(os.environ["OPEN4D_RERF_PLAN"]))
    _train = data_dict['i_train']
    _views = [int(v) for v in _plan["views"]]
    _times = [int(t) for t in _plan["times"]]
    def _host(value):
        # `load_everything_frame` leaves poses and intrinsics on the GPU, and
        # numpy refuses to convert a CUDA tensor. Upstream never hits this
        # because it rebuilds its orbit poses in numpy from scratch.
        return value.detach().cpu().numpy() if hasattr(value, "detach") else np.asarray(value)

    sTs, sKs, HWs, frame_ids = [], [], [], []
    for _t in _times:
        for _v in _views:
            sTs.append(_host(data_dict['poses'][_train[_v]]))
            sKs.append(_host(data_dict['Ks'][_train[_v]]))
            HWs.append([int(n) for n in _host(data_dict['HW'][_train[_v]])])
            frame_ids.append(_t)
    sTs = np.stack(sTs)
    sKs = np.stack(sKs)
    print(f"gs-tools: {len(_views)} views x {len(_times)} timesteps "
          f"= {len(sTs)} images, {len(_times)} decodes", flush=True)

    _decoded = {"time": None}

    def model_callback(model, render_kwargs, frame_id):
        # Upstream advances the decode stream on every image. Here it advances
        # on every *timestep*, so the views of one instant share one volume --
        # which is the whole point, and is also why the stream never has to be
        # rewound to something it cannot be rewound to.
        if _decoded["time"] == frame_id:
            return model, render_kwargs
        _decoded["time"] = frame_id
        if frame_id != args.render_start_frame:
            model_receive = next(mmap_iter)
            model.density = torch.nn.Parameter(model_receive[:, :1])
            model.k0.k0 = torch.nn.Parameter(model_receive[:, 1:])

            density = F.max_pool3d(model.density, kernel_size=3, padding=1, stride=1)
            alpha = 1 - torch.exp(
                -F.softplus(density + model_kwargs['act_shift']) * model_kwargs['voxel_size_ratio'])
            mask = (alpha >= model.mask_cache_thres).squeeze(0).squeeze(0)
            xyz_min = torch.Tensor(model_kwargs['xyz_min'])
            xyz_max = torch.Tensor(model_kwargs['xyz_max'])

            model.mask_cache.mask = mask
            xyz_len = xyz_max - xyz_min
            model.mask_cache.xyz2ijk_scale = (torch.Tensor(list(mask.shape)) - 1) / xyz_len
            model.mask_cache.xyz2ijk_shift = -xyz_min * model.mask_cache.xyz2ijk_scale

        return model, render_kwargs

    render_viewpoints_kwargs['model_callback'] = model_callback
'''

OUTPUT_INJECTION = '    testsavedir = os.environ["OPEN4D_RERF_OUT"]\n'


def main(argv: list[str]) -> int:
    plan_path = Path(os.environ["OPEN4D_RERF_PLAN"])
    plan = json.loads(plan_path.read_text())

    sys.path.insert(0, str(Path(plan["nevo_tree"]).resolve()))
    from nevo import rerf_env  # noqa: E402  -- only importable once the tree is on the path

    root = rerf_env.activate()
    script = root / "rerf_render.py"
    source = script.read_text()
    for anchor, replacement in (
        (CALLBACK_ANCHOR, INJECTION),
        (OUTPUT_ANCHOR, OUTPUT_INJECTION),
    ):
        if anchor not in source:
            raise SystemExit(
                f"{script} no longer contains the line this patch anchors on:\n  "
                f"{anchor.strip()}\nThe vendored ReRF changed; re-check "
                "gs_tools/methods/_rerf_rig_render.py against it."
            )
        source = source.replace(anchor, replacement, 1)

    with rerf_env.rerf_cwd():
        sys.argv = [str(script), *argv]
        exec(compile(source, str(script), "exec"), {"__name__": "__main__", "__file__": str(script)})
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
