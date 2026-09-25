"""Closed payload layouts for native research representations.

This module inspects JSON only. It never imports or deserializes a native model.
All paths are generated here, rather than taken from an archive's filenames.
"""
from __future__ import annotations

from ._protocol import CodecError

PROFILES = {
    "tvmc": ("triangle_mesh", "shared-reference"),
    "tsmc": ("triangle_mesh", "whole-group"),
    "vega": ("neural_gaussians", "shared-reference"),
    "queen": ("gaussian_splats", "previous-frame"),
    "3dgstream": ("gaussian_splats", "previous-frame"),
    "rerf": ("neural_field", "previous-frame"),
    "vdmc": ("triangle_mesh", "native-sequence"),
    "faster_vdmc": ("triangle_mesh", "native-sequence"),
    # Explicit scope exception: a shared spatial model, with no temporal
    # prediction or residual dependency between the quantized frame latents.
    "n4mc": ("neural_tsdf", "shared-model-independent-frames"),
}
NEURAL_CODECS = frozenset(("vega", "queen", "3dgstream", "rerf"))


def _require(condition, message):
    if not condition:
        raise CodecError(f"invalid native profile: {message}")


def layout(codec, count, native=None):
    """Validate a profile and return its exact ordered filename/role pairs."""
    _require(codec in PROFILES, f"unsupported codec {codec!r}")
    _require(type(count) is int and count > 0, "invalid frame count")
    files = [("metadata.json", "sequence-metadata")]
    if codec in ("tvmc", "tsmc"):
        _require(native is None, "unexpected tracked-mesh profile")
        files.append(("reference.drc", "reference-mesh"))
        if codec == "tvmc":
            files += [(f"displacement_{i:06d}.{suffix}", role) for i in range(count)
                      for suffix, role in (("drc", "frame-displacement"), ("npy", "vertex-order"))]
        else:
            files += [("B_matrix.txt", "trajectory-basis"), ("T_matrix.txt", "trajectory-offset"),
                      ("delta_trajectories_encoded.npy", "entropy-coded-trajectories"),
                      ("entropy_model.npz", "entropy-model")]
        return files
    _require(isinstance(native, dict) and native.get("profile") == f"{codec}/1", "unsupported profile version")
    if codec in NEURAL_CODECS or codec == "n4mc":
        _require(count >= 2, "native sequence profiles require at least two frames")
    if codec == "n4mc":
        _require(set(native) == {"profile"}, "unexpected N4MC configuration")
        files.append(("checkpoint.pt", "shared-neural-model"))
        files += [(f"frame_{i:06d}.npz", "quantized-tsdf-latent") for i in range(count)]
    elif codec == "vega":
        _require(set(native) == {"profile"}, "unexpected Vega configuration")
        files += [("manifest.json", "native-frame-index"), ("color_model.pt", "shared-neural-appearance")]
        files += [(f"frame_{i:04d}.pt", "key-state" if i == 0 else "residual-state") for i in range(count)]
    elif codec == "queen":
        _require(set(native) == {"profile", "sh_degree", "gate_params"}, "unexpected QUEEN configuration")
        _require(type(native["sh_degree"]) is int and 0 <= native["sh_degree"] <= 3, "invalid SH degree")
        gates = native["gate_params"]
        _require(isinstance(gates, list) and len(gates) == 7 and all(g in ("on", "none") for g in gates), "invalid QUEEN gates")
        _require(all(g == "none" for g in gates[1:]), "only QUEEN position gating is supported by its native serializer")
        files.append(("initial.ply", "initial-gaussians"))
        files += [(f"frame_{i:06d}.pkl", "compressed-attribute-residual") for i in range(1, count)]
    elif codec == "3dgstream":
        _require(set(native) == {"profile", "sh_degree", "rotate_sh", "only_mlp", "added"}, "unexpected 3DGStream configuration")
        _require(type(native["sh_degree"]) is int and 0 <= native["sh_degree"] <= 3, "invalid SH degree")
        _require(type(native["rotate_sh"]) is bool and type(native["only_mlp"]) is bool, "invalid NTC flags")
        _require(not native["rotate_sh"] or native["sh_degree"] == 1, "the native SH rotation requires degree 1")
        added = native["added"]
        _require(isinstance(added, list) and len(added) == count and all(type(v) is bool for v in added) and not added[0], "invalid added-Gaussian index")
        files += [("initial.ply", "initial-gaussians"), ("ntc_config.json", "neural-transform-architecture")]
        for i in range(1, count):
            files.append((f"ntc_{i:06d}.pth", "neural-transform"))
            if added[i]:
                files.append((f"added_{i:06d}.ply", "added-gaussians"))
    elif codec == "rerf":
        _require(set(native) == {"profile", "group_size", "pca", "pca_channels", "frames"}, "unexpected ReRF configuration")
        group, pca, frames = native["group_size"], native["pca"], native["frames"]
        _require(type(group) is int and group > 0 and type(pca) is bool, "invalid ReRF group/PCA mode")
        channels = native["pca_channels"]
        _require(isinstance(channels, list) and len(channels) == 2 and all(type(c) is int and c > 0 for c in channels), "invalid PCA channels")
        _require(isinstance(frames, list) and len(frames) == count, "invalid ReRF frame index")
        files += [("decoder_config.json", "field-configuration"), ("model_kwargs.json", "field-model"), ("rgb_net.tar", "shared-rgb-network")]
        previous = None
        for frame in frames:
            _require(isinstance(frame, dict) and set(frame) == {"id", "quality", "motion", "channels"}, "invalid ReRF frame record")
            index, quality, motion = frame["id"], frame["quality"], frame["motion"]
            _require(type(index) is int and index >= 0 and (previous is None or index == previous + 1), "ReRF frames must be contiguous")
            _require(previous is not None or index % group == 0, "ReRF sequence must start at a group key")
            _require(type(quality) is int and 0 <= quality <= 100 and type(motion) is bool, "invalid ReRF quality/motion")
            key = index % group == 0
            _require(motion != key, "ReRF residual frames require both motion payloads")
            halves = frame["channels"]
            _require(isinstance(halves, list) and len(halves) == (2 if not key and pca else 1)
                     and all(type(n) is int and 0 < n <= 4096 for n in halves), "invalid ReRF entropy channels")
            files += [(f"header_{index}.json", "field-frame-header"), (f"mask_{index}.rerf", "occupancy-mask")]
            # The native ac_dc_encode2 writes one file per channel; .rerf is
            # a prefix, not an aggregate entropy file.
            for half, channels_in_half in enumerate(halves):
                files += [(f"feature_{index}_{quality-half}.rerf{channel}", "key-features" if key else "residual-features")
                          for channel in range(channels_in_half)]
            if not key and pca:
                _require(quality > 0, "PCA residual quality must exceed zero")
                files.append((f"pca_m_{index}.npy", "pca-basis"))
            if motion:
                files += [(f"deform_{index}.npy", "motion-field"), (f"deform_mask_{index}.rerf", "motion-mask")]
            previous = index
    else:
        _require(set(native) == {"profile", "decoder_config"} and type(native["decoder_config"]) is bool, "invalid V-DMC configuration")
        files.append(("sequence.vmesh", "native-v3c-sequence"))
        if native["decoder_config"]:
            files.append(("decoder.cfg", "native-decoder-configuration"))
    return files
