"""Train QNDF, dynamically quantize it to INT8, and verify a saved decoder.

This is an isolated experimental variant. It does not modify the upstream QNDF
implementation and it deliberately saves both FP32 and INT8 reconstructions.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import random
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F
import torch.optim as optim
from pytorch3d.io import load_obj, save_obj
from torch.utils.data import DataLoader, Dataset
from tqdm import tqdm

from mesh_errors import normal_error, point2mesh_error


class PE(nn.Module):
    def __init__(self, pe_dim: int):
        super().__init__()
        self.pe_dim = pe_dim

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        values = []
        for j in range(self.pe_dim // 2):
            values.append(torch.sin((2**j) * x * torch.pi))
            values.append(torch.cos((2**j) * x * torch.pi))
        return torch.cat(values, dim=1)


class MLP(nn.Module):
    def __init__(self, input_dim: int, hidden_dim: int, output_dim: int, num_layers: int):
        super().__init__()
        self.num_layers = num_layers
        self.layers = nn.ModuleList(
            [
                nn.Sequential(
                    nn.Linear(input_dim if i == 0 else hidden_dim, hidden_dim),
                    nn.SiLU(),
                    nn.LayerNorm(hidden_dim),
                )
                for i in range(num_layers)
            ]
        )
        self.neighbor_layers = nn.ModuleList(
            [
                nn.Sequential(
                    nn.Linear(input_dim if i == 0 else hidden_dim, hidden_dim),
                    nn.SiLU(),
                    nn.LayerNorm(hidden_dim),
                )
                for i in range(3)
            ]
        )
        self.output_layer = nn.Linear(hidden_dim, output_dim)

    def forward(self, x: torch.Tensor, xn: torch.Tensor, ln: torch.Tensor) -> torch.Tensor:
        out = x
        for i in range(self.num_layers):
            residual = out
            out = self.layers[i](out)
            if i < 3:
                xn = self.neighbor_layers[i](xn)
                x_acc = (xn * ln[:, :, None]).sum(dim=1)
                out = (out + x_acc) * 0.5
            if i > 0:
                out = out + residual
        return self.output_layer(out)


class MeshDataset(Dataset):
    def __init__(self, pe_verts: torch.Tensor, verts: torch.Tensor, faces: torch.Tensor, targets: torch.Tensor):
        self.pv = pe_verts
        self.v = verts
        self.f = faces
        self.gv = targets
        self.neighbors, self.edge_wts = self._preprocess()

    def _preprocess(self) -> tuple[torch.Tensor, torch.Tensor]:
        neighbors = []
        edge_wts = []
        for idx in tqdm(range(self.pv.size(0)), desc="Building neighborhoods"):
            face_numbers, _ = torch.nonzero(self.f == idx, as_tuple=True)
            neighbor_vertices = torch.unique(self.f[face_numbers, :])
            neighbor_vertices = neighbor_vertices[neighbor_vertices != idx]
            neighbor_pe = self.pv[neighbor_vertices]
            edges = self.v[neighbor_vertices] - self.v[idx]
            weights = F.softmax((edges**2).sum(dim=1).sqrt(), dim=0)
            neighbor_pad = torch.zeros((14, neighbor_pe.shape[1]), device=self.pv.device)
            weight_pad = torch.zeros(14, device=self.pv.device)
            count = min(14, neighbor_pe.shape[0])
            neighbor_pad[:count] = neighbor_pe[:count]
            weight_pad[:count] = weights[:count]
            neighbors.append(neighbor_pad)
            edge_wts.append(weight_pad)
        return torch.stack(neighbors), torch.stack(edge_wts)

    def __len__(self) -> int:
        return self.pv.shape[0]

    def __getitem__(self, idx: int):
        return self.pv[idx], self.neighbors[idx], self.edge_wts[idx], self.gv[idx]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("mesh_name")
    parser.add_argument("--num_subdiv", "-ns", type=int, default=2)
    parser.add_argument("--coarse_size", "-cs", type=int, default=3000)
    parser.add_argument("--pe_dim", "-pe", type=int, default=20)
    parser.add_argument("--hidden_dim", "-hd", type=int, default=28)
    parser.add_argument("--num_layers", "-nl", type=int, default=17)
    parser.add_argument("--input_scale", "-is", type=float, default=1000)
    parser.add_argument("--output_scale", "-os", type=float, default=1414)
    parser.add_argument("--learning_rate", "-lr", type=float, default=0.001)
    parser.add_argument("--num_epochs", "-ne", type=int, default=300)
    parser.add_argument("--batch_size", "-bs", type=int, default=2048)
    parser.add_argument("--seed", type=int, default=20260715)
    parser.add_argument("--output_dir", type=Path, default=Path("outputs"))
    return parser.parse_args()


def set_seed(seed: int) -> None:
    random.seed(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)
    torch.cuda.manual_seed_all(seed)


def evaluate(tv, faces, original_vertices, original_faces) -> dict[str, float]:
    r2t, _, _ = point2mesh_error(tv, faces, original_vertices, original_faces)
    t2r, _, _ = point2mesh_error(original_vertices, original_faces, tv, faces)
    normals = normal_error(tv, faces, original_vertices, original_faces)
    return {
        "reconstruction_to_target": float(r2t),
        "target_to_reconstruction": float(t2r),
        "total_error": float(r2t + t2r),
        "normal_error_degrees": float(normals),
    }


def main() -> None:
    args = parse_args()
    set_seed(args.seed)
    if not torch.cuda.is_available():
        raise RuntimeError("CUDA is required for QNDF training and mesh evaluation")
    device = torch.device("cuda:0")

    low_path = Path("experiments") / args.mesh_name / f"input_f{args.coarse_size}_s{args.num_subdiv}.obj"
    target_path = Path("experiments") / args.mesh_name / f"output_f{args.coarse_size}_s{args.num_subdiv}.obj"
    original_path = Path("objs_original") / f"{args.mesh_name}.obj"
    for path in (low_path, target_path, original_path):
        if not path.is_file() or path.stat().st_size == 0:
            raise FileNotFoundError(f"Required non-empty mesh is missing: {path}")

    low, low_faces_aux, _ = load_obj(str(low_path), load_textures=False, device=device)
    low_faces = low_faces_aux.verts_idx
    target, _, _ = load_obj(str(target_path), load_textures=False, device=device)
    original, original_faces_aux, _ = load_obj(str(original_path), load_textures=False, device=device)
    original_faces = original_faces_aux.verts_idx

    original_min = original.min(dim=0).values
    normalized_original = original - original_min
    original_scale = normalized_original.max()
    normalized_original = normalized_original / original_scale

    inputs = low * args.input_scale
    inputs = (inputs - inputs.mean(dim=0, keepdim=True)) / inputs.std(dim=0, keepdim=True)
    pe_inputs = PE(args.pe_dim)(inputs)
    dataset = MeshDataset(pe_inputs, inputs, low_faces, (target - low) * args.output_scale)
    loader = DataLoader(dataset, batch_size=args.batch_size, shuffle=False)

    model = MLP(3 * args.pe_dim, args.hidden_dim, 3, args.num_layers).to(device)
    optimizer = optim.AdamW(model.parameters(), lr=args.learning_rate)
    scheduler = optim.lr_scheduler.CosineAnnealingLR(optimizer, args.num_epochs)
    criterion = nn.MSELoss()

    model.train()
    final_loss = math.nan
    for _ in tqdm(range(args.num_epochs), desc="Training"):
        total_loss = 0.0
        for batch_inputs, neighbors, weights, targets in loader:
            optimizer.zero_grad()
            predictions = model(batch_inputs, neighbors, weights)
            loss = criterion(predictions, targets)
            loss.backward()
            optimizer.step()
            total_loss += loss.item() * batch_inputs.size(0)
        final_loss = total_loss / len(dataset)
        scheduler.step()

    run_dir = args.output_dir / (
        f"{args.mesh_name}_ssp_cs{args.coarse_size}_ns{args.num_subdiv}_{args.num_epochs}epochs_int8"
    )
    run_dir.mkdir(parents=True, exist_ok=True)

    model.eval()
    with torch.no_grad():
        fp32_vertices = low + model(pe_inputs, dataset.neighbors, dataset.edge_wts) / args.output_scale
    fp32_metrics = evaluate(fp32_vertices, low_faces, normalized_original, original_faces)
    save_obj(str(run_dir / "reconstruction_fp32_normalized.obj"), fp32_vertices, low_faces)
    save_obj(
        str(run_dir / "reconstruction_fp32_original_scale.obj"),
        fp32_vertices * original_scale + original_min,
        low_faces,
    )
    torch.save(model.state_dict(), run_dir / "model_fp32_state_dict.pt")

    cpu_model = model.cpu()
    cpu_inputs = pe_inputs.cpu()
    cpu_neighbors = dataset.neighbors.cpu()
    cpu_weights = dataset.edge_wts.cpu()
    cpu_low = low.cpu()
    example = (cpu_inputs[:256], cpu_neighbors[:256], cpu_weights[:256])

    with torch.inference_mode():
        fp32_cpu_prediction = cpu_model(cpu_inputs, cpu_neighbors, cpu_weights)
    fp32_traced = torch.jit.trace(cpu_model, example, strict=False)
    fp32_model_path = run_dir / "model_fp32_torchscript.pt"
    torch.jit.save(fp32_traced, str(fp32_model_path))
    fp32_reloaded = torch.jit.load(str(fp32_model_path), map_location="cpu").eval()
    with torch.inference_mode():
        fp32_reloaded_prediction = fp32_reloaded(cpu_inputs, cpu_neighbors, cpu_weights)
    fp32_reload_max_abs_difference = float((fp32_cpu_prediction - fp32_reloaded_prediction).abs().max())
    if fp32_reload_max_abs_difference > 1e-7:
        raise RuntimeError(f"Reloaded FP32 decoder changed output by {fp32_reload_max_abs_difference}")

    quantized_model = torch.ao.quantization.quantize_dynamic(
        cpu_model,
        {nn.Linear},
        dtype=torch.qint8,
        inplace=False,
    ).eval()
    with torch.inference_mode():
        int8_vertices = cpu_low + quantized_model(cpu_inputs, cpu_neighbors, cpu_weights) / args.output_scale

    traced = torch.jit.trace(quantized_model, example, strict=False)
    int8_model_path = run_dir / "model_int8_torchscript.pt"
    torch.jit.save(traced, str(int8_model_path))
    reloaded = torch.jit.load(str(int8_model_path), map_location="cpu").eval()
    with torch.inference_mode():
        reloaded_vertices = cpu_low + reloaded(cpu_inputs, cpu_neighbors, cpu_weights) / args.output_scale
    reload_max_abs_difference = float((int8_vertices - reloaded_vertices).abs().max())
    if reload_max_abs_difference > 1e-7:
        raise RuntimeError(f"Reloaded INT8 decoder changed output by {reload_max_abs_difference}")

    int8_vertices_cuda = reloaded_vertices.cuda()
    int8_metrics = evaluate(int8_vertices_cuda, low_faces, normalized_original, original_faces)
    save_obj(str(run_dir / "reconstruction_int8_normalized.obj"), int8_vertices_cuda, low_faces)
    save_obj(
        str(run_dir / "reconstruction_int8_original_scale.obj"),
        int8_vertices_cuda * original_scale + original_min,
        low_faces,
    )

    fp32_size = (run_dir / "model_fp32_state_dict.pt").stat().st_size
    fp32_torchscript_size = fp32_model_path.stat().st_size
    int8_size = int8_model_path.stat().st_size
    coarse_obj_size = low_path.stat().st_size
    report = {
        "variant": "QNDF-SSP-INT8 experimental",
        "mesh": args.mesh_name,
        "settings": vars(args) | {"output_dir": str(args.output_dir)},
        "final_training_mse": final_loss,
        "fp32": fp32_metrics
        | {
            "state_dict_bytes": fp32_size,
            "loadable_torchscript_bytes": fp32_torchscript_size,
            "decoder_reload_max_abs_difference": fp32_reload_max_abs_difference,
        },
        "int8": int8_metrics
        | {
            "loadable_torchscript_bytes": int8_size,
            "decoder_reload_max_abs_difference": reload_max_abs_difference,
        },
        "coarse_mesh": {
            "obj_bytes": coarse_obj_size,
            "theoretical_bytes_from_upstream_formula": math.ceil(
                ((args.coarse_size // 2) * 3 * 32 + args.coarse_size * 3 * math.ceil(math.log2(args.coarse_size // 2))) / 8
            ),
        },
        "notes": [
            "INT8 size is a real loadable TorchScript artifact, not the commented Huffman format.",
            "OBJ coarse-mesh bytes are not a compressed bitstream size.",
            "The upstream theoretical coarse-mesh size omits a concrete encoder/decoder container.",
        ],
    }
    report["int8"]["total_loadable_model_plus_coarse_obj_bytes"] = int8_size + coarse_obj_size
    (run_dir / "metrics.json").write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))
    print(f"Outputs saved to: {run_dir.resolve()}")


if __name__ == "__main__":
    main()
