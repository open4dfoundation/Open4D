"""TVMC adapter — time-varying mesh compression (ARAP tracking + Draco).

Orchestrates the real TVMC pipeline (the same commands as
``core/tvmc/run_pipeline.sh``) as timed subprocesses: ARAP volume tracking ->
reference centers -> transformations -> TVMEditor deform -> reference-mesh
extraction -> deform-back -> displacements -> Draco encode/evaluate.

Unlike TSMC, TVMC includes the ARAP tracking step itself (it builds and runs
the C# ``Client`` on a per-dataset ``config-*.xml``), so ``source`` is a
dataset name and ``arap_config`` selects the tracking config.
"""
from __future__ import annotations

import glob
import os
import shutil
from typing import List, Optional

from .base import Capability, Codec, CompressionResult, StageRunner

_HERE = os.path.dirname(os.path.abspath(__file__))
_TVMC = os.path.normpath(os.path.join(_HERE, "..", "..", "core", "tvmc"))
_NET = "net5.0"  # retargeted build symlinks net5.0 -> net7.0


def _dotnet_env() -> dict:
    home = os.path.expanduser("~")
    dotnet = os.path.join(home, ".dotnet")
    return {"DOTNET_ROOT": dotnet, "PATH": dotnet + os.pathsep + os.environ.get("PATH", "")}


class TVMCCodec(Codec):
    name = "tvmc"
    description = "Time-varying mesh compression (ARAP tracking + Draco encode)."

    def available(self) -> Capability:
        missing: List[str] = []
        notes: List[str] = []
        if not os.path.isdir(_TVMC):
            return Capability(False, [f"TVMC source ({_TVMC})"], notes)
        if not (shutil.which("dotnet") or os.path.exists(os.path.expanduser("~/.dotnet/dotnet"))):
            missing.append("dotnet runtime")
        editor = os.path.join(_TVMC, "tvm-editing", "TVMEditor.Test", "bin", "Release", _NET, "TVMEditor.Test")
        if not os.path.exists(editor):
            notes.append(f"TVMEditor.Test not built yet ({editor}) — will need `dotnet build`")
        for tool in ("draco_encoder", "draco_decoder"):
            if not os.path.exists(os.path.join(_TVMC, "draco", "build", tool)):
                missing.append(f"draco {tool} (build core/tvmc/draco)")
        try:
            import open3d  # noqa: F401
        except Exception:
            missing.append("open3d")
        return Capability(ok=not missing, missing=missing, notes=notes)

    def _plan(self, dataset, arap_config, num_frames, num_centers, first, last, centers_dir):
        py = "python"
        tag = f"{dataset}_{num_centers}"
        editor = f"TVMEditor.Test/bin/Release/{_NET}/TVMEditor.Test"
        data = f"./TVMEditor.Test/bin/Release/{_NET}/Data/{tag}/"
        out = f"./TVMEditor.Test/bin/Release/{_NET}/output/{tag}/"
        common = ["--dataset", dataset, "--num_frames", str(num_frames), "--num_centers", str(num_centers)]
        idx = ["--firstIndex", str(first), "--lastIndex", str(last)]
        return [
            ("1a_arap_build", ["dotnet", "build", "-c", "release"], "arap-volume-tracking"),
            ("1b_arap_track", ["dotnet", "./bin/Client.dll", arap_config], "arap-volume-tracking"),
            ("2_reference_center", [py, "./get_reference_center.py", *common,
                "--centers_dir", centers_dir], "TVMC"),
            ("3_transformation", [py, "./get_transformation.py", *common,
                "--centers_dir", centers_dir, *idx], "TVMC"),
            ("4_tvmeditor_deform", [editor, dataset, "1", str(first), str(last), data, out], "tvm-editing"),
            ("5_extract_reference_mesh", [py, "./extract_reference_mesh.py", *common,
                "--inputDir", f"../tvm-editing/{out[2:]}output/",
                "--outputDir", f"../tvm-editing/{data[2:]}reference_mesh/",
                *idx, "--key", "4"], "TVMC"),
            ("6_tvmeditor_deformback", [editor, dataset, "2", str(first), str(last), data.rstrip("/"), out], "tvm-editing"),
            ("7_displacements", [py, "./get_displacements.py", *common,
                "--target_mesh_path", f"../arap-volume-tracking/data/{dataset}", *idx], "TVMC"),
            ("8_evaluation_draco", [py, "./evaluation.py", *common, *idx,
                "--fileNamePrefix", f"{dataset}_fr0",
                "--encoderPath", "../draco/build/draco_encoder",
                "--decoderPath", "../draco/build/draco_decoder",
                "--qp", "10", "--outputPath", f"./{dataset}_outputs"], "TVMC"),
        ]

    def compress(
        self,
        source,
        *,
        workdir: Optional[str] = None,
        arap_config: Optional[str] = None,
        num_frames: int = 10,
        num_centers: int = 2000,
        first_index: int = 0,
        last_index: int = 9,
        centers_dir: Optional[str] = None,
        dry_run: bool = False,
    ) -> CompressionResult:
        dataset = source if isinstance(source, str) else getattr(source, "name", None)
        if not dataset:
            raise TypeError("TVMC source must be a dataset name (str)")
        arap_config = arap_config or f"./config/max/config-{dataset}-max.xml"
        centers_dir = centers_dir or f"../arap-volume-tracking/data/{dataset}-output-max-2000/"
        result = CompressionResult(codec=self.name, source=dataset, workdir=workdir or _TVMC)
        plan = self._plan(dataset, arap_config, num_frames, num_centers, first_index, last_index, centers_dir)

        if dry_run:
            from .base import StageTiming
            for name, argv, sub in plan:
                result.stages.append(StageTiming(name, 0.0, True, f"[plan] ({sub}) " + " ".join(argv)))
            return result

        self._require_available()
        runner = StageRunner()
        env = _dotnet_env()
        for name, argv, sub in plan:
            runner.run_cmd(name, argv, cwd=os.path.join(_TVMC, sub), env=env)
        result.stages = runner.stages

        outdir = os.path.join(_TVMC, "TVMC", f"{dataset}_outputs")
        for drc in glob.glob(os.path.join(outdir, "**", "*.drc"), recursive=True):
            result.artifacts["bitstream_" + os.path.basename(drc)] = drc
        result.ok = all(s.ok for s in result.stages)
        return result
