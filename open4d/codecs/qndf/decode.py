"""Decode a self-contained QNDF checkpoint without encoder-side state."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys

import torch

if __package__:
    from .compress import MLP, MeshDataset, PE
else:
    sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
    from open4d.codecs.qndf.compress import MLP, MeshDataset, PE
from open4d.torch_ops import save_obj


def decode(checkpoint: Path, destination: Path, device: str = "cpu") -> Path:
    payload = torch.load(checkpoint, map_location=device, weights_only=True)
    if payload.get("schema") != "open4d.qndf/v1":
        raise ValueError(f"unsupported QNDF checkpoint: {payload.get('schema')!r}")
    coarse = payload["coarse_vertices"].to(device)
    faces = payload["coarse_faces"].to(device)
    inputs = coarse * payload["input_scale"]
    inputs = (inputs - payload["input_mean"].to(device)) / payload["input_std"].to(device)
    encoded = PE(payload["pe_dim"])(inputs)
    neighborhoods = MeshDataset(encoded, inputs, faces, torch.zeros_like(coarse))
    model = MLP(
        3 * payload["pe_dim"], payload["hidden_dim"], 3, payload["num_layers"]
    ).to(device)
    model.load_state_dict(payload["model_state_dict"])
    model.eval()
    with torch.inference_mode():
        vertices = coarse + model(
            encoded, neighborhoods.neighbors, neighborhoods.edge_wts
        ) / payload["output_scale"]
    normalization = payload.get("normalization")
    if normalization:
        vertices = (
            vertices * float(normalization["scale"])
            + torch.tensor(normalization["bbox_min"], device=device)
        )
    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    save_obj(str(destination), vertices, faces)
    return destination


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checkpoint", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--device", default="cpu")
    args = parser.parse_args()
    decode(args.checkpoint, args.destination, args.device)


if __name__ == "__main__":
    main()
