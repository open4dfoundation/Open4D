"""In-process adapter for Open4D's neural TSDF codec."""

from __future__ import annotations

from importlib import import_module
import json
from numbers import Integral
from pathlib import Path
import tempfile
from zipfile import BadZipFile, ZIP_DEFLATED, ZipFile

import numpy as np

from open4d.core import Sequence
from open4d.io import open_sequence

from ._klt import _KLTProvider
from ._npz import _json_value
from ._protocol import CodecError
from ._torch import torch_device
from ._tsdf import write_tsdf_sequence

_SCHEMA = "open4d.n4mc-sequence/v1"


def _backend():
    try:
        return (
            import_module("torch"),
            import_module("open4d.codecs.n4mc.models"),
            import_module("open4d.codecs.n4mc.losses"),
            import_module("open4d.codecs.n4mc.evaluation.metrics"),
        )
    except ImportError as error:
        raise CodecError("N4MC dependencies are missing; install open4d[n4mc]") from error


def _volume(torch, path: Path, device):
    values = np.load(path, allow_pickle=False)["sdf"]
    if values.ndim == 4:
        values = np.moveaxis(values, -1, 0)
    return torch.from_numpy(np.ascontiguousarray(values)).float().to(device)


def _filter_components(mesh, min_component_faces: int | None):
    if min_component_faces is None:
        return mesh
    if (
        not isinstance(min_component_faces, Integral)
        or isinstance(min_component_faces, bool)
        or min_component_faces < 1
    ):
        raise ValueError("min_component_faces must be a positive integer or None")
    kept = [
        part for part in mesh.split(only_watertight=False)
        if len(part.faces) >= min_component_faces
    ]
    if not kept:
        raise CodecError("N4MC component filtering removed all decoded geometry")
    if len(kept) == 1:
        return kept[0]
    return import_module("trimesh").util.concatenate(kept)


class N4MCCodec:
    id = "n4mc"
    suffixes = (".n4d",)
    backend = "python-in-process"
    lossless = False
    preserves = ("positions", "triangles")

    def can_decode(self, source: Path) -> bool:
        try:
            with ZipFile(source) as archive:
                return json.loads(archive.read("manifest.json")).get("schema") == _SCHEMA
        except (OSError, BadZipFile, KeyError, json.JSONDecodeError):
            return False

    def encode(
        self, sequence: Sequence, destination: Path, *, overwrite: bool = False,
        resolution: int = 63, epochs: int = 100, hidden_channels=(24, 48, 96),
        latent_channels: int = 64, learning_rate: float = 1e-4,
        device: str | None = None, seed: int = 7,
    ) -> Path:
        destination = Path(destination).absolute()
        if destination.exists() and not overwrite:
            raise FileExistsError(f"artifact already exists: {destination}")
        for frame in sequence:
            mesh = frame.geometry
            if any((mesh.colors is not None, mesh.normals is not None,
                    mesh.texture_coordinates is not None, bool(mesh.attributes))):
                raise CodecError("N4MC's TSDF profile cannot preserve mesh attributes")
        torch, models, losses, _ = _backend()
        target_device = torch_device(torch, device)
        torch.manual_seed(seed)
        if torch.cuda.is_available():
            torch.cuda.manual_seed_all(seed)
        model_config = {
            "in_channels": 1, "out_channels": 1,
            "hidden_channels": list(hidden_channels),
            "latent_channels": latent_channels, "embed_hwd": None,
            "quantization_mode": "ste", "prior_scale_init": 1.0,
        }
        model = models.TSDFCompressionAutoencoder(**model_config).to(target_device)
        optimizer = torch.optim.AdamW(model.parameters(), lr=learning_rate)
        loss_config = {
            "reconstruction": "l1", "narrow_band_threshold": .1,
            "lambda_rate": 1e-4, "lambda_rec": 1.,
            "lambda_band": 2., "lambda_sign": .2,
        }
        manifest = {
            "schema": _SCHEMA, "codec": self.id,
            "metadata": _json_value(sequence.metadata, "sequence"),
            "allow_nonmonotonic_timestamps": sequence.allow_nonmonotonic_timestamps,
            "frames": [{
                "frame_index": frame.frame_index, "timestamp": frame.timestamp,
                "metadata": _json_value(frame.metadata, f"frame {ordinal}"),
            } for ordinal, frame in enumerate(sequence)],
        }
        destination.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="open4d-n4mc-") as directory:
            work = Path(directory)
            manifest["normalization"] = write_tsdf_sequence(
                sequence, work / "tsdf", resolution=resolution
            )
            volumes = sorted((work / "tsdf").glob("*.npz"))
            model.train()
            for _ in range(epochs):
                for path in volumes:
                    target = _volume(torch, path, target_device).unsqueeze(0)
                    optimizer.zero_grad(set_to_none=True)
                    outputs = model(target)
                    loss, _ = losses.compute_rd_loss(outputs, target, loss_config)
                    loss.backward()
                    optimizer.step()
            model.eval()
            checkpoint = work / "checkpoint.pt"
            torch.save({
                "schema": "open4d.n4mc/v1", "model": model.state_dict(),
                "model_config": model_config,
            }, checkpoint)
            packs = []
            with torch.inference_mode():
                for ordinal, path in enumerate(volumes):
                    encoded = model.encode(_volume(torch, path, target_device).unsqueeze(0))
                    pack = work / f"frame_{ordinal:06d}.npz"
                    np.savez_compressed(
                        pack,
                        quantized_latent=encoded["quantized_latent"][0].cpu().numpy(),
                        original_shape=encoded["original_shape"].cpu().numpy(),
                        bottleneck_shape=encoded["bottleneck_shape"].cpu().numpy(),
                    )
                    packs.append(pack)
            temporary = destination.with_name(f".{destination.name}.tmp")
            try:
                with ZipFile(temporary, "w", compression=ZIP_DEFLATED) as archive:
                    archive.write(checkpoint, checkpoint.name)
                    for pack in packs:
                        archive.write(pack, pack.name)
                    archive.writestr("manifest.json", json.dumps(manifest))
                temporary.replace(destination)
            except Exception:
                temporary.unlink(missing_ok=True)
                raise
        return destination

    def decode(
        self, source: Path, *, device: str | None = None,
        min_component_faces: int | None = None,
    ) -> Sequence:
        torch, models, _, metrics = _backend()
        target_device = torch_device(torch, device)
        temporary = tempfile.TemporaryDirectory(prefix="open4d-n4mc-decode-")
        work = Path(temporary.name)
        try:
            with ZipFile(source) as archive:
                manifest = json.loads(archive.read("manifest.json"))
                if manifest.get("schema") != _SCHEMA:
                    raise CodecError("unsupported N4MC artifact schema")
                archive.extract("checkpoint.pt", work)
                for ordinal in range(len(manifest["frames"])):
                    archive.extract(f"frame_{ordinal:06d}.npz", work)
            checkpoint = torch.load(
                work / "checkpoint.pt", map_location=target_device, weights_only=True
            )
            model = models.TSDFCompressionAutoencoder(
                **checkpoint["model_config"]
            ).to(target_device)
            model.load_state_dict(checkpoint["model"])
            model.eval()
            output = work / "decoded"
            output.mkdir()
            with torch.inference_mode():
                for ordinal in range(len(manifest["frames"])):
                    pack = np.load(work / f"frame_{ordinal:06d}.npz", allow_pickle=False)
                    latent = torch.from_numpy(pack["quantized_latent"]).unsqueeze(0).to(target_device)
                    volume = model.decode_quantized_latent(
                        latent, pack["bottleneck_shape"], pack["original_shape"]
                    )[0]
                    mesh = metrics.reconstruct_mesh_from_tsdf(volume)
                    if mesh is None:
                        raise CodecError(f"N4MC frame {ordinal} has no decoded surface")
                    mesh = _filter_components(mesh, min_component_faces)
                    mesh.export(output / f"frame_{ordinal:06d}.obj")
            decoded = open_sequence(output)
            return Sequence(_KLTProvider(temporary, decoded, manifest))
        except Exception:
            temporary.cleanup()
            raise


N4MC_CODEC = N4MCCodec()
