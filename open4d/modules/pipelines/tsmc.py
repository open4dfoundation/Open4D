"""TSMC adapter — tracked-mesh compression (PCA/KLT + entropy coding).

Orchestrates the real TSMC pipeline stages (the same commands as
``core/tsmc/run.sh``) as timed subprocesses and collects the compressed
bitstream + reconstructed meshes + evaluation metrics.

Input assumption: ARAP volume tracking has already produced per-frame centers
under ``arap-volume-tracking/data/<centers_dir>`` (this is what run.sh Step 1
consumes). ``compress`` runs Steps 1-8 (reference center -> transformation ->
TVMEditor deform -> reference-mesh extraction -> deform-back -> displacements
-> compress -> evaluation) for a single group.
"""
from __future__ import annotations

import glob
import os
import shutil
from typing import List, Optional

from .base import Capability, Codec, CompressionResult, StageRunner

_HERE = os.path.dirname(os.path.abspath(__file__))
_TSMC = os.path.normpath(os.path.join(_HERE, "..", "..", "core", "tsmc"))
_NET = "net5.0"  # run.sh path; retargeted build symlinks net5.0 -> net7.0


def _dotnet_env() -> dict:
    home = os.path.expanduser("~")
    return {
        "DOTNET_ROOT": os.path.join(home, ".dotnet"),
        "PATH": os.path.join(home, ".dotnet") + os.pathsep + os.environ.get("PATH", ""),
    }


class TSMCCodec(Codec):
    name = "tsmc"
    description = "Tracked static-mesh compression (KLT/PCA + GPU Laplacian + entropy)."

    def available(self) -> Capability:
        missing: List[str] = []
        notes: List[str] = []
        if not os.path.isdir(_TSMC):
            missing.append(f"TSMC source ({_TSMC})")
            return Capability(False, missing, notes)
        editor = os.path.join(
            _TSMC, "tvm-editing", "TVMEditor.Test", "bin", "Release", _NET, "TVMEditor.Test"
        )
        if not os.path.exists(editor):
            missing.append(f"TVMEditor.Test binary (build tvm-editing; {editor})")
        if not (shutil.which("dotnet") or os.path.exists(os.path.expanduser("~/.dotnet/dotnet"))):
            missing.append("dotnet runtime")
        try:
            import open3d  # noqa: F401
        except Exception:
            missing.append("open3d (Poisson reconstruction / evaluation)")
        try:
            import constriction  # noqa: F401
        except Exception:
            notes.append("constriction not importable — entropy coding stage may fail")
        return Capability(ok=not missing, missing=missing, notes=notes)

    def _plan(self, dataset, num_frames, num_centers, group_idx, first, last, centers_dir):
        py = "python"
        tag = f"{dataset}_{num_centers}"
        editor = f"TVMEditor.Test/bin/Release/{_NET}/TVMEditor.Test"
        data = f"./TVMEditor.Test/bin/Release/{_NET}/Data/{tag}/"
        out = f"./TVMEditor.Test/bin/Release/{_NET}/output/{tag}/"
        ref_mesh = f"../tvm-editing/{data[2:]}reference_mesh/others/decoded_decimated_reference_mesh.obj"
        disp = f"../tvm-editing/{out[2:]}reference"
        common = ["--dataset", dataset, "--num_frames", str(num_frames), "--num_centers", str(num_centers)]
        gi = ["--group_idx", str(group_idx)]
        idx = ["--firstIndex", str(first), "--lastIndex", str(last)]
        return [
            ("1_reference_center", [py, "./get_reference_center.py", *common,
                "--centers_dir", centers_dir, *gi], "tsmc"),
            ("2_transformation", [py, "./get_transformation.py", *common,
                "--centers_dir", centers_dir, *idx, *gi], "tsmc"),
            ("3_tvmeditor_deform", [editor, dataset, "1", str(first), str(last), data, out], "tvm-editing"),
            ("4_extract_reference_mesh", [py, "./extract_reference_mesh.py", *common,
                "--inputDir", f"../tvm-editing/{out[2:]}output/",
                "--outputDir", f"../tvm-editing/{data[2:]}reference_mesh/",
                *idx, "--key", "4"], "tsmc"),
            ("5_tvmeditor_deformback", [editor, dataset, "2", str(first), str(last), data.rstrip("/"), out.rstrip("/")], "tvm-editing"),
            ("6_displacements", [py, "./get_displacements.py", *common,
                "--target_mesh_path", "../arap-volume-tracking/data/combined_scaled",
                *idx, *gi], "tsmc"),
            ("7_compress_displacements", [py, "compress_displacements.py",
                "--dataset", dataset, "--num_frames", str(num_frames), "--num_eigenvectors", "5",
                "--displacement_path", disp, "--output_path", disp, *idx,
                "--reference_mesh_path", ref_mesh], "tsmc"),
            ("8_evaluation", [py, "evaluation.py", *common,
                "--input_path", disp, "--dynamic_static_path", f"../data/{dataset}/meshes",
                *idx, "--reference_mesh_path", ref_mesh, *gi], "tsmc"),
        ]

    def compress(
        self,
        source,
        *,
        workdir: Optional[str] = None,
        num_frames: int = 10,
        num_centers: int = 2000,
        group_idx: int = 1,
        first_index: int = 0,
        last_index: int = 9,
        centers_dir: str = "../arap-volume-tracking/data/combined-100-max-2000",
        dry_run: bool = False,
    ) -> CompressionResult:
        dataset = source if isinstance(source, str) else getattr(source, "name", None)
        if not dataset:
            raise TypeError("TSMC source must be a dataset name (str)")
        result = CompressionResult(codec=self.name, source=dataset, workdir=workdir or _TSMC)
        plan = self._plan(dataset, num_frames, num_centers, group_idx, first_index, last_index, centers_dir)

        if dry_run:
            from .base import StageTiming
            for name, argv, sub in plan:
                result.stages.append(StageTiming(name, 0.0, True, f"[plan] ({sub}) " + " ".join(argv)))
            return result

        self._require_available()
        runner = StageRunner()
        env = _dotnet_env()
        for name, argv, sub in plan:
            runner.run_cmd(name, argv, cwd=os.path.join(_TSMC, sub), env=env)
        result.stages = runner.stages

        tag = f"{dataset}_{num_centers}"
        outdir = os.path.join(_TSMC, "tvm-editing", "TVMEditor.Test", "bin", "Release", _NET, "output", tag)
        for drc in glob.glob(os.path.join(outdir, "**", "*.drc"), recursive=True):
            result.artifacts["bitstream_" + os.path.basename(drc)] = drc
        for obj in sorted(glob.glob(os.path.join(outdir, "**", "*reconstruct*.obj"), recursive=True)):
            result.artifacts["rec_mesh_" + os.path.basename(obj)] = obj
        result.ok = all(s.ok for s in result.stages)
        return result
