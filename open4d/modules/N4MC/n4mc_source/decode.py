"""Decode N4MC latent grids back into meshes."""
from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
import torch
import trimesh

from config_load import get_config
from fmc import construct_voxel_grid, dynamic_marching_cubes
from network import get_network


def decode(config_path: Path, checkpoint: Path, output: Path, metadata_path: Path | None,
           device: str) -> list[Path]:
    args = get_config().parse_args([
        f"--config_path={config_path}", f"--device={device}"
    ])
    net = get_network(args.model, args).to(device)
    decoder_path = checkpoint / "decoder_compressed.pt"
    if not decoder_path.is_file():
        decoder_path = checkpoint / "decoder.pt"
    if not decoder_path.is_file():
        raise FileNotFoundError(f"decoder weights not found in {checkpoint}")
    state = torch.load(decoder_path, map_location=device, weights_only=True)
    net.decoder.load_state_dict(state)
    net.eval()

    latent_files = sorted((checkpoint / "embed_features").glob("embed_feature_*.npy"))
    if not latent_files:
        raise FileNotFoundError(f"latent features not found in {checkpoint / 'embed_features'}")

    metadata = {}
    if metadata_path and metadata_path.is_file():
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    center = np.asarray(metadata.get("center", [0, 0, 0]), dtype=np.float32)
    scale = float(metadata.get("scale", 1.0))
    names = metadata.get("source_files", [])

    output.mkdir(parents=True, exist_ok=True)
    grid, cubes = construct_voxel_grid(args.voxel_grid_res, device)
    grid = grid * 2
    written = []
    with torch.no_grad():
        for index, latent_path in enumerate(latent_files):
            latent = torch.from_numpy(np.load(latent_path)).float().to(device)
            predicted = net(embed_features=latent)
            sdf = predicted[..., 0].reshape(-1)
            offsets = predicted[..., 1:].reshape(-1, 3)
            vertices, faces = dynamic_marching_cubes(
                grid + offsets * (2 - 1e-8) / (args.voxel_grid_res * 2), cubes, sdf
            )
            vertices_np = vertices.cpu().numpy() * scale + center
            faces_np = faces.cpu().numpy()
            source_name = Path(names[index]).stem if index < len(names) else f"frame_{index:04d}"
            destination = output / f"{source_name}.obj"
            trimesh.Trimesh(vertices=vertices_np, faces=faces_np, process=False).export(destination)
            written.append(destination)
            print(f"[{index + 1}/{len(latent_files)}] {destination}")
    return written


def main() -> int:
    parser = argparse.ArgumentParser(description="Decode N4MC checkpoint into OBJ meshes")
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--checkpoint", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--metadata", type=Path)
    parser.add_argument("--device", default="cuda" if torch.cuda.is_available() else "cpu")
    args = parser.parse_args()
    decode(args.config, args.checkpoint, args.output, args.metadata, args.device)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
