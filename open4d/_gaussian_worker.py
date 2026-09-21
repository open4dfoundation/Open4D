"""Run Vega in its own Python environment."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np


def _read_manifest(source: Path, expected_count: int | None = None) -> list[dict]:
    manifest = json.loads((source / "manifest.json").read_text(encoding="utf-8"))
    entries = manifest.get("frames") if isinstance(manifest, dict) else None
    if not isinstance(entries, list) or len(entries) < 2:
        raise ValueError("Vega's native bitstream must contain at least two frames")
    if expected_count is not None and len(entries) != expected_count:
        raise ValueError("Vega's output frame count does not match the input")
    groups = set()
    for index, entry in enumerate(entries):
        if not isinstance(entry, dict) or type(entry.get("frame_idx")) is not int or entry["frame_idx"] != index:
            raise ValueError("Vega frames must be contiguous and start at zero")
        group = entry.get("group_id")
        if type(group) is not int or group < 0:
            raise ValueError("Vega group IDs must be nonnegative integers")
        groups.add(group)
        if entry.get("frame_type") != ("key" if index == 0 else "residual"):
            raise ValueError("Vega's single-model bitstream must start with one key frame")
        name = f"frame_{index:04d}.pt"
        if entry.get("file") != name:
            raise ValueError("Vega's frame filename does not match its index")
        path = source / name
        if not path.is_file() or not path.stat().st_size:
            raise FileNotFoundError(f"Missing Vega frame chunk: {path}")
    if len(groups) != 1:
        raise ValueError("Vega's native writer cannot preserve multiple colour models")
    model = source / "color_model.pt"
    if not model.is_file() or not model.stat().st_size:
        raise FileNotFoundError(f"Missing Vega colour model: {model}")
    return entries


def _reconstruct_frames(player, chunks):
    key = None
    for chunk in chunks:
        if key is None:
            key = chunk
        else:
            # Static objects refer to the key, not the most recent dynamic frame.
            player.reconstruct(key)
        yield chunk, player.reconstruct(chunk)


def _encode(request: dict) -> None:
    import torch
    from vega.bitstream import write_bitstream
    from vega.encoder import VegaEncoderConfig, encode_sequence
    from vega.gaussians import GaussianSet

    frames = []
    for path in sorted(Path(request["source"]).glob("frame_*.npz")):
        with np.load(path, allow_pickle=False) as data:
            tensors = {name: torch.from_numpy(data[name]).cuda() for name in data.files}
        sh = tensors["spherical_harmonics"]
        frames.append(GaussianSet(
            xyz=tensors["positions"], scale_raw=tensors["scales"].log(),
            rot_raw=tensors["rotations"], opacity_raw=torch.logit(tensors["opacities"][:, None]),
            sh_dc=sh[:, :1], sh_rest=sh[:, 1:],
            object_id=torch.zeros(len(sh), dtype=torch.long, device="cuda"),
            sh_degree=int(np.sqrt(sh.shape[1])) - 1,
        ))
    positions = torch.cat([frame.xyz for frame in frames])
    bounds_min, bounds_max = positions.amin(dim=0), positions.amax(dim=0)
    config = VegaEncoderConfig(key_iters=request["key_iterations"],
                               residual_iters=request["residual_iterations"])
    result = encode_sequence(frames, bounds_min, bounds_max, config=config)
    if len(set(result.group_ids)) != 1:
        raise RuntimeError("Vega's native writer cannot preserve multiple colour models; encode a shorter sequence")
    if len(result.chunks) != len(frames):
        raise RuntimeError("Vega did not encode every input frame")
    states = [result.color_model.state_dict(),
              *(chunk.tiny_hash_state for chunk in result.chunks if chunk.tiny_hash_state)]
    if any(not torch.isfinite(value).all() for state in states for value in state.values()):
        raise RuntimeError("Vega produced nonfinite colour parameters")
    write_bitstream(request["output"], result.color_model, result.chunks)


def _decode(request: dict) -> None:
    import torch
    from vega.color_encoding import ColorEncodingConfig, HierarchicalColorModel
    from vega.player import StreamingPlayer

    source, output = Path(request["source"]), Path(request["output"])
    entries = _read_manifest(source)
    with torch.serialization.safe_globals([ColorEncodingConfig]):
        state = torch.load(source / "color_model.pt", weights_only=True, map_location="cuda")
    model = HierarchicalColorModel(state["config"]).cuda()
    model.load_state_dict(state["state_dict"])
    model._key_trained = True
    player = StreamingPlayer(model)
    def chunks():
        for entry in entries:
            index = entry["frame_idx"]
            chunk = torch.load(source / f"frame_{index:04d}.pt", weights_only=True, map_location="cpu")
            if chunk["frame_idx"] != index or chunk["group_id"] != entry["group_id"]:
                raise ValueError("Vega frame chunk does not match its manifest")
            if chunk["frame_type"] != ("key" if index == 0 else "residual"):
                raise ValueError("Vega's single-model bitstream must start with one key frame")
            yield chunk

    with torch.no_grad():
        for chunk, frame in _reconstruct_frames(player, chunks()):
            index = chunk["frame_idx"]
            if request["operation"] == "decode":
                np.savez(output / f"frame_{index:06d}.npz",
                         positions=frame.get_xyz.cpu().numpy(), scales=frame.get_scaling.cpu().numpy(),
                         rotations=frame.get_rotation.cpu().numpy(),
                         opacities=frame.get_opacity.cpu().numpy().reshape(-1))
            elif index == request["frame"]:
                directions = torch.from_numpy(np.load(output / "directions.npy", allow_pickle=False)).cuda()
                if len(directions) != len(frame):
                    raise ValueError("Expected one direction per decoded Gaussian")
                colors = (model.forward_key(frame.get_xyz, directions) if index == 0 else
                          model.forward_residual(frame.get_xyz, directions, index))
                np.save(output / "colors.npy", colors.cpu().numpy())
                return
    if request["operation"] == "colors":
        raise IndexError(request["frame"])


def main() -> None:
    runtime, request_path = map(Path, sys.argv[1:])
    sys.path.insert(0, str(runtime))
    request = json.loads(request_path.read_text())
    if request["operation"] == "encode":
        _encode(request)
    elif request["operation"] in ("decode", "colors"):
        _decode(request)
    else:
        raise ValueError(f"Unknown operation: {request['operation']}")


if __name__ == "__main__":
    main()
