"""Experimental reference-topology sequence codecs authored by Open4D."""

from __future__ import annotations

from importlib import import_module
import json
from pathlib import Path
import tempfile

import numpy as np

from open4d.core import Frame, MemoryFrameProvider, Sequence, TopologyMode, TriangleMesh

from ._npz import _json_value
from ._protocol import CodecError


def _manifest(sequence, codec):
    return {
        "schema": f"open4d.{codec}-sequence/v1", "codec": codec,
        "metadata": _json_value(sequence.metadata, "sequence"),
        "frames": [{
            "frame_index": frame.frame_index, "timestamp": frame.timestamp,
            "metadata": _json_value(frame.metadata, f"frame {ordinal}"),
        } for ordinal, frame in enumerate(sequence)],
    }


def _fit(sequence: Sequence, face_budget: int):
    if not len(sequence):
        raise CodecError("temporal mesh codecs cannot encode an empty sequence")
    first = sequence[0].geometry
    for frame in sequence:
        mesh = frame.geometry
        if not len(mesh.positions) or not len(mesh.triangles):
            raise CodecError("temporal mesh codecs need non-empty triangle meshes")
        if any((mesh.colors is not None, mesh.normals is not None,
                mesh.texture_coordinates is not None, bool(mesh.attributes))):
            raise CodecError("experimental temporal codecs cannot preserve mesh attributes")
    if len(first.triangles) > face_budget:
        try:
            open3d = import_module("open3d")
        except ImportError as error:
            raise CodecError("surface fitting needs open4d[temporal]") from error
        mesh = open3d.geometry.TriangleMesh(
            open3d.utility.Vector3dVector(first.positions),
            open3d.utility.Vector3iVector(first.triangles),
        )
        simplified = mesh.simplify_quadric_decimation(face_budget)
        reference = np.asarray(simplified.vertices, dtype=np.float32)
        faces = np.asarray(simplified.triangles, dtype=np.uint32)
        if not len(reference) or not len(faces):
            raise CodecError("surface fitting produced an empty reference mesh")
    else:
        reference, faces = first.positions.copy(), first.triangles.copy()
    fitted = []
    for frame in sequence:
        mesh = frame.geometry
        if (len(reference) == len(mesh.positions)
                and np.array_equal(faces, mesh.triangles)):
            fitted.append(mesh.positions.copy())
            continue
        try:
            pcu = import_module("point_cloud_utils")
        except ImportError as error:
            raise CodecError("changing-topology fitting needs open4d[temporal]") from error
        _, face_indices, barycentric = pcu.closest_points_on_mesh(
            np.asarray(reference, dtype=np.float64),
            np.asarray(mesh.positions, dtype=np.float64),
            np.asarray(mesh.triangles, dtype=np.int32),
        )
        corners = mesh.positions[mesh.triangles[face_indices]]
        fitted.append(np.einsum("vij,vi->vj", corners, barycentric))
    return np.asarray(reference, dtype=np.float32), np.asarray(faces), np.asarray(fitted)


def _quantize(values, bits):
    if not 2 <= bits <= 16:
        raise ValueError("quantization_bits must be in [2, 16]")
    limit = 2 ** (bits - 1) - 1
    maximum = float(np.max(np.abs(values), initial=0))
    scale = maximum / limit if maximum else 1.0
    dtype = np.int8 if bits <= 8 else np.int16
    return np.rint(values / scale).clip(-limit, limit).astype(dtype), scale


class TemporalMeshCodec:
    backend = "python-experimental"
    lossless = False
    preserves = ("positions", "triangles")

    def __init__(self, identifier: str) -> None:
        if identifier not in {"temporal-delta", "temporal-pca"}:
            raise ValueError(f"unknown experimental temporal profile: {identifier}")
        self.id = identifier
        self.suffixes = ((".td4d",) if identifier == "temporal-delta" else (".tp4d",))
        self.schema = f"open4d.{identifier}-sequence/v1"

    def can_decode(self, source: Path) -> bool:
        try:
            with np.load(source, allow_pickle=False) as artifact:
                manifest = json.loads(artifact["manifest"].tobytes())
                return manifest.get("schema") == self.schema
        except (OSError, ValueError, KeyError, json.JSONDecodeError):
            return False

    def encode(
        self, sequence: Sequence, destination: Path, *, overwrite: bool = False,
        face_budget: int = 3000, quantization_bits: int = 16,
        components: int = 5,
    ) -> Path:
        if face_budget < 1:
            raise ValueError("face_budget must be positive")
        destination = Path(destination).absolute()
        if destination.exists() and not overwrite:
            raise FileExistsError(f"artifact already exists: {destination}")
        reference, faces, fitted = _fit(sequence, face_budget)
        displacement = fitted - reference[None]
        payload = {"reference": reference, "triangles": faces}
        if self.id == "temporal-delta":
            payload["displacement"], payload["scale"] = _quantize(
                displacement, quantization_bits
            )
        else:
            trajectories = displacement.transpose(1, 0, 2).reshape(len(reference), -1)
            mean = trajectories.mean(0, keepdims=True)
            centered = trajectories - mean
            maximum = min(centered.shape)
            if components < 1:
                raise ValueError("components must be positive")
            components = min(components, maximum)
            _, _, basis = np.linalg.svd(centered, full_matrices=False)
            basis = basis[:components].astype(np.float32)
            coefficients = centered @ basis.T
            payload.update({"mean": mean.astype(np.float32), "basis": basis})
            payload["coefficients"], payload["scale"] = _quantize(
                coefficients, quantization_bits
            )
        manifest = _manifest(sequence, self.id)
        manifest["quantization_bits"] = quantization_bits
        destination.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(
            prefix=f".{destination.name}.", suffix=".tmp",
            dir=destination.parent, delete=False,
        ) as stream:
            temporary = Path(stream.name)
        try:
            with temporary.open("wb") as stream:
                np.savez_compressed(
                    stream,
                    manifest=np.frombuffer(json.dumps(manifest).encode(), dtype=np.uint8),
                    **payload,
                )
            temporary.replace(destination)
        except Exception:
            temporary.unlink(missing_ok=True)
            raise
        return destination

    def decode(self, source: Path, *, device: str | None = None) -> Sequence:
        if device not in (None, "cpu"):
            raise ValueError(f"{self.id} decoding is NumPy-based; device must be 'cpu'")
        try:
            with np.load(source, allow_pickle=False) as artifact:
                manifest = json.loads(artifact["manifest"].tobytes())
                if manifest.get("schema") != self.schema:
                    raise CodecError(f"unsupported {self.id} artifact schema")
                reference, faces = artifact["reference"], artifact["triangles"]
                if self.id == "temporal-delta":
                    positions = reference[None] + artifact["displacement"] * artifact["scale"]
                else:
                    trajectories = (
                        artifact["coefficients"] * artifact["scale"]
                    ) @ artifact["basis"] + artifact["mean"]
                    positions = reference[None] + trajectories.reshape(
                        len(reference), len(manifest["frames"]), 3
                    ).transpose(1, 0, 2)
                frames = [Frame(
                    record["frame_index"], record["timestamp"],
                    TriangleMesh(position, faces), record.get("metadata", {}),
                ) for position, record in zip(positions, manifest["frames"], strict=True)]
        except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
            if isinstance(error, CodecError):
                raise
            raise CodecError(f"invalid {self.id} artifact {source}: {error}") from error
        return Sequence(MemoryFrameProvider(
            frames, metadata=manifest.get("metadata", {}), topology=TopologyMode.FIXED,
            has_constant_vertex_count=True, has_vertex_correspondence=True,
        ))


TEMPORAL_DELTA_CODEC = TemporalMeshCodec("temporal-delta")
TEMPORAL_PCA_CODEC = TemporalMeshCodec("temporal-pca")
