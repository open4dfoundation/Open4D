"""Neural displacement field codecs with direct standalone O4D carriage."""

from __future__ import annotations

from collections.abc import Mapping
from importlib import import_module
from io import BytesIO
import json
import math
from numbers import Real
from pathlib import Path
import pickle
import tempfile
from zipfile import BadZipFile, ZipFile

import numpy as np

from open4d.core import Frame, MemoryFrameProvider, Sequence, TopologyMode, TriangleMesh

from .._metadata import _json_value, _validate_manifest
from .._protocol import CodecError
from .._research import research_module
from .._stages import Stages, checkpoint_destinations, validate_stage_hook
from .._torch import torch_device
from .._o4d_format import contains_codec, pack_o4d, probe_codec, unpack_o4d

_MAX_CONTEXT_BYTES = 64 * 1024 * 1024
_MAX_GRAPH_ELEMENTS = 64 * 1024 * 1024


def _backend():
    try:
        return (
            *_model_backend(),
            research_module("qndf.build_dataset_open3d"),
        )
    except ImportError as error:
        raise CodecError("QNDF dependencies are missing; install open4d[qndf]") from error


def _model_backend():
    try:
        return import_module("torch"), research_module("qndf.compress")
    except ImportError as error:
        raise CodecError("QNDF decoding needs PyTorch; install open4d[qndf]") from error


def _torch_bytes(torch, value) -> bytes:
    stream = BytesIO()
    torch.save(value, stream)
    return stream.getvalue()


def _training_batches(graph, batch_size):
    """Slice the resident graph tensors with the original unshuffled batches.

    DataLoader's default collation indexes every CUDA vertex separately and
    stacks it again. The model dataset already supports tensor slicing, so
    slicing a batch avoids thousands of Python calls and tiny GPU operations.
    """
    for start in range(0, len(graph), batch_size):
        yield graph[slice(start, start + batch_size)]


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


def _manifest(sequence, codec, profile=1):
    return {
        "version": 1, "codec": codec, "native": {"profile": f"{codec}/{profile}"},
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
    if pe_dim > 128 or hidden_dim > 4096 or num_layers > 128:
        raise ValueError("QNDF model dimensions outside limits")
    parameters = (2 * 3 * pe_dim * hidden_dim + (num_layers + 1) * hidden_dim ** 2
                  + 3 * (num_layers + 3) * hidden_dim + 3 * hidden_dim + 3)
    if parameters > 16 * 1024 * 1024:
        raise ValueError("QNDF model parameter count outside limits")
    for name, value in (("input_scale", input_scale), ("output_scale", output_scale)):
        if isinstance(value, bool) or not isinstance(value, Real) or not math.isfinite(value):
            raise ValueError(f"{name} must be a finite number")
    if output_scale == 0:
        raise ValueError("output_scale must be nonzero")


_COMMON_FIELDS = frozenset(("version", "input_mean", "input_std", "pe_dim", "hidden_dim",
                            "num_layers", "input_scale", "output_scale", "normalization",
                            "model_state_dict"))
_CONTEXT_FIELDS = {
    1: _COMMON_FIELDS | {"coarse_vertices", "coarse_faces"},
    # Profile 2 stores the simplified base mesh and regenerates the subdivided
    # network input with the deterministic midpoint rule below.
    2: _COMMON_FIELDS | {"base_vertices", "base_faces", "component_vertices",
                         "component_faces", "num_subdiv", "subdivision"},
}
_SUBDIVISION = "midpoint/1"
_MAX_SUBDIVISIONS = 6


def _subdivide_midpoint(vertices: np.ndarray, faces: np.ndarray, iterations: int):
    """Open3D-ordered midpoint subdivision of one component, deterministically.

    New vertices are appended in the order edges are first met while visiting
    triangles in order, edges (0, 1), (1, 2), (2, 0). Each triangle becomes
    ``(v0, v01, v20), (v01, v1, v12), (v12, v2, v20), (v01, v12, v20)``. This
    reproduces ``open3d.geometry.TriangleMesh.subdivide_midpoint``; the encoder
    verifies the match before writing profile 2. Midpoints are computed in
    float64 from the stored float32 base.
    """
    vertices = np.asarray(vertices, dtype=np.float64)
    faces = np.asarray(faces, dtype=np.int64).reshape(-1, 3)
    for _ in range(iterations):
        if not len(faces):
            break
        edges = np.sort(faces[:, [0, 1, 1, 2, 2, 0]].reshape(-1, 2), axis=1)
        keys = edges[:, 0] * len(vertices) + edges[:, 1]
        _, first, inverse = np.unique(keys, return_index=True, return_inverse=True)
        order = np.argsort(first, kind="stable")
        rank = np.empty_like(order)
        rank[order] = np.arange(len(order))
        created = edges[first[order]]
        midpoint = (len(vertices) + rank[inverse.reshape(-1)]).reshape(-1, 3)
        vertices = np.concatenate((
            vertices, 0.5 * (vertices[created[:, 0]] + vertices[created[:, 1]])
        ))
        v01, v12, v20 = midpoint[:, 0], midpoint[:, 1], midpoint[:, 2]
        v0, v1, v2 = faces[:, 0], faces[:, 1], faces[:, 2]
        faces = np.stack((
            np.stack((v0, v01, v20), 1), np.stack((v01, v1, v12), 1),
            np.stack((v12, v2, v20), 1), np.stack((v01, v12, v20), 1),
        ), axis=1).reshape(-1, 3)
    return vertices, faces


def _subdivide_components(vertices, faces, vertex_counts, face_counts, iterations):
    """Subdivide concatenated components independently, preserving block order."""
    vertices = np.asarray(vertices, dtype=np.float64)
    faces = np.asarray(faces, dtype=np.int64)
    output_vertices, output_faces = [], []
    vertex_start = face_start = offset = 0
    for vertex_count, face_count in zip(vertex_counts, face_counts):
        component = faces[face_start:face_start + face_count] - vertex_start
        divided_vertices, divided_faces = _subdivide_midpoint(
            vertices[vertex_start:vertex_start + vertex_count], component, iterations
        )
        output_vertices.append(divided_vertices)
        output_faces.append(divided_faces + offset)
        offset += len(divided_vertices)
        vertex_start += int(vertex_count)
        face_start += int(face_count)
    return np.concatenate(output_vertices), np.concatenate(output_faces)


def _validate_triangles(torch, faces, vertex_count, name, dtypes):
    if (not isinstance(faces, torch.Tensor) or faces.layout != torch.strided
            or faces.dtype not in dtypes or faces.ndim != 2
            or faces.shape[1] != 3 or torch.any(faces < 0) or torch.any(faces >= vertex_count)):
        raise ValueError(f"{name} must contain valid integer triangle indices")


def _validate_context(torch, context, *, int8=False, version=None):
    try:
        if not isinstance(context, Mapping) or type(context.get("version")) is not int:
            raise ValueError("unsupported context/version or fields")
        if version is not None and context["version"] != version:
            raise ValueError("context version disagrees with the O4D profile")
        required = set(_CONTEXT_FIELDS.get(context["version"], ()))
        if int8:
            required.add("quantized_engine")
        if not required or set(context) != required:
            raise ValueError("unsupported context/version or fields")
        _validate_model_options(*(context[name] for name in (
            "pe_dim", "hidden_dim", "num_layers", "input_scale", "output_scale",
        )))
        vertices_name = "coarse_vertices" if context["version"] == 1 else "base_vertices"
        for name in (vertices_name, "input_mean", "input_std"):
            value = context[name]
            if (not isinstance(value, torch.Tensor) or value.layout != torch.strided
                    or value.dtype != torch.float32 or not torch.isfinite(value).all()):
                raise ValueError(f"{name} must be a finite float32 tensor")
            shape = value.shape
            if ((name == vertices_name and (len(shape) != 2 or shape[1] != 3 or shape[0] == 0))
                    or (name != vertices_name and shape != (1, 3))):
                raise ValueError(f"invalid {name} dimensions")
        if torch.any(context["input_std"] <= 0):
            raise ValueError("input_std must be positive")
        vertex_count = len(context[vertices_name])
        if context["version"] == 1:
            _validate_triangles(torch, context["coarse_faces"], vertex_count, "coarse_faces",
                                (torch.int64,))
        else:
            _validate_base(torch, context, vertex_count)
            vertex_count += len(context["base_faces"]) * (4 ** context["num_subdiv"] - 1)
        if vertex_count * 3 * context["pe_dim"] * 14 > _MAX_GRAPH_ELEMENTS:
            raise ValueError("QNDF neighbor graph outside limits")
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
        if int8 and not isinstance(context["quantized_engine"], str):
            raise ValueError("quantized_engine must be a string")
    except (KeyError, TypeError, ValueError) as error:
        raise CodecError(f"invalid QNDF frame context: {error}") from error


def _validate_base(torch, context, vertex_count):
    if context["subdivision"] != _SUBDIVISION:
        raise ValueError(f"unsupported subdivision rule {context['subdivision']!r}")
    levels = context["num_subdiv"]
    if type(levels) is not int or not 0 <= levels <= _MAX_SUBDIVISIONS:
        raise ValueError("num_subdiv outside limits")
    faces = context["base_faces"]
    _validate_triangles(torch, faces, vertex_count, "base_faces", (torch.int32, torch.int64))
    counts = []
    for name in ("component_vertices", "component_faces"):
        value = context[name]
        if (not isinstance(value, torch.Tensor) or value.dtype != torch.int64
                or value.ndim != 1 or len(value) == 0 or torch.any(value < 1)):
            raise ValueError(f"{name} must be a nonempty int64 vector of positive counts")
        counts.append(value)
    vertex_counts, face_counts = counts
    if (len(vertex_counts) != len(face_counts) or int(vertex_counts.sum()) != vertex_count
            or int(face_counts.sum()) != len(faces)):
        raise ValueError("component counts disagree with the base mesh")
    lower = torch.repeat_interleave(torch.cumsum(vertex_counts, 0) - vertex_counts, face_counts)
    upper = lower + torch.repeat_interleave(vertex_counts, face_counts)
    if torch.any(faces.to(torch.int64) < lower[:, None]) or torch.any(faces.to(torch.int64) >= upper[:, None]):
        raise ValueError("base_faces cross component boundaries")


def _extract_legacy_qndf(source: Path, destination: Path, *, int8: bool) -> dict:
    """Convert safe legacy weights explicitly, without carrying their archive."""
    codec = "qndf-int8" if int8 else "qndf"
    version = 2 if int8 else 1
    try:
        with ZipFile(source) as archive:
            members = archive.infolist()
            if len({member.filename for member in members}) != len(members):
                raise CodecError("duplicate QNDF archive member")
            if archive.getinfo("manifest.json").file_size > 16 * 1024 * 1024:
                raise CodecError("oversized QNDF manifest")
            manifest = json.loads(archive.read("manifest.json"))
            if int8 and isinstance(manifest, dict) and manifest.get("schema") == "open4d.qndf-int8-sequence/v1":
                raise CodecError("QNDF-int8 v1 contains executable models; re-encode it")
            _validate_manifest(manifest, schema=f"open4d.{codec}-sequence/v{version}", codec=codec)
            metadata = {key: value for key, value in manifest.items() if key != "schema"}
            metadata.update(version=1, native={"profile": f"{codec}/1"})
            from .._native_profiles import layout
            layout(codec, len(metadata["frames"]), metadata["native"])
            expected = {"manifest.json", *(f"frames/{i:06d}/context.pt" for i in range(len(metadata["frames"])))}
            if set(archive.namelist()) != expected:
                raise CodecError("unexpected QNDF archive payloads")
            torch = import_module("torch")
            for ordinal in range(len(metadata["frames"])):
                member = archive.getinfo(f"frames/{ordinal:06d}/context.pt")
                if not 0 < member.file_size <= _MAX_CONTEXT_BYTES:
                    raise CodecError("QNDF legacy context outside limits")
                context = torch.load(BytesIO(archive.read(member)), map_location="cpu", weights_only=True)
                if not isinstance(context, Mapping) or context.pop("schema", None) != f"open4d.{codec}/v{version}":
                    raise CodecError("invalid legacy QNDF frame context")
                context["version"] = 1
                _validate_context(torch, context, int8=int8)
                torch.save(context, destination / f"frame_{ordinal:06d}.pt")
        (destination / "metadata.json").write_text(json.dumps(metadata, allow_nan=False), encoding="utf-8")
        return metadata
    except (BadZipFile, KeyError, ValueError, TypeError, RuntimeError, pickle.UnpicklingError) as error:
        raise CodecError(f"invalid legacy {codec} artifact: {error}") from error


class QNDFCodec:
    backend = "python-in-process"
    lossless = False
    preserves = ("positions", "triangles")

    def __init__(self, *, int8: bool = False) -> None:
        self.int8 = int8
        self.id = "qndf-int8" if int8 else "qndf"
        self.suffixes = (".o4d",)

    def can_decode(self, source: Path) -> bool:
        return contains_codec(source, self.id)

    def encode(
        self, sequence: Sequence, destination: Path, *, overwrite: bool = False,
        coarse_size: int = 3000, num_subdiv: int = 2, pe_dim: int = 20,
        hidden_dim: int = 56, num_layers: int = 20, epochs: int = 300,
        batch_size: int = 2048, learning_rate: float = 1e-3,
        lr_schedule: str = "constant",
        input_scale: float = 1000, output_scale: float = 1414,
        device: str | None = None, seed: int = 7, verbose: bool = False,
        profile: int = 1, checkpoint_artifacts: Mapping[int, Path] | None = None,
        stage_hook=None,
    ) -> Path:
        """Train one displacement network per frame and write an O4D artifact.

        ``lr_schedule="cosine"`` anneals the learning rate to zero over
        ``epochs`` (the published QNDF recipe uses 3,500 epochs, batch 2048).
        ``profile=2`` stores the simplified base mesh and the midpoint
        subdivision recipe instead of the subdivided mesh; decoders regenerate
        the identical network input. ``checkpoint_artifacts`` maps intermediate
        epochs to extra ``.o4d`` destinations exported from the same run.
        ``stage_hook`` receives instrumentation events (see ``_stages``).
        """
        if not len(sequence):
            raise CodecError("QNDF cannot encode an empty sequence")
        _validate_model_options(pe_dim, hidden_dim, num_layers, input_scale, output_scale)
        for name, value in (("coarse_size", coarse_size), ("epochs", epochs), ("batch_size", batch_size)):
            if type(value) is not int or value < 1:
                raise ValueError(f"{name} must be a positive integer")
        if type(num_subdiv) is not int or num_subdiv < 0:
            raise ValueError("num_subdiv must be a nonnegative integer")
        if lr_schedule not in ("constant", "cosine"):
            raise ValueError("lr_schedule must be 'constant' or 'cosine'")
        if type(profile) is not int or profile not in (1, 2):
            raise ValueError("profile must be 1 (subdivided mesh) or 2 (base mesh and recipe)")
        if profile == 2 and num_subdiv > _MAX_SUBDIVISIONS:
            raise ValueError(f"profile 2 supports at most {_MAX_SUBDIVISIONS} subdivisions")
        validate_stage_hook(stage_hook)
        destination = Path(destination).absolute()
        if destination.suffix.lower() != ".o4d":
            raise ValueError("QNDF destination must have a .o4d extension")
        if destination.exists() and not overwrite:
            raise FileExistsError(f"artifact already exists: {destination}")
        checkpoints = checkpoint_destinations(checkpoint_artifacts, epochs, destination, overwrite)
        for frame in sequence:
            mesh = frame.geometry
            if any((mesh.colors is not None, mesh.normals is not None,
                    mesh.texture_coordinates is not None, bool(mesh.attributes))):
                raise CodecError(f"{self.id} cannot preserve mesh attributes")
        torch, models, preprocessing = _backend()
        target = torch_device(torch, device)
        stages = Stages(stage_hook, torch, target)
        torch.manual_seed(seed)
        if torch.cuda.is_available():
            torch.cuda.manual_seed_all(seed)
        destination.parent.mkdir(parents=True, exist_ok=True)
        manifest = _manifest(sequence, self.id, profile)
        with tempfile.TemporaryDirectory(prefix="open4d-qndf-") as directory:
            outputs = {epoch: Path(directory) / f"epoch-{epoch}" for epoch in (*checkpoints, epochs)}
            for native in outputs.values():
                native.mkdir()
            for ordinal, frame in enumerate(sequence):
                with stages.phase("preprocessing", ordinal):
                    if profile == 1:
                        low, faces, projected, normalization = preprocessing.build_pair(
                            frame.geometry.positions, frame.geometry.triangles,
                            coarse_size, num_subdiv,
                        )
                        base = None
                    else:
                        low, faces, projected, normalization, base = _build_base_pair(
                            torch, preprocessing, frame.geometry, coarse_size, num_subdiv,
                        )
                    coarse = torch.from_numpy(np.asarray(low, dtype=np.float32)).to(target)
                    triangles = torch.from_numpy(np.asarray(faces, dtype=np.int64)).to(target)
                    goal = torch.from_numpy(np.asarray(projected, dtype=np.float32)).to(target)
                    encoded, inputs, mean, std = _inputs(
                        torch, models, coarse, pe_dim, input_scale
                    )
                    graph = models.MeshDataset(
                        encoded, inputs, triangles, (goal - coarse) * output_scale,
                        progress=verbose,
                    )
                restore = (float(normalization["scale"]),
                           np.asarray(normalization["bbox_min"], dtype=np.float64))
                stages.mesh("target", ordinal, np.asarray(projected) * restore[0] + restore[1], faces)
                model = models.MLP(3 * pe_dim, hidden_dim, 3, num_layers).to(target)
                optimizer = torch.optim.AdamW(model.parameters(), lr=learning_rate)
                scheduler = (torch.optim.lr_scheduler.CosineAnnealingLR(optimizer, epochs)
                             if lr_schedule == "cosine" else None)
                states = {}
                with stages.phase("training", ordinal):
                    model.train()
                    for epoch in range(1, epochs + 1):
                        for inputs, neighbors, weights, expected in _training_batches(graph, batch_size):
                            optimizer.zero_grad(set_to_none=True)
                            loss = torch.nn.functional.mse_loss(
                                model(inputs, neighbors, weights), expected
                            )
                            loss.backward()
                            optimizer.step()
                        if scheduler is not None:
                            scheduler.step()
                        if epoch in outputs:
                            states[epoch] = {
                                key: value.detach().clone()
                                for key, value in model.state_dict().items()
                            }
                fields = {
                    "input_mean": mean.cpu(), "input_std": std.cpu(),
                    "pe_dim": pe_dim, "hidden_dim": hidden_dim,
                    "num_layers": num_layers, "input_scale": input_scale,
                    "output_scale": output_scale,
                }
                if profile == 1:
                    fields.update(version=1, coarse_vertices=coarse.cpu(),
                                  coarse_faces=triangles.cpu(), normalization=normalization)
                else:
                    # Only the restoring transform is needed to decode.
                    fields.update(version=2, **base, normalization={
                        "bbox_min": [float(value) for value in normalization["bbox_min"]],
                        "scale": float(normalization["scale"]),
                    })
                with stages.phase("export", ordinal):
                    for epoch, native in outputs.items():
                        context = dict(fields)
                        if self.int8:
                            context["quantized_engine"] = _quantized_engine(torch)
                            cpu_model = models.MLP(3 * pe_dim, hidden_dim, 3, num_layers)
                            cpu_model.load_state_dict({
                                key: value.cpu() for key, value in states[epoch].items()
                            })
                            cpu_model = torch.ao.quantization.quantize_dynamic(
                                cpu_model.eval(), {torch.nn.Linear}, dtype=torch.qint8
                            )
                            context["model_state_dict"] = cpu_model.state_dict()
                        else:
                            context["model_state_dict"] = states[epoch]
                        _validate_context(torch, context, int8=self.int8, version=profile)
                        torch.save(context, native / f"frame_{ordinal:06d}.pt")
                if stages.enabled:
                    model.eval()
                    for epoch in outputs:
                        model.load_state_dict(states[epoch])
                        with torch.inference_mode():
                            positions = coarse + model(
                                encoded, graph.neighbors, graph.edge_wts
                            ) / output_scale
                        stages.mesh("trained" if epoch == epochs else f"trained@{epoch}", ordinal,
                                    positions.cpu().numpy() * restore[0] + restore[1], faces)
                del graph, states, model, optimizer
            with stages.phase("packing"):
                for epoch, native in outputs.items():
                    (native / "metadata.json").write_text(
                        json.dumps(manifest, allow_nan=False), encoding="utf-8"
                    )
                    pack_o4d(native, checkpoints.get(epoch, destination), overwrite=overwrite)
        return destination

    def decode(
        self, source: Path, *, device: str | None = None, verbose: bool = False
    ) -> Sequence:
        source = Path(source).absolute()
        if source.suffix.lower() != ".o4d":
            raise CodecError("QNDF decoding requires .o4d; migrate the legacy artifact explicitly")
        if probe_codec(source) != self.id:
            raise CodecError(f"O4D does not contain {self.id}")
        with tempfile.TemporaryDirectory(prefix="open4d-qndf-decode-") as directory:
            native = Path(directory) / "native"
            unpack_o4d(source, native)
            manifest = json.loads((native / "metadata.json").read_text(encoding="utf-8"))
            profile = 2 if manifest["native"]["profile"] == f"{self.id}/2" else 1
            try:
                torch, models = _model_backend()
                target = torch.device("cpu") if self.int8 else torch_device(torch, device)
                frames = []
                for ordinal, record in enumerate(manifest["frames"]):
                    context_path = native / f"frame_{ordinal:06d}.pt"
                    if context_path.stat().st_size > _MAX_CONTEXT_BYTES:
                        raise CodecError("QNDF context outside limits")
                    context = torch.load(
                        context_path,
                        map_location=target, weights_only=True,
                    )
                    _validate_context(torch, context, int8=self.int8, version=profile)
                    if profile == 1:
                        coarse, faces = context["coarse_vertices"], context["coarse_faces"]
                    else:
                        vertices, triangles = _subdivide_components(
                            context["base_vertices"].cpu().numpy(),
                            context["base_faces"].cpu().numpy(),
                            context["component_vertices"].tolist(),
                            context["component_faces"].tolist(), context["num_subdiv"],
                        )
                        coarse = torch.from_numpy(vertices.astype(np.float32))
                        faces = torch.from_numpy(triangles)
                    coarse, faces = coarse.to(target), faces.to(target)
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
            except (KeyError, ValueError, TypeError, RuntimeError, pickle.UnpicklingError) as error:
                raise CodecError(f"invalid {self.id} native payload: {error}") from error
        return Sequence(MemoryFrameProvider(
            frames, metadata=manifest.get("metadata", {}), topology=TopologyMode.CHANGING,
            has_constant_vertex_count=None, has_vertex_correspondence=False,
            allow_nonmonotonic_timestamps=manifest.get(
                "allow_nonmonotonic_timestamps", False
            ),
        ))


def _build_base_pair(torch, preprocessing, mesh, coarse_size, num_subdiv):
    """Build a profile 2 training pair whose input is regenerated from its base."""
    try:
        low, faces, projected, normalization, base = preprocessing.build_pair(
            mesh.positions, mesh.triangles, coarse_size, num_subdiv, return_base=True,
        )
    except TypeError as error:
        raise CodecError(
            "QNDF profile 2 needs build_pair(..., return_base=True) from this Open4D "
            "revision's QNDF source; use profile=1 with older sources"
        ) from error
    base_vertices = np.asarray(base[0], dtype=np.float32)
    base_faces = np.asarray(base[1], dtype=np.int64)
    vertex_counts = np.asarray(base[2], dtype=np.int64)
    face_counts = np.asarray(base[3], dtype=np.int64)
    vertices, triangles = _subdivide_components(
        base_vertices, base_faces, vertex_counts, face_counts, num_subdiv
    )
    reference = np.asarray(low, dtype=np.float64)
    # Faces must match exactly; vertices differ only by the float32 base.
    if (triangles.shape != np.shape(faces) or not np.array_equal(triangles, faces)
            or vertices.shape != reference.shape
            or not np.allclose(vertices, reference, rtol=0, atol=1e-6)):
        raise CodecError(
            "QNDF profile 2 could not reproduce the preprocessing subdivision order; "
            "use profile=1"
        )
    index_type = np.int32 if len(base_vertices) < 2 ** 31 else np.int64
    return vertices.astype(np.float32), triangles, projected, normalization, {
        "base_vertices": torch.from_numpy(base_vertices),
        "base_faces": torch.from_numpy(base_faces.astype(index_type)),
        "component_vertices": torch.from_numpy(vertex_counts),
        "component_faces": torch.from_numpy(face_counts),
        "num_subdiv": num_subdiv, "subdivision": _SUBDIVISION,
    }


QNDF_CODEC = QNDFCodec()
QNDF_INT8_CODEC = QNDFCodec(int8=True)
