"""In-process adapters for Open4D's neural displacement field codecs."""

from __future__ import annotations

from importlib import import_module
from io import BytesIO
import json
from pathlib import Path
import tempfile
from zipfile import BadZipFile, ZIP_DEFLATED, ZipFile

import numpy as np

from open4d.core import Frame, MemoryFrameProvider, Sequence, TopologyMode, TriangleMesh

from ._npz import _json_value
from ._protocol import CodecError


def _backend():
    try:
        return (
            import_module("torch"),
            import_module("open4d.codecs.qndf.compress"),
            import_module("open4d.codecs.qndf.build_dataset_open3d"),
        )
    except ImportError as error:
        raise CodecError("QNDF dependencies are missing; install open4d[qndf]") from error


def _torch_bytes(torch, value) -> bytes:
    stream = BytesIO()
    torch.save(value, stream)
    return stream.getvalue()


def _quantized_engine(torch, required=None):
    supported = tuple(
        engine for engine in torch.backends.quantized.supported_engines
        if engine != "none"
    )
    selected = required or ("qnnpack" if "qnnpack" in supported else None)
    selected = selected or (supported[0] if supported else None)
    if selected not in supported:
        raise CodecError(
            f"QNDF-int8 needs PyTorch quantized linear support for {selected!r}; "
            f"this runtime provides {supported or 'no quantized engine'}"
        )
    torch.backends.quantized.engine = selected
    return selected


def _manifest(sequence, codec):
    return {
        "schema": f"open4d.{codec}-sequence/v1", "codec": codec,
        "metadata": _json_value(sequence.metadata, "sequence"),
        "frames": [{
            "frame_index": frame.frame_index, "timestamp": frame.timestamp,
            "metadata": _json_value(frame.metadata, f"frame {ordinal}"),
        } for ordinal, frame in enumerate(sequence)],
    }


def _inputs(torch, models, coarse, pe_dim, input_scale):
    inputs = coarse * input_scale
    mean, std = inputs.mean(0, keepdim=True), inputs.std(0, keepdim=True)
    std = std.clamp_min(torch.finfo(inputs.dtype).eps)
    normalized = (inputs - mean) / std
    encoded = models.PE(pe_dim)(normalized)
    return encoded, normalized, mean, std


class QNDFCodec:
    backend = "python-in-process"
    lossless = False
    preserves = ("positions", "triangles")

    def __init__(self, *, int8: bool = False) -> None:
        self.int8 = int8
        self.id = "qndf-int8" if int8 else "qndf"
        self.suffixes = (".qi4d",) if int8 else (".q4d",)
        self.schema = f"open4d.{self.id}-sequence/v1"

    def can_decode(self, source: Path) -> bool:
        try:
            with ZipFile(source) as archive:
                return json.loads(archive.read("manifest.json")).get("schema") == self.schema
        except (OSError, BadZipFile, KeyError, json.JSONDecodeError):
            return False

    def encode(
        self, sequence: Sequence, destination: Path, *, overwrite: bool = False,
        coarse_size: int = 3000, num_subdiv: int = 2, pe_dim: int = 20,
        hidden_dim: int = 56, num_layers: int = 20, epochs: int = 300,
        batch_size: int = 2048, learning_rate: float = 1e-3,
        input_scale: float = 1000, output_scale: float = 1414,
        device: str | None = None, seed: int = 7, verbose: bool = False,
    ) -> Path:
        if not len(sequence):
            raise CodecError("QNDF cannot encode an empty sequence")
        if min(coarse_size, pe_dim, hidden_dim, num_layers, epochs, batch_size) < 1:
            raise ValueError("QNDF size and training options must be positive")
        if pe_dim % 2:
            raise ValueError("pe_dim must be even")
        destination = Path(destination).absolute()
        if destination.exists() and not overwrite:
            raise FileExistsError(f"artifact already exists: {destination}")
        for frame in sequence:
            mesh = frame.geometry
            if any((mesh.colors is not None, mesh.normals is not None,
                    mesh.texture_coordinates is not None, bool(mesh.attributes))):
                raise CodecError(f"{self.id} cannot preserve mesh attributes")
        torch, models, preprocessing = _backend()
        target = torch.device(device or ("cuda" if torch.cuda.is_available() else "cpu"))
        if self.int8 and target.type != "cpu" and not torch.cuda.is_available():
            target = torch.device("cpu")
        torch.manual_seed(seed)
        if torch.cuda.is_available():
            torch.cuda.manual_seed_all(seed)
        destination.parent.mkdir(parents=True, exist_ok=True)
        manifest = _manifest(sequence, self.id)
        with tempfile.NamedTemporaryFile(
            prefix=f".{destination.name}.", suffix=".tmp",
            dir=destination.parent, delete=False,
        ) as stream:
            temporary = Path(stream.name)
        try:
            with ZipFile(temporary, "w", compression=ZIP_DEFLATED) as archive:
                for ordinal, frame in enumerate(sequence):
                    low, faces, projected, normalization = preprocessing.build_pair(
                        frame.geometry.positions, frame.geometry.triangles,
                        coarse_size, num_subdiv,
                    )
                    coarse = torch.from_numpy(np.asarray(low, dtype=np.float32)).to(target)
                    triangles = torch.from_numpy(np.asarray(faces, dtype=np.int64)).to(target)
                    goal = torch.from_numpy(np.asarray(projected, dtype=np.float32)).to(target)
                    encoded, inputs, mean, std = _inputs(
                        torch, models, coarse, pe_dim, input_scale
                    )
                    model = models.MLP(3 * pe_dim, hidden_dim, 3, num_layers).to(target)
                    optimizer = torch.optim.AdamW(model.parameters(), lr=learning_rate)
                    graph = models.MeshDataset(
                        encoded, inputs, triangles, (goal - coarse) * output_scale,
                        progress=verbose,
                    )
                    loader = torch.utils.data.DataLoader(
                        graph, batch_size=batch_size, shuffle=False
                    )
                    model.train()
                    for _ in range(epochs):
                        for inputs, neighbors, weights, expected in loader:
                            optimizer.zero_grad(set_to_none=True)
                            loss = torch.nn.functional.mse_loss(
                                model(inputs, neighbors, weights), expected
                            )
                            loss.backward()
                            optimizer.step()
                    context = {
                        "schema": f"open4d.{self.id}/v1",
                        "coarse_vertices": coarse.cpu(), "coarse_faces": triangles.cpu(),
                        "input_mean": mean.cpu(), "input_std": std.cpu(),
                        "pe_dim": pe_dim, "hidden_dim": hidden_dim,
                        "num_layers": num_layers, "input_scale": input_scale,
                        "output_scale": output_scale, "normalization": normalization,
                    }
                    if self.int8:
                        context["quantized_engine"] = _quantized_engine(torch)
                        cpu_model = torch.ao.quantization.quantize_dynamic(
                            model.cpu().eval(), {torch.nn.Linear}, dtype=torch.qint8
                        )
                        example = (encoded.cpu(), graph.neighbors.cpu(), graph.edge_wts.cpu())
                        traced = torch.jit.trace(cpu_model, example, strict=False)
                        model_stream = BytesIO()
                        torch.jit.save(traced, model_stream)
                        archive.writestr(f"frames/{ordinal:06d}/model.pt", model_stream.getvalue())
                    else:
                        context["model_state_dict"] = model.state_dict()
                    archive.writestr(
                        f"frames/{ordinal:06d}/context.pt", _torch_bytes(torch, context)
                    )
                archive.writestr("manifest.json", json.dumps(manifest))
            temporary.replace(destination)
        except Exception:
            temporary.unlink(missing_ok=True)
            raise
        return destination

    def decode(
        self, source: Path, *, device: str | None = None, verbose: bool = False
    ) -> Sequence:
        torch, models, _ = _backend()
        target = torch.device("cpu" if self.int8 else (
            device or ("cuda" if torch.cuda.is_available() else "cpu")
        ))
        try:
            with ZipFile(source) as archive:
                manifest = json.loads(archive.read("manifest.json"))
                if manifest.get("schema") != self.schema:
                    raise CodecError(f"unsupported {self.id} artifact schema")
                frames = []
                for ordinal, record in enumerate(manifest["frames"]):
                    context = torch.load(
                        BytesIO(archive.read(f"frames/{ordinal:06d}/context.pt")),
                        map_location=target, weights_only=True,
                    )
                    if context.get("schema") != f"open4d.{self.id}/v1":
                        raise CodecError(f"invalid {self.id} frame context")
                    coarse, faces = context["coarse_vertices"].to(target), context["coarse_faces"].to(target)
                    inputs = (coarse * context["input_scale"] - context["input_mean"].to(target))
                    inputs /= context["input_std"].to(target)
                    encoded = models.PE(context["pe_dim"])(inputs)
                    graph = models.MeshDataset(
                        encoded, inputs, faces, torch.zeros_like(coarse), progress=verbose
                    )
                    if self.int8:
                        _quantized_engine(torch, context.get("quantized_engine"))
                        model = torch.jit.load(
                            BytesIO(archive.read(f"frames/{ordinal:06d}/model.pt")),
                            map_location="cpu",
                        ).eval()
                    else:
                        model = models.MLP(
                            3 * context["pe_dim"], context["hidden_dim"], 3,
                            context["num_layers"],
                        ).to(target)
                        model.load_state_dict(context["model_state_dict"])
                        model.eval()
                    with torch.inference_mode():
                        positions = coarse + model(
                            encoded, graph.neighbors, graph.edge_wts
                        ) / context["output_scale"]
                    normalization = context["normalization"]
                    positions = positions * float(normalization["scale"])
                    positions += torch.tensor(normalization["bbox_min"], device=target)
                    frames.append(Frame(
                        record["frame_index"], record["timestamp"], TriangleMesh(
                            positions.cpu().numpy(), faces.cpu().numpy()
                        ), record.get("metadata", {}),
                    ))
        except (BadZipFile, KeyError, json.JSONDecodeError) as error:
            raise CodecError(f"invalid {self.id} artifact {source}: {error}") from error
        return Sequence(MemoryFrameProvider(
            frames, metadata=manifest.get("metadata", {}), topology=TopologyMode.CHANGING,
            has_constant_vertex_count=None, has_vertex_correspondence=False,
        ))


QNDF_CODEC = QNDFCodec()
QNDF_INT8_CODEC = QNDFCodec(int8=True)
