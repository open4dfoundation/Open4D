"""Native temporal evaluation in isolated, method-specific Python runtimes.

Keep this entry point compatible with ReRF's required Python 3.8. It does not
import the public package (whose base interpreter may be newer).
"""
from __future__ import annotations

from collections import OrderedDict
import json
from pathlib import Path
import sys
from types import SimpleNamespace

import numpy as np


def _save_gaussians(model, path):
    def array(value):
        return value.detach().float().cpu().numpy()
    np.savez(path, positions=array(model.get_xyz), scales=array(model.get_scaling),
             rotations=array(model.get_rotation), opacities=array(model.get_opacity).reshape(-1),
             spherical_harmonics=array(model.get_features))


def _queen(source, output, metadata):
    import torch
    from scene.gaussian_model import GaussianModel
    from scene.decoders import DecoderIdentity

    profile = metadata["native"]
    names = ["xyz", "f_dc", "f_rest", "sc", "rot", "op", "flow"]
    # Initial PLY uses identity decoders. Later payloads carry their complete
    # quantized decoder architecture and weights; no training setup is needed.
    latent = SimpleNamespace(param_names=names, seed=0, gate_params=profile["gate_params"],
                             quant_type=["none"] * len(names))
    arguments = SimpleNamespace(gate_gamma=-0.5, gate_eta=1.01, gate_lr=0.1, gate_temp=0.3)
    model = GaussianModel(profile["sh_degree"], latent, arguments)
    model.latent_decoders = OrderedDict((name, DecoderIdentity()) for name in names)
    model.gate_params = dict.fromkeys(names, False)
    model.load_ply(str(source / "initial.ply"))
    with torch.no_grad():
        for index in range(len(metadata["frames"])):
            model.frame_idx = index + 1
            if index:
                model.load_compressed_pkl(str(source / ("frame_%06d.pkl" % index)))
            _save_gaussians(model, output / ("frame_%06d.npz" % index))
            # This is the native compressed renderer's state update, including
            # decoded attributes and latent state, after each residual frame.
            for name in model.get_atts:
                model.prev_atts[name] = model.get_decoded_atts[name].detach().clone()
                model.prev_latents[name] = model.get_atts[name].detach().clone()


def _gstream(source, output, metadata):
    import torch
    import tinycudann as tcnn
    from scene.gaussian_model import GaussianModel
    from ntc import NeuralTransformationCache

    profile = metadata["native"]
    architecture = json.loads((source / "ntc_config.json").read_text())
    model = GaussianModel(profile["sh_degree"], profile["rotate_sh"])
    model.load_ply(str(source / "initial.ply"), 1.0)
    fields = ("_xyz", "_features_dc", "_features_rest", "_opacity", "_scaling", "_rotation")
    with torch.no_grad():
        _save_gaussians(model, output / "frame_000000.npz")
        for index in range(1, len(metadata["frames"])):
            state = torch.load(source / ("ntc_%06d.pth" % index), weights_only=True, map_location="cuda")
            options = dict(n_input_dims=3, n_output_dims=8, network_config=architecture["network"])
            network = (tcnn.Network(**options) if profile["only_mlp"] else
                       tcnn.NetworkWithInputEncoding(**options, encoding_config=architecture["encoding"]))
            model.ntc = NeuralTransformationCache(network.cuda(), state["xyz_bound_min"], state["xyz_bound_max"])
            model.ntc.load_state_dict(state)
            model.query_ntc()
            model.update_by_ntc()
            recurrent = {name: getattr(model, name) for name in fields}
            if profile["added"][index]:
                added = GaussianModel(profile["sh_degree"])
                added.load_ply(str(source / ("added_%06d.ply" % index)), 1.0)
                for name in fields:
                    setattr(model, name, torch.cat((getattr(model, name), getattr(added, name))).detach())
            _save_gaussians(model, output / ("frame_%06d.npz" % index))
            # Stage-two additions render this frame but are not NTC state.
            for name, value in recurrent.items():
                setattr(model, name, value)


def _rerf(request, source, output, metadata):
    from rerf_stream.bitstream import BitstreamPlayer
    from rerf_stream.cameras import Camera

    config = json.loads((source / "decoder_config.json").read_text())
    profile = metadata["native"]
    player = BitstreamPlayer(config, source, pca=profile["pca"],
                             pca_channels=profile["pca_channels"], group_size=profile["group_size"],
                             render_bounds=config["render"])
    if player.frames != [f["id"] for f in profile["frames"]]:
        raise ValueError("ReRF decoded frame index disagrees with profile")
    camera = None
    if request["operation"] == "render":
        camera = Camera(**dict(request["camera"], c2w=np.asarray(request["camera"]["c2w"])))
    for index, frame in enumerate(player.play(loop=False)):
        if camera is not None:
            np.save(output / ("image_%06d.npy" % index), player.render(camera))
        else:
            np.savez(output / ("frame_%06d.npz" % index),
                     density=frame.model.density.detach().cpu().numpy(),
                     features=frame.model.k0.k0.detach().cpu().numpy(),
                     xyz_min=np.asarray(player.model_kwargs["xyz_min"]),
                     xyz_max=np.asarray(player.model_kwargs["xyz_max"]))


def main():
    request = json.loads(Path(sys.argv[1]).read_text())
    root = Path(request["runtime"])
    if not root.is_dir():
        raise FileNotFoundError("research runtime is missing: " + str(root))
    sys.path.insert(0, str(root))
    source, output = Path(request["source"]), Path(request["output"])
    metadata = json.loads((source / "metadata.json").read_text())
    if request["codec"] != metadata["codec"]:
        raise ValueError("native codec disagrees with worker request")
    if request["operation"] == "decode" and request["codec"] in ("queen", "3dgstream"):
        (_queen if request["codec"] == "queen" else _gstream)(source, output, metadata)
    elif request["operation"] in ("decode", "render") and request["codec"] == "rerf":
        _rerf(request, source, output, metadata)
    else:
        raise ValueError("unsupported native operation")


if __name__ == "__main__":
    main()
