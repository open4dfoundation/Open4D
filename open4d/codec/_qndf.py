"""In-process adapters for Open4D's neural displacement field codecs."""

from __future__ import annotations

from collections.abc import Mapping
from importlib import import_module
from io import BytesIO
import json
import math
from numbers import Real
from pathlib import Path
import tempfile
from zipfile import BadZipFile, ZIP_DEFLATED, ZipFile

import numpy as np

from open4d.core import Frame, MemoryFrameProvider, Sequence, TopologyMode, TriangleMesh

from ._npz import _json_value, _publish_file, _validate_manifest
from ._protocol import CodecError
from ._research import research_module
from ._torch import torch_device


def _backend():
    try:
        return (
            import_module("torch"),
            research_module("qndf.compress"),
            research_module("qndf.build_dataset_open3d"),
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
        "allow_nonmonotonic_timestamps": sequence.allow_nonmonotonic_timestamps,
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


def _validate_model_options(pe_dim, hidden_dim, num_layers, input_scale, output_scale):
    for name, value in (("pe_dim", pe_dim), ("hidden_dim", hidden_dim), ("num_layers", num_layers)):
        if type(value) is not int or value < 1:
            raise ValueError(f"{name} must be a positive integer")
    if pe_dim % 2:
        raise ValueError("pe_dim must be even")
    for name, value in (("input_scale", input_scale), ("output_scale", output_scale)):
        if isinstance(value, bool) or not isinstance(value, Real) or not math.isfinite(value):
            raise ValueError(f"{name} must be a finite number")
    if output_scale == 0:
        raise ValueError("output_scale must be nonzero")


def _validate_context(torch, context, schema):
    try:
        if not isinstance(context, Mapping) or context.get("schema") != schema:
            raise ValueError("unsupported schema or context is not an object")
        _validate_model_options(*(context[name] for name in (
            "pe_dim", "hidden_dim", "num_layers", "input_scale", "output_scale",
        )))
        for name in ("coarse_vertices", "input_mean", "input_std"):
            value = context[name]
            if (not isinstance(value, torch.Tensor) or value.layout != torch.strided
                    or value.dtype != torch.float32 or not torch.isfinite(value).all()):
                raise ValueError(f"{name} must be a finite float32 tensor")
            shape = value.shape
            if ((name == "coarse_vertices" and (len(shape) != 2 or shape[1] != 3 or shape[0] == 0))
                    or (name != "coarse_vertices" and shape != (1, 3))):
                raise ValueError(f"invalid {name} dimensions")
        if torch.any(context["input_std"] <= 0):
            raise ValueError("input_std must be positive")
        faces = context["coarse_faces"]
        if (not isinstance(faces, torch.Tensor) or faces.layout != torch.strided
                or faces.dtype != torch.int64 or faces.ndim != 2 or faces.shape[1] != 3
                or torch.any(faces < 0) or torch.any(faces >= len(context["coarse_vertices"]))):
            raise ValueError("coarse_faces must contain valid int64 triangle indices")
        normalization = context["normalization"]
        if not isinstance(normalization, Mapping):
            raise ValueError("normalization must be an object")
        scale, lower = normalization["scale"], np.asarray(normalization["bbox_min"])
        if (isinstance(scale, bool) or not isinstance(scale, Real) or not math.isfinite(scale)
                or scale <= 0 or lower.shape != (3,) or lower.dtype.kind not in "fiu"
                or not np.isfinite(lower).all()):
            raise ValueError("normalization requires finite XYZ bounds and a positive scale")
        if not isinstance(context["model_state_dict"], Mapping):
            raise ValueError("model_state_dict must be an object")
    except (KeyError, TypeError, ValueError) as error:
        raise CodecError(f"invalid QNDF frame context: {error}") from error


class QNDFCodec:
    backend = "python-in-process"
    lossless = False
    preserves = ("positions", "triangles")

    def __init__(self, *, int8: bool = False) -> None:
        self.int8 = int8
        self.id = "qndf-int8" if int8 else "qndf"
        self.suffixes = (".qi4d",) if int8 else (".q4d",)
        self.version = 2 if int8 else 1
        self.schema = f"open4d.{self.id}-sequence/v{self.version}"

    def can_decode(self, source: Path) -> bool:
        try:
            with ZipFile(source) as archive:
                manifest = json.loads(archive.read("manifest.json"))
                return isinstance(manifest, dict) and manifest.get("schema") == self.schema
        except (OSError, BadZipFile, KeyError, ValueError, TypeError):
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
        _validate_model_options(pe_dim, hidden_dim, num_layers, input_scale, output_scale)
        for name, value in (("coarse_size", coarse_size), ("epochs", epochs), ("batch_size", batch_size)):
            if type(value) is not int or value < 1:
                raise ValueError(f"{name} must be a positive integer")
        if type(num_subdiv) is not int or num_subdiv < 0:
            raise ValueError("num_subdiv must be a nonnegative integer")
        destination = Path(destination).absolute()
        if destination.exists() and not overwrite:
            raise FileExistsError(f"artifact already exists: {destination}")
        for frame in sequence:
            mesh = frame.geometry
            if any((mesh.colors is not None, mesh.normals is not None,
                    mesh.texture_coordinates is not None, bool(mesh.attributes))):
                raise CodecError(f"{self.id} cannot preserve mesh attributes")
        torch, models, preprocessing = _backend()
        target = torch_device(torch, device)
        torch.manual_seed(seed)
        if torch.cuda.is_available():
            torch.cuda.manual_seed_all(seed)
        destination.parent.mkdir(parents=True, exist_ok=True)
        manifest = _manifest(sequence, self.id)
        manifest["schema"] = self.schema
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
                        "schema": f"open4d.{self.id}/v{self.version}",
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
                        context["model_state_dict"] = cpu_model.state_dict()
                    else:
                        context["model_state_dict"] = model.state_dict()
                    _validate_context(torch, context, f"open4d.{self.id}/v{self.version}")
                    archive.writestr(
                        f"frames/{ordinal:06d}/context.pt", _torch_bytes(torch, context)
                    )
                archive.writestr("manifest.json", json.dumps(manifest))
            _publish_file(temporary, destination, overwrite=overwrite)
        except Exception:
            temporary.unlink(missing_ok=True)
            raise
        return destination

    def decode(
        self, source: Path, *, device: str | None = None, verbose: bool = False
    ) -> Sequence:
        try:
            with ZipFile(source) as archive:
                manifest = json.loads(archive.read("manifest.json"))
                if self.int8 and isinstance(manifest, dict) and manifest.get("schema") == "open4d.qndf-int8-sequence/v1":
                    raise CodecError("QNDF-int8 v1 contains executable models; re-encode with the current version")
                _validate_manifest(manifest, schema=self.schema, codec=self.id)
                torch, models, _ = _backend()
                target = torch.device("cpu") if self.int8 else torch_device(torch, device)
                frames = []
                for ordinal, record in enumerate(manifest["frames"]):
                    context = torch.load(
                        BytesIO(archive.read(f"frames/{ordinal:06d}/context.pt")),
                        map_location=target, weights_only=True,
                    )
                    _validate_context(torch, context, f"open4d.{self.id}/v{self.version}")
                    coarse, faces = context["coarse_vertices"].to(target), context["coarse_faces"].to(target)
                    inputs = (coarse * context["input_scale"] - context["input_mean"].to(target))
                    inputs /= context["input_std"].to(target)
                    encoded = models.PE(context["pe_dim"])(inputs)
                    graph = models.MeshDataset(
                        encoded, inputs, faces, torch.zeros_like(coarse), progress=verbose
                    )
                    if self.int8:
                        _quantized_engine(torch, context.get("quantized_engine"))
                        model = models.MLP(3 * context["pe_dim"], context["hidden_dim"], 3,
                                           context["num_layers"]).eval()
                        model = torch.ao.quantization.quantize_dynamic(
                            model, {torch.nn.Linear}, dtype=torch.qint8,
                        )
                        model.load_state_dict(context["model_state_dict"])
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
            allow_nonmonotonic_timestamps=manifest.get(
                "allow_nonmonotonic_timestamps", False
            ),
        ))


QNDF_CODEC = QNDFCodec()
QNDF_INT8_CODEC = QNDFCodec(int8=True)
