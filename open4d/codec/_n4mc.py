"""In-process adapter for Open4D's neural TSDF codec."""

from __future__ import annotations

from hashlib import sha256
from importlib import import_module
import json
import math
from numbers import Integral, Real
import os
from pathlib import Path
import shutil
import tempfile
import warnings
from zipfile import BadZipFile, ZipFile

import numpy as np

from open4d.core import Sequence
from open4d.io import open_sequence

from ._klt import _KLTProvider, _normalization
from ._metadata import _json_value, _validate_manifest, require_o4d_destination
from ._protocol import CodecError
from ._research import research_module
from ._stages import Stages, checkpoint_destinations as _checkpoint_destinations
from ._stages import validate_stage_hook
from ._torch import torch_device
from ._tsdf import write_tsdf_sequence
from ._o4d_format import _json, contains_codec, pack_o4d, probe_codec, unpack_o4d

_SCHEMA = "open4d.n4mc-sequence/v1"


def _extract_n4d(source: Path, destination: Path) -> dict:
    """Read the legacy adapter's fixed payloads without loading a model."""
    try:
        with ZipFile(source) as archive:
            names = archive.namelist()
            if len(names) != len(set(names)):
                raise CodecError("duplicate N4MC archive member")
            if archive.getinfo("manifest.json").file_size > 16 * 1024 * 1024:
                raise CodecError("oversized N4MC manifest")
            manifest = _json(archive.read("manifest.json"))
            _validate_manifest(manifest, schema=_SCHEMA, codec="n4mc")
            _normalization(manifest)
            if archive.getinfo("checkpoint.pt").file_size > 64 * 1024**2:
                raise CodecError("N4MC migration checkpoint exceeds 64 MiB")
            required = ["checkpoint.pt", *(f"frame_{i:06d}.npz" for i in range(len(manifest["frames"])))]
            if len(required) > 65535 or sum(archive.getinfo(name).file_size for name in required) > 8 * 1024**3:
                raise CodecError("N4MC migration payload budget exceeded")
            for name in required:
                if archive.getinfo(name).file_size == 0:
                    raise CodecError(f"empty N4MC payload: {name}")
                with archive.open(name) as payload, (destination / name).open("xb") as output:
                    shutil.copyfileobj(payload, output)
            return manifest
    except (BadZipFile, KeyError, ValueError, TypeError) as error:
        raise CodecError(f"invalid N4MC archive: {error}") from error


def _write_native_metadata(manifest: dict, destination: Path) -> None:
    metadata = {key: value for key, value in manifest.items() if key != "schema"}
    metadata.update(version=1, native={"profile": "n4mc/1"})
    (destination / "metadata.json").write_text(json.dumps(metadata, allow_nan=False), encoding="utf-8")


def _device(torch, requested):
    target = torch_device(torch, requested)
    if target.type != "mps":
        return target
    # Availability of Metal does not imply availability of the decoder's 3D
    # transposed convolution. Probe before constructing volumes or training.
    try:
        with torch.no_grad():
            value = torch.zeros((1, 1, 1, 1, 1), device=target)
            torch.nn.functional.conv_transpose3d(value, torch.ones_like(value))
    except (RuntimeError, NotImplementedError) as exc:
        if "ConvTranspose 3D is not supported on MPS" not in str(exc):
            raise
        if requested in (None, "auto"):
            warnings.warn("N4MC requires ConvTranspose3D, unsupported by this MPS runtime; using CPU",
                          RuntimeWarning, stacklevel=3)
            return torch.device("cpu")
        raise CodecError(
            "N4MC requires ConvTranspose3D, which this Apple Metal/MPS runtime does not support; "
            "use device='cpu', device='cuda', or device='auto'"
        ) from exc
    return target


def _backend():
    try:
        return (
            import_module("torch"),
            research_module("n4mc.models"),
            research_module("n4mc.losses"),
            research_module("n4mc.evaluation.metrics"),
        )
    except ImportError as error:
        raise CodecError("N4MC dependencies are missing; install open4d[n4mc]") from error


def _volume(torch, path: Path, device):
    with np.load(path, allow_pickle=False) as archive:
        values = archive["sdf"]
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


def _reconstruct_mesh(volume, metrics):
    mesh = metrics.reconstruct_mesh_from_tsdf(volume)
    if mesh is not None:
        # The research helper returns [-1, 1]; our TSDF samples [-1.1, 1.1].
        mesh.vertices *= 1.1
    return mesh


def _fingerprint(volumes, model_config, loss_config, learning_rate, seed) -> str:
    """Identify training inputs so resumed state never mixes runs."""
    digest = sha256(json.dumps({
        "model": model_config, "loss": loss_config,
        "learning_rate": float(learning_rate), "seed": seed,
    }, sort_keys=True).encode())
    for path in volumes:
        with np.load(path, allow_pickle=False) as archive:
            values = np.ascontiguousarray(archive["sdf"])
        digest.update(str(values.shape).encode())
        digest.update(values.tobytes())
    return digest.hexdigest()


class _TrainingState:
    """Resumable optimizer, model and RNG state kept outside the artifact."""

    def __init__(self, torch, directory, fingerprint, model, optimizer) -> None:
        self.torch, self.fingerprint = torch, fingerprint
        self.directory = None if directory is None else Path(directory).absolute()
        self.model, self.optimizer = model, optimizer

    def save(self, epoch: int) -> None:
        torch = self.torch
        self.directory.mkdir(parents=True, exist_ok=True)
        path = self.directory / f"epoch_{epoch:06d}.pt"
        staged = path.with_suffix(".partial")
        torch.save({
            "fingerprint": self.fingerprint, "epoch": epoch,
            "model": self.model.state_dict(), "optimizer": self.optimizer.state_dict(),
            "rng": torch.get_rng_state(),
            "cuda_rng": torch.cuda.get_rng_state_all() if torch.cuda.is_available() else [],
        }, staged)
        os.replace(staged, path)

    def restore(self, epochs: int) -> int:
        """Load the newest compatible state at or before ``epochs``."""
        torch = self.torch
        saved = sorted((int(path.stem[6:]), path) for path in self.directory.glob("epoch_*.pt")
                       if path.stem[6:].isdigit()) if self.directory.is_dir() else []
        for epoch, path in reversed(saved):
            if epoch > epochs:
                continue
            state = torch.load(path, map_location="cpu", weights_only=True)
            if state.get("fingerprint") != self.fingerprint or state.get("epoch") != epoch:
                raise CodecError(f"N4MC checkpoint {path} belongs to other data or settings")
            self.model.load_state_dict(state["model"])
            self.optimizer.load_state_dict(state["optimizer"])
            torch.set_rng_state(state["rng"])
            if torch.cuda.is_available() and state["cuda_rng"]:
                torch.cuda.set_rng_state_all(state["cuda_rng"])
            return epoch
        return 0


class N4MCCodec:
    id = "n4mc"
    suffixes = (".o4d",)
    backend = "python-in-process"
    lossless = False
    preserves = ("positions", "triangles")

    def can_decode(self, source: Path) -> bool:
        return contains_codec(source, self.id)

    def encode(
        self, sequence: Sequence, destination: Path, *, overwrite: bool = False,
        resolution: int = 63, epochs: int = 100, hidden_channels=(24, 48, 96),
        latent_channels: int = 64, learning_rate: float = 1e-4,
        lambda_rate: float = 1e-4, device: str | None = None, seed: int = 7,
        checkpoint_dir: Path | None = None, checkpoint_every: int | None = None,
        resume: bool = False, checkpoint_artifacts=None, stage_hook=None,
    ) -> Path:
        """Train the shared TSDF autoencoder and write its quantized latents.

        ``lambda_rate`` weights the learned entropy-model rate term in the loss.
        ``checkpoint_dir`` keeps resumable training state at each
        ``checkpoint_every`` epochs (and at the final and exported epochs);
        ``resume=True`` continues from the newest compatible state not beyond
        ``epochs``. A saved state at exactly ``epochs`` is exported without more
        training, which selects any saved checkpoint. ``checkpoint_artifacts``
        maps intermediate epochs to extra ``.o4d`` destinations from the same run.
        ``stage_hook`` receives instrumentation events (see ``_stages``),
        including each frame's learned entropy estimate in bits.
        """
        destination = require_o4d_destination(destination)
        if destination.exists() and not overwrite:
            raise FileExistsError(f"artifact already exists: {destination}")
        if type(epochs) is not int or epochs < 1:
            raise ValueError("epochs must be a positive integer")
        if (isinstance(lambda_rate, bool) or not isinstance(lambda_rate, Real)
                or not math.isfinite(lambda_rate) or lambda_rate < 0):
            raise ValueError("lambda_rate must be a finite nonnegative number")
        if checkpoint_every is not None and (type(checkpoint_every) is not int or checkpoint_every < 1):
            raise ValueError("checkpoint_every must be a positive integer or None")
        if not isinstance(resume, bool):
            raise TypeError("resume must be bool")
        if (resume or checkpoint_every is not None) and checkpoint_dir is None:
            raise ValueError("resume and checkpoint_every need checkpoint_dir")
        validate_stage_hook(stage_hook)
        checkpoints = _checkpoint_destinations(checkpoint_artifacts, epochs, destination, overwrite)
        for frame in sequence:
            mesh = frame.geometry
            if any((mesh.colors is not None, mesh.normals is not None,
                    mesh.texture_coordinates is not None, bool(mesh.attributes))):
                raise CodecError("N4MC's TSDF profile cannot preserve mesh attributes")
        torch, models, losses, metrics = _backend()
        target_device = _device(torch, device)
        stages = Stages(stage_hook, torch, target_device)
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
            "lambda_rate": float(lambda_rate), "lambda_rec": 1.,
            "lambda_band": 2., "lambda_sign": .2,
        }
        manifest = {
            "version": 1, "codec": self.id,
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
            with stages.phase("preprocessing"):
                manifest["normalization"] = write_tsdf_sequence(
                    sequence, work / "tsdf", resolution=resolution
                )
                volumes = sorted((work / "tsdf").glob("*.npz"))
            center, scale = _normalization(manifest)
            if stages.enabled:
                for ordinal, path in enumerate(volumes):
                    mesh = _reconstruct_mesh(_volume(torch, path, "cpu"), metrics)
                    if mesh is not None:
                        stages.mesh("target", ordinal, mesh.vertices / scale + center, mesh.faces)
            fingerprint = _fingerprint(volumes, model_config, loss_config, learning_rate, seed)
            state = _TrainingState(torch, checkpoint_dir, fingerprint, model, optimizer)
            start = state.restore(epochs) if resume else 0
            exports = {epoch: path for epoch, path in checkpoints.items() if epoch > start}
            exports[epochs] = destination

            def export(epoch, artifact):
                model.eval()
                native = work / f"native-{epoch}"
                native.mkdir()
                with stages.phase("export"):
                    torch.save({
                        "model": model.state_dict(),
                        "model_config": model_config,
                    }, native / "checkpoint.pt")
                    with torch.inference_mode():
                        estimates = []
                        for ordinal, path in enumerate(volumes):
                            encoded = model.encode(_volume(torch, path, target_device).unsqueeze(0))
                            quantized = encoded["quantized_latent"]
                            estimates.append(float(model.entropy_model(quantized).sum()))
                            np.savez_compressed(
                                native / f"frame_{ordinal:06d}.npz",
                                quantized_latent=quantized[0].cpu().numpy(),
                                original_shape=encoded["original_shape"].cpu().numpy(),
                                bottleneck_shape=encoded["bottleneck_shape"].cpu().numpy(),
                            )
                label = "trained" if epoch == epochs else f"trained@{epoch}"
                for ordinal, bits in enumerate(estimates):
                    stages.rate(ordinal, {"stage": label, "learned_entropy_bits": bits})
                if stages.enabled:
                    with torch.inference_mode():
                        for ordinal, path in enumerate(volumes):
                            volume = model(_volume(torch, path, target_device).unsqueeze(0))
                            mesh = _reconstruct_mesh(volume["reconstruction"][0], metrics)
                            if mesh is not None:
                                stages.mesh(label, ordinal, mesh.vertices / scale + center, mesh.faces)
                with stages.phase("packing"):
                    _write_native_metadata(manifest, native)
                    pack_o4d(native, artifact, overwrite=overwrite)
                    shutil.rmtree(native)
                model.train()

            model.train()
            for epoch in range(start + 1, epochs + 1):
                with stages.phase("training"):
                    for path in volumes:
                        target = _volume(torch, path, target_device).unsqueeze(0)
                        optimizer.zero_grad(set_to_none=True)
                        outputs = model(target)
                        loss, _ = losses.compute_rd_loss(outputs, target, loss_config)
                        loss.backward()
                        optimizer.step()
                if checkpoint_dir is not None and (
                        epoch in exports or (checkpoint_every and epoch % checkpoint_every == 0)):
                    state.save(epoch)
                if epoch in exports and epoch != epochs:
                    export(epoch, exports[epoch])
            export(epochs, destination)
            return destination

    def decode(
        self, source: Path, *, device: str | None = None,
        min_component_faces: int | None = None,
    ) -> Sequence:
        source = Path(source).absolute()
        temporary = tempfile.TemporaryDirectory(prefix="open4d-n4mc-decode-")
        work = Path(temporary.name)
        decoded = None
        try:
            if source.suffix.lower() != ".o4d" or probe_codec(source) != self.id:
                raise CodecError("N4MC decode requires its O4D profile; use migrate_legacy for older artifacts")
            native = unpack_o4d(source, work / "native")
            manifest = json.loads((native / "metadata.json").read_text())
            _validate_manifest(manifest, schema=None, codec=self.id)
            _normalization(manifest)
            torch, models, _, metrics = _backend()
            target_device = _device(torch, device)
            checkpoint = torch.load(
                native / "checkpoint.pt", map_location=target_device, weights_only=True
            )
            model = models.TSDFCompressionAutoencoder(
                **checkpoint["model_config"]
            ).to(target_device)
            model.load_state_dict(checkpoint["model"])
            model.eval()
            output = work / "decoded"
            output.mkdir()
            with torch.inference_mode(), torch.backends.cudnn.flags(
                enabled=torch.backends.cudnn.enabled, benchmark=False,
                deterministic=True, allow_tf32=torch.backends.cudnn.allow_tf32,
            ):
                for ordinal in range(len(manifest["frames"])):
                    with np.load(native / f"frame_{ordinal:06d}.npz", allow_pickle=False) as pack:
                        latent = torch.from_numpy(pack["quantized_latent"]).unsqueeze(0).to(target_device)
                        volume = model.decode_quantized_latent(
                            latent, pack["bottleneck_shape"], pack["original_shape"]
                        )[0]
                    mesh = _reconstruct_mesh(volume, metrics)
                    if mesh is None:
                        raise CodecError(f"N4MC frame {ordinal} has no decoded surface")
                    mesh = _filter_components(mesh, min_component_faces)
                    mesh.export(output / f"frame_{ordinal:06d}.obj")
            decoded = open_sequence(output)
            return Sequence(_KLTProvider(temporary, decoded, manifest))
        except BaseException:
            try:
                if decoded is not None:
                    decoded.close()
            finally:
                temporary.cleanup()
            raise


N4MC_CODEC = N4MCCodec()
