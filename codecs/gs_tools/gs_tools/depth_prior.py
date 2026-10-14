"""Cache QUEEN's MiDaS depth priors, run inside the MiDaS environment.

`gs-tools depth-prior` runs this file with the interpreter that has MiDaS's pins
(timm==0.6.13, see requirements-midas.txt), so the training environment never
needs them. For the first image of every ``camNN`` folder it writes
``<scene>/depth_priors/camNN.npy``: the inverse-depth map QUEEN's train.py would
otherwise compute inline, with the same model, transform and interpolation.

Usage: python depth_prior.py <queen tree> <scene> <weights>
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np


def main() -> int:
    queen, scene, weights = (Path(argument).resolve() for argument in sys.argv[1:4])
    # model_loader imports MiDaS's own `midas` package by its top-level name.
    sys.path[:0] = [str(queen / "MiDaS"), str(queen)]
    import torch
    from PIL import Image
    from midas.model_loader import load_model

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    model, transform, _, _ = load_model(device, str(weights), "dpt_beit_large_512",
                                        optimize=False, height=None, square=False)
    cameras = sorted(path for path in scene.glob("cam[0-9]*") if (path / "images").is_dir())
    if not cameras:
        print(f"no camNN/images folders in {scene}", file=sys.stderr)
        return 1
    output = scene / "depth_priors"
    output.mkdir(exist_ok=True)
    for camera in cameras:
        images = sorted((camera / "images").glob("*.png"))
        if not images:
            print(f"{camera} has no images", file=sys.stderr)
            return 1
        rgb = np.asarray(Image.open(images[0]).convert("RGB"), dtype=np.float32) / 255.0
        sample = torch.from_numpy(transform({"image": rgb})["image"]).to(device).unsqueeze(0)
        with torch.no_grad():
            # MiDaS/run.py's process(), without its webcam and OpenVINO imports.
            prediction = torch.nn.functional.interpolate(
                model.forward(sample).unsqueeze(1), size=rgb.shape[:2],
                mode="bicubic", align_corners=False,
            ).squeeze().cpu().numpy()
        if not np.isfinite(prediction).all():
            print(f"MiDaS returned non-finite depth for {images[0]}", file=sys.stderr)
            return 1
        np.save(output / f"{camera.name}.npy", prediction.astype(np.float32))
        print(f"  {camera.name}  {images[0].name}  {prediction.shape[1]}x{prediction.shape[0]}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
