"""Decode a self-contained QNDF-INT8 TorchScript artifact."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys

import torch

if __package__:
    from .compress_int8 import MeshDataset, PE
else:
    sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
    from open4d.codecs.qndf_int8.compress_int8 import MeshDataset, PE
from open4d.torch_ops import save_obj


def decode(
    model_path: Path,
    context_path: Path,
    destination: Path,
    *,
    normalized: bool = False,
) -> Path:
    model = torch.jit.load(str(model_path), map_location="cpu").eval()
    context = torch.load(context_path, map_location="cpu", weights_only=True)
    if context.get("schema") != "open4d.qndf-int8/v1":
        raise ValueError(f"unsupported QNDF-INT8 context: {context.get('schema')!r}")
    coarse = context["coarse_vertices"]
    inputs = coarse * context["input_scale"]
    inputs = (
        inputs - context.get("input_mean", inputs.mean(0, keepdim=True))
    ) / context.get("input_std", inputs.std(0, keepdim=True))
    encoded = PE(context["pe_dim"])(inputs)
    neighborhoods = MeshDataset(
        encoded, inputs, context["coarse_faces"], torch.zeros_like(coarse)
    )
    with torch.inference_mode():
        vertices = coarse + model(
            encoded, neighborhoods.neighbors, neighborhoods.edge_wts
        ) / context["output_scale"]
    if not normalized:
        vertices = vertices * context["original_scale"] + context["original_min"]
    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    save_obj(str(destination), vertices, context["coarse_faces"])
    return destination


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("model", type=Path)
    parser.add_argument("context", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--normalized", action="store_true")
    args = parser.parse_args()
    decode(args.model, args.context, args.destination, normalized=args.normalized)


if __name__ == "__main__":
    main()
