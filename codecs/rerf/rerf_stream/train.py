"""Train upstream ReRF on prepared calibrated frames in its separate runtime.

Run prepare_rerf_orbit.py with Open4D's Python first. This module runs under
the entropy coder's Python 3.8, records its actual configuration and retains
the upstream model and residual training stages.
"""

import argparse
import hashlib
import json
from pathlib import Path
import runpy
import sys
import time

from . import env


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--iterations", type=int, default=1000)
    parser.add_argument("--residual-iterations", type=int, default=500)
    parser.add_argument("--grid-size", type=int, default=40)
    parser.add_argument("--batch-size", type=int, default=2048)
    parser.add_argument("--seed", type=int, default=777)
    parser.add_argument("--compress", action="store_true")
    parser.add_argument("--quality", type=int, default=90)
    args = parser.parse_args(argv)
    if min(args.iterations, args.residual_iterations, args.grid_size, args.batch_size) < 1:
        parser.error("training counts must be positive")
    capture = args.capture.resolve()
    meta = json.loads((capture / "capture.json").read_text())
    target = args.output.resolve()
    target.mkdir(parents=True, exist_ok=False)
    build = env.preflight()
    env.activate()
    import mmcv
    cfg = mmcv.Config.fromfile(str(env.UPSTREAM / "configs/default.py"))
    # The released upstream video forward supports one frame's rays at a time.
    # Its published individual recipe still learns each P-frame from the prior
    # checkpoint, while sequential replay doubles the output batch incorrectly.
    cfg.update(expname="model", basedir=str(target), train_mode="individual",
               frame_num=meta["frames"], fix_rgbnet=True, use_res=False,
               deform_res_mode="separate", deform_from_start=True, voxel_size=8)
    cfg.data.update(datadir=str(capture), dataset_type="NHR", inverse_y=True, white_bkgd=False)
    cfg.coarse_train.N_iters = 0
    cfg.fine_train.update(N_iters=args.iterations, N_iters_pretrained=args.residual_iterations,
                         N_rand=args.batch_size, pg_scale=[], pg_scale_pretrained=[], ray_sampler="flatten",
                         lrate_deformation_field=1e-4,
                         weight_tv_density=0., weight_tv_k0=0., weight_tv_deform=0.)
    cfg.fine_model_and_render.update(num_voxels=args.grid_size ** 3, num_voxels_base=args.grid_size ** 3,
                                    rgbnet_dim=12, rgbnet_width=64, alpha_init=.01,
                                    fast_color_thres=1e-4, world_bound_scale=1.)
    config_path = target / "train-config.py"
    cfg.dump(str(config_path))
    started = time.monotonic()
    with env.upstream_cwd():
        from lib.load_NHR import NHR_Dataset
        original = NHR_Dataset.__init__
        def calibrated_size(self, path, num_frame, tar_size=None, cam_num=-1):
            # Match the prepared aspect ratio; the upstream 16:9 default crops
            # 4:3 ORBIT views and changes their calibration.
            original(self, path, num_frame, tar_size=(meta["height"], meta["width"]), cam_num=cam_num)
        NHR_Dataset.__init__ = calibrated_size
        saved = sys.argv
        try:
            sys.argv = ["run.py", "--config", str(config_path), "--seed", str(args.seed), "--i_print", "100"]
            runpy.run_path(str(env.UPSTREAM / "run.py"), run_name="__main__")
        finally:
            sys.argv = saved
            NHR_Dataset.__init__ = original
        model = target / "model"
        required = [model / f"fine_last_{frame}.tar" for frame in range(meta["frames"])]
        required.append(model / "rgb_net.tar")
        if any(not path.is_file() for path in required):
            raise RuntimeError("upstream training did not produce all learned checkpoints")
        provenance = {"capture": meta, "seed": args.seed, "iterations": args.iterations,
                      "residual_iterations": args.residual_iterations, "runtime": build.describe(),
                      "training_seconds": time.monotonic() - started,
                      "checkpoints": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in required}}
        (target / "training-provenance.json").write_text(json.dumps(provenance, indent=2))
        if args.compress:
            try:
                sys.argv = ["compress.py", "--model_path", str(model), "--expr_name", "compressed",
                            "--quality", str(args.quality), "--frame_num", str(meta["frames"]),
                            "--group_size", str(meta["frames"]), "--pca", "--pca_chs", "7,13"]
                runpy.run_path(str(env.UPSTREAM / "codec/compress.py"), run_name="__main__")
            finally:
                sys.argv = saved
            import numpy as np
            bounds = json.loads((capture / "bbox.json").read_text())
            lo, hi = np.asarray(bounds["xyz_min"]), np.asarray(bounds["xyz_max"])
            center, radius = (lo + hi) / 2, np.linalg.norm(hi - lo) / 2
            distances = [np.linalg.norm(np.asarray(c["c2w"])[:3, 3] - center) for c in meta["cameras"]]
            decoder = {"voxel_size": 8, "fine_model_and_render": dict(cfg.fine_model_and_render),
                       "data": {"datadir": "", "white_bkgd": False, "inverse_y": True, "flip_x": False, "flip_y": False},
                       "render": {"near": max(.01, min(distances) - 2 * radius), "far": max(distances) + 2 * radius},
                       "native": {"group_size": meta["frames"], "pca": True, "pca_channels": [7, 13]}}
            (model / "compressed/decoder_config.json").write_text(json.dumps(decoder, indent=2))
    print(target, flush=True)


if __name__ == "__main__":
    main()
