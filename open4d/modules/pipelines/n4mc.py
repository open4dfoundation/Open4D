"""N4MC adapter — neural auto-decoder mesh compression.

Wraps the real ``train_quant.py`` auto-decoder. ``compress`` takes an Open4D
:class:`~open4d.core.MeshSequence` (or a directory of ``.obj`` frames, or an
``.o4d`` container), voxelizes each frame to the TSDF ``.npz`` layout the
network consumes, trains the quantized generator, and collects the compressed
weights + per-frame latent codes + reconstructed meshes.

Caveats (unchanged from the standalone pipeline):
- The ``optimize_tsdf_offset`` sub-voxel *offset* refinement needs
  ``nvdiffrast`` (+ a CUDA compiler) and a ``render`` module absent from the
  repo, so the offset field is zero-initialized here. The SDF field itself is
  the network's real signed-distance init.
- ``train_quant.py`` top-level ``import kaolin`` / ``import py7zr`` are only
  used by code paths this run never touches; shipped stubs satisfy them.
"""
from __future__ import annotations

import glob
import os
import sys
from typing import List, Optional

import numpy as np

from .base import Capability, Codec, CompressionResult, StageRunner

# repo-relative locations
_HERE = os.path.dirname(os.path.abspath(__file__))
_STUBS = os.path.join(_HERE, "_n4mc_stubs")
# open4d/modules/pipelines/ -> open4d/core/N4MC/...
_CORE = os.path.normpath(os.path.join(_HERE, "..", "..", "core", "N4MC"))
_N4MC_SRC = os.path.join(_CORE, "n4mc_source")
_DEFAULT_CONFIG = os.path.join(_CORE, "configs", "configs_128.txt")


def _load_source_sequence(source):
    """Coerce a MeshSequence | dir-of-obj | .o4d path into a MeshSequence."""
    from open4d.core import MeshSequence

    if isinstance(source, MeshSequence):
        return source
    if isinstance(source, str) and source.endswith(".o4d"):
        return MeshSequence.from_o4d(source)
    if isinstance(source, str) and os.path.isdir(source):
        import trimesh

        files = sorted(
            glob.glob(os.path.join(source, "*.obj"))
            + glob.glob(os.path.join(source, "*.ply"))
        )
        if not files:
            raise ValueError(f"no .obj/.ply frames found in {source}")
        seq = MeshSequence(name=source)
        for f in files:
            m = trimesh.load_mesh(f, process=False)
            seq.append(m.vertices, m.faces, timestamp=float(len(seq)))
        return seq
    raise TypeError(
        "source must be a MeshSequence, a directory of .obj/.ply frames, "
        f"or a path to an .o4d container; got {type(source).__name__}"
    )


def _sequence_normalizer(seq, target_halfextent: float = 1.8):
    """Return a function mapping frame vertices into the voxel grid range.

    Uses the sequence-wide bounding box so inter-frame motion is preserved.
    The FlexiCubes grid spans roughly [-2, 2] after the x2 scale, so meshes are
    centered and scaled to fit within +/- ``target_halfextent``.
    """
    lo = np.array([np.inf, np.inf, np.inf], dtype=np.float64)
    hi = -lo
    for frame in seq:
        v = frame.vertices
        lo = np.minimum(lo, v.min(axis=0))
        hi = np.maximum(hi, v.max(axis=0))
    center = (lo + hi) / 2.0
    half = float(np.max((hi - lo) / 2.0))
    scale = target_halfextent / half if half > 0 else 1.0

    def transform(v: np.ndarray) -> np.ndarray:
        return ((v.astype(np.float64) - center) * scale).astype(np.float32)

    return transform


class N4MCCodec(Codec):
    name = "n4mc"
    description = "Neural auto-decoder mesh compression (quantized generator)."

    def available(self) -> Capability:
        missing: List[str] = []
        notes: List[str] = []
        try:
            import torch

            if torch.cuda.is_available():
                notes.append(f"CUDA available ({torch.cuda.device_count()} GPU)")
            else:
                notes.append("no CUDA — will run on CPU (slow)")
        except Exception:
            missing.append("torch")
        try:
            import point_cloud_utils  # noqa: F401
        except Exception:
            missing.append("point_cloud_utils (voxelization)")
        try:
            import trimesh  # noqa: F401
        except Exception:
            missing.append("trimesh")
        if not os.path.isdir(_N4MC_SRC):
            missing.append(f"N4MC source ({_N4MC_SRC})")
        if not os.path.isfile(_DEFAULT_CONFIG):
            notes.append(f"default config not found at {_DEFAULT_CONFIG}")
        notes.append(
            "offset field is zero-init (optimize_tsdf_offset needs nvdiffrast)"
        )
        return Capability(ok=not missing, missing=missing, notes=notes)

    # ---- voxelization stage -------------------------------------------- #
    def _voxelize(self, seq, out_dir: str, res: int, normalize: bool) -> int:
        import point_cloud_utils as pcu

        sys.path.insert(0, _N4MC_SRC)
        try:
            from fmc import construct_voxel_grid  # real N4MC grid builder
        finally:
            pass
        try:
            x_nx3, _ = construct_voxel_grid(res, "cpu")
            x = x_nx3.cpu().numpy().astype(np.float32) * 2.0
        finally:
            sys.path.remove(_N4MC_SRC)

        n = res + 1
        data_dir = os.path.join(out_dir, "data")
        os.makedirs(data_dir, exist_ok=True)
        transform = _sequence_normalizer(seq) if normalize else (lambda v: v.astype(np.float32))

        for i, frame in enumerate(seq):
            v = transform(frame.vertices)
            faces = np.asarray(frame.faces, dtype=np.int32)
            sdf, _, _ = pcu.signed_distance_to_mesh(x, v, faces)
            if sdf[-1] < 0:  # opt_fmc sign convention
                sdf = -sdf
            sdf = np.clip(sdf / (2 * 2 / res), -1, 1).astype(np.float32)
            sdf = sdf.reshape(n, n, n, 1)
            offset = np.zeros((n, n, n, 3), dtype=np.float32)
            np.savez_compressed(os.path.join(data_dir, "%04d.npz" % i), sdf=sdf, offset=offset)
        return len(seq)

    # ---- artifact collection ------------------------------------------- #
    @staticmethod
    def _collect(log_path: str, result: CompressionResult) -> None:
        # newest checkpoint dir (train_quant timestamps its run dir)
        run_dirs = [d for d in glob.glob(os.path.join(log_path, "*")) if os.path.isdir(d)]
        if not run_dirs:
            return
        run_dir = max(run_dirs, key=os.path.getmtime)
        result.artifacts["run_dir"] = run_dir
        # weights (top-level or newest checkpoint_*)
        for name in ("encoder_compressed.pt", "decoder_compressed.pt"):
            hits = sorted(glob.glob(os.path.join(run_dir, "**", name), recursive=True))
            if hits:
                result.artifacts[name] = hits[-1]
        for npy in sorted(glob.glob(os.path.join(run_dir, "**", "embed_feature_*.npy"), recursive=True)):
            result.artifacts["code_" + os.path.basename(npy)] = npy
        for obj in sorted(glob.glob(os.path.join(run_dir, "**", "rec_mesh_*.obj"), recursive=True)):
            result.artifacts["rec_mesh_" + os.path.basename(obj)] = obj

    @staticmethod
    def _parse_metrics(stdout: str, result: CompressionResult) -> None:
        # best-effort: last "loss ... <float>" style line
        last_loss = None
        for line in stdout.splitlines():
            low = line.lower()
            if "loss" in low:
                for tok in low.replace("=", " ").replace(":", " ").split():
                    try:
                        last_loss = float(tok)
                    except ValueError:
                        continue
        if last_loss is not None:
            result.metrics["final_loss"] = last_loss

    def compress(
        self,
        source,
        *,
        workdir: Optional[str] = None,
        config: Optional[str] = None,
        n_epoch: int = 100,
        voxel_grid_res: int = 127,
        embed_hwd: int = 8,
        ssim_weight: float = 10.0,
        important_weight: float = 5.0,
        offset_weight: float = 0.0,
        normalize: bool = True,
        num_frames: Optional[int] = None,
        python_exe: Optional[str] = None,
        extra_args: Optional[List[str]] = None,
    ) -> CompressionResult:
        self._require_available()
        workdir = workdir or os.path.join(os.getcwd(), "n4mc_run")
        config = config or _DEFAULT_CONFIG
        python_exe = python_exe or sys.executable
        os.makedirs(workdir, exist_ok=True)

        seq = _load_source_sequence(source)
        if num_frames:
            seq = seq[:num_frames]
        if len(seq) == 0:
            raise ValueError("source sequence is empty")

        runner = StageRunner()
        result = CompressionResult(
            codec=self.name,
            source=getattr(seq, "name", None) or str(source),
            workdir=workdir,
        )

        tsdf_dir = os.path.join(workdir, "tsdf")
        n = runner.run_py(
            "voxelize (mesh -> TSDF)", self._voxelize, seq, tsdf_dir, voxel_grid_res, normalize
        )

        log_path = os.path.join(workdir, "log")
        env = {"PYTHONPATH": os.pathsep.join([_STUBS, _N4MC_SRC, env_get("PYTHONPATH")])}
        argv = [
            python_exe, "-u", "train_quant.py",
            "--config_path", config,
            "--data_path", tsdf_dir,
            "--num_frames", str(n),
            "--n_epoch", str(n_epoch),
            "--log_path", log_path,
            "--voxel_grid_res", str(voxel_grid_res),
            "--embed_hwd", str(embed_hwd),
            "--ssim_weight", str(ssim_weight),
            "--important_weight", str(important_weight),
            "--offset_weight", str(offset_weight),
        ] + (extra_args or [])
        proc = runner.run_cmd("train (QuantGeneratorV2)", argv, cwd=_N4MC_SRC, env=env)

        runner.run_py("collect artifacts", self._collect, log_path, result)
        self._parse_metrics(proc.stdout, result)

        result.stages = runner.stages
        result.metrics["num_frames"] = float(n)
        result.ok = all(s.ok for s in result.stages)
        return result


def env_get(key: str) -> str:
    return os.environ.get(key, "")
