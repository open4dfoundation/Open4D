from __future__ import annotations

import argparse
import fcntl
import json
import os
import re
import shutil
import subprocess
import sys
import time
import traceback
from contextlib import contextmanager
from pathlib import Path
from typing import Any, Callable

import numpy as np
import trimesh

from job_control import RUNS_ROOT, atomic_json, load_status, utc_now


OPEN4D = Path(os.environ.get("OPEN4D_ROOT", str(Path(__file__).resolve().parent.parent)))
MODULES = OPEN4D / "open4d/modules"
N4MC = next((path for path in (MODULES / "N4MC", MODULES / "n4mc") if path.exists()), MODULES / "N4MC")
QNDF = MODULES / "Quantized-Neural-Displacement-Fields"
TVMC = MODULES / "tvmc"
TSMC = MODULES / "tsmc"
CONDA = Path(os.environ.get("CONDA_EXE") or shutil.which("conda") or "conda")
DOTNET_ROOT = str(Path.home() / ".dotnet")
os.environ.setdefault("DOTNET_ROOT", DOTNET_ROOT)
if DOTNET_ROOT not in os.environ.get("PATH", "").split(os.pathsep):
    os.environ["PATH"] = DOTNET_ROOT + os.pathsep + os.environ.get("PATH", "")


class JobFailure(RuntimeError):
    pass


def conda(environment: str, *command: str) -> list[str]:
    return [str(CONDA), "run", "--no-capture-output", "-n", environment, *map(str, command)]


class Runner:
    def __init__(self, job_id: str):
        self.job_id = job_id
        self.job_dir = RUNS_ROOT / job_id
        self.status_path = self.job_dir / "status.json"
        self.status = load_status(job_id)
        self.slug = "bench_" + re.sub(r"[^a-z0-9]", "", job_id.lower())[-16:]
        self.settings = self.status["settings"]
        self.results = self.job_dir / "results"
        self.results.mkdir(exist_ok=True)
        self.transform: dict[str, Any] = {}

    def save(self) -> None:
        self.status["updated_at"] = utc_now()
        atomic_json(self.status_path, self.status)

    def log(self, method: str, message: str) -> None:
        path = self.job_dir / f"{method.lower()}.log"
        with path.open("a", encoding="utf-8") as handle:
            handle.write(f"[{utc_now()}] {message}\n")

    def command(self, method: str, command: list[str], cwd: Path, env: dict[str, str] | None = None) -> None:
        pretty = " ".join(map(str, command))
        self.log(method, f"$ (cd {cwd} && {pretty})")
        with (self.job_dir / f"{method.lower()}.log").open("a", encoding="utf-8") as handle:
            result = subprocess.run(
                command,
                cwd=cwd,
                env={**os.environ, **(env or {})},
                stdout=handle,
                stderr=subprocess.STDOUT,
                text=True,
            )
        if result.returncode:
            raise JobFailure(f"Command exited with status {result.returncode}; see {method.lower()}.log")

    def prepare(self) -> None:
        source = self.job_dir / "input.obj"
        loaded = trimesh.load(source, force="mesh", process=False)
        if not isinstance(loaded, trimesh.Trimesh) or len(loaded.vertices) < 4 or len(loaded.faces) < 4:
            raise JobFailure("The upload is not a usable triangle OBJ mesh")
        vertices = np.asarray(loaded.vertices, dtype=np.float64)
        center = (vertices.min(axis=0) + vertices.max(axis=0)) / 2
        extent = float(np.ptp(vertices, axis=0).max())
        if not np.isfinite(extent) or extent <= 0:
            raise JobFailure("The uploaded mesh has an invalid or zero-size bounding box")
        scale = 1.6 / extent
        normalized = loaded.copy()
        normalized.vertices = (vertices - center) * scale
        normalized_path = self.job_dir / "normalized.obj"
        normalized.export(normalized_path)
        normalized_vertices = np.asarray(normalized.vertices)
        qndf_min = normalized_vertices.min(axis=0)
        # QNDF compress.py applies: ov -= ov.min(dim=0); ov /= ov.max().
        # Keep those exact operands so its reconstruction can be mapped back.
        qndf_scale = float((normalized_vertices - qndf_min).max())
        self.transform = {
            "center": center.tolist(),
            "scale": scale,
            "qndf_scale": qndf_scale,
            "qndf_min": qndf_min.tolist(),
            "vertices": int(len(normalized.vertices)),
            "faces": int(len(normalized.faces)),
        }
        atomic_json(self.job_dir / "transform.json", self.transform)
        self.status["normalized_input"] = "normalized.obj"
        self.status["input_summary"] = {"vertices": len(normalized.vertices), "faces": len(normalized.faces)}
        self.save()

    def restore(
        self,
        source: Path,
        destination: Path,
        qndf_coordinates: bool = False,
        coordinate_scale: float = 1.0,
    ) -> None:
        mesh = trimesh.load(source, force="mesh", process=False)
        vertices = np.asarray(mesh.vertices, dtype=np.float64)
        if qndf_coordinates:
            vertices = vertices * self.transform["qndf_scale"] + np.asarray(self.transform["qndf_min"])
        vertices *= coordinate_scale
        vertices = vertices / self.transform["scale"] + np.asarray(self.transform["center"])
        mesh.vertices = vertices
        destination.parent.mkdir(parents=True, exist_ok=True)
        mesh.export(destination)

    def duplicate_obj(self, destination: Path, names: list[str]) -> None:
        destination.mkdir(parents=True, exist_ok=True)
        for name in names:
            shutil.copy2(self.job_dir / "normalized.obj", destination / name)

    def execute_method(self, method: str, callback: Callable[[], dict[str, Any]]) -> None:
        record = self.status["methods"][method]
        record.update(state="running", started_at=utc_now(), error=None)
        self.save()
        started = time.monotonic()
        try:
            details = callback()
            record.update(details)
            record["state"] = "completed"
        except Exception as error:
            record["state"] = "failed"
            record["error"] = str(error)
            self.log(method, traceback.format_exc())
        record["elapsed_seconds"] = round(time.monotonic() - started, 3)
        record["finished_at"] = utc_now()
        self.save()

    def run_n4mc(self) -> dict[str, Any]:
        epochs = int(self.settings.get("n4mc_epochs", 300))
        input_dir = self.job_dir / "n4mc_input"
        self.duplicate_obj(input_dir, ["frame_0000.obj"])
        dataset_dir = self.job_dir / "n4mc_dataset"
        self.command(
            "N4MC",
            conda("pytorch", "python", "optimize_tsdf_offset.py", "--data_path", input_dir, "--save_path", dataset_dir,
                  "--num_frames", "1", "--voxel_grid_res", str(self.settings.get("n4mc_resolution", 127)),
                  "--niter", str(self.settings.get("n4mc_preprocess_iterations", 500))),
            N4MC,
        )
        for index in range(1, 10):
            for relative in (Path("data") / "0000.npz", Path("data/TSDF") / "0000.npz"):
                target = relative.parent / f"{index:04}.npz"
                shutil.copy2(dataset_dir / relative, dataset_dir / target)
        output_root = self.job_dir / "n4mc_training"
        experiment = self.slug + "_n4mc"
        self.command(
            "N4MC",
            conda("pytorch", "python", "-m", "training.train", "--config", "configs/train_tsdf.yaml",
                  "--set", f"experiment.name={experiment}", "--set", f"experiment.output_root={output_root}",
                  "--set", f"data.root={dataset_dir}", "--set", "data.pattern=data/TSDF/*.npz",
                  "--set", "data.batch_size=1", "--set", f"training.epochs={epochs}",
                  "--set", "training.validate_every=10", "--set", "training.device=cuda:0"),
            N4MC,
        )
        candidates = sorted(output_root.glob(f"{experiment}_*"), key=lambda path: path.stat().st_mtime)
        if not candidates:
            raise JobFailure("N4MC training completed without producing a run directory")
        run_dir = candidates[-1]
        reconstruction = self.job_dir / "n4mc_reconstruction"
        self.command(
            "N4MC",
            conda("pytorch", "python", "-m", "evaluation.reconstruct", "--config", run_dir / "config.yaml",
                  "--checkpoint", run_dir / "best.pt", "--split", "test", "--output-dir", reconstruction),
            N4MC,
        )
        outputs = sorted(reconstruction.glob("*.ply"))
        if not outputs:
            raise JobFailure("N4MC reconstruction produced no PLY mesh")
        final = self.results / "n4mc.obj"
        self.restore(outputs[0], final)
        summary_path = reconstruction / "summary.json"
        summary = json.loads(summary_path.read_text()) if summary_path.exists() else {}
        return {"output": str(final.relative_to(self.job_dir)), "native_metrics": summary, "variant": f"trained {epochs} epochs"}

    def run_qndf(self) -> dict[str, Any]:
        epochs = int(self.settings.get("qndf_epochs", 300))
        coarse = int(self.settings.get("qndf_coarse_size", 3000))
        subdiv = int(self.settings.get("qndf_subdivisions", 2))
        source_link = QNDF / "objs_original" / f"{self.slug}.obj"
        experiment_dir = QNDF / "experiments" / self.slug
        source_link.parent.mkdir(exist_ok=True)
        source_link.symlink_to(self.job_dir / "normalized.obj")
        temporary_script = self.job_dir / "qndf_compress.py"
        text = (QNDF / "compress.py").read_text(encoding="utf-8")
        text = text.replace("import os\n", "import os\n    import shutil\n", 1)
        text = text.replace('mlflow.set_tracking_uri("http://127.0.0.1:8080")',
                            'mlflow.set_tracking_uri(os.environ.get("MLFLOW_TRACKING_URI", "http://127.0.0.1:8081"))')
        text = text.replace(
            "save_obj(f'reconstruction.obj', tv, lf)",
            "save_obj(f'reconstruction.obj', tv, lf)\n        "
            "shutil.copy2('reconstruction.obj', os.environ['OPEN4D_QNDF_OUTPUT'])",
        )
        temporary_script.write_text(text, encoding="utf-8")
        raw_output = self.job_dir / "qndf_normalized.obj"
        try:
            self.command(
                "QNDF",
                conda("pytorch", "python", temporary_script, self.slug, "-ns", str(subdiv), "-cs", str(coarse),
                      "-hd", "28", "-nl", "17", "-ne", str(epochs), "-rs", self.job_id),
                QNDF,
                {
                    "OPEN4D_QNDF_OUTPUT": str(raw_output),
                    "MLFLOW_TRACKING_URI": (self.job_dir / "mlruns").resolve().as_uri(),
                    "MLFLOW_ALLOW_FILE_STORE": "true",
                    "PYTHONPATH": str(QNDF),
                },
            )
        finally:
            source_link.unlink(missing_ok=True)
        if not raw_output.exists():
            raise JobFailure("QNDF finished without exporting reconstruction.obj")
        final = self.results / "qndf.obj"
        self.restore(raw_output, final, qndf_coordinates=True)
        return {"output": str(final.relative_to(self.job_dir)), "variant": f"SSP · {epochs} epochs · cs{coarse}/ns{subdiv}"}

    def tvmc_xml(self, path: Path, in_dir: str, out_dir: str, prefix: str, first: int, last: int) -> None:
        path.write_text(f"""<?xml version="1.0"?>
<Config xmlns:xsd="http://www.w3.org/2001/XMLSchema" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
  <firstIndex>{first}</firstIndex><lastIndex>{last}</lastIndex><inDir>{in_dir}</inDir>
  <fileNamePrefix>{prefix}</fileNamePrefix><outDir>{out_dir}</outDir>
  <volumeGridResolution>512</volumeGridResolution><pointCount>2000</pointCount>
  <gradientThreshold>0.0001</gradientThreshold><smoothSigma>0.125</smoothSigma>
  <smoothSigma2>0.125</smoothSigma2><falloffStrength>0.05</falloffStrength>
  <applySmooth>1</applySmooth><applyLloyd>1</applyLloyd>
</Config>""", encoding="utf-8")

    def run_tvmc(self) -> dict[str, Any]:
        sequence = self.job_dir / "tvmc_sequence"
        self.duplicate_obj(sequence, [f"{self.slug}_fr{i:03}.obj" for i in range(1, 11)])
        staged = TVMC / "arap-volume-tracking/data" / self.slug
        staged.symlink_to(sequence, target_is_directory=True)
        centers = self.job_dir / "tvmc_centers"
        xml = self.job_dir / "tvmc_tracking.xml"
        self.tvmc_xml(xml, f"data/{self.slug}", str(centers), f"{self.slug}_fr", 1, 10)
        config = self.job_dir / "tvmc_config.json"
        atomic_json(config, {
            "dataset": self.slug, "editor_dataset": "basketball", "first_index": 1, "last_index": 10,
            "num_frames": 10, "num_centers": 2000, "key_frame": 4, "file_prefix": f"{self.slug}_fr",
            "tracking_config": str(xml), "centers_dir": str(centers),
        })
        decoded = self.job_dir / "tvmc_decoded"
        try:
            self.command("TVMC", conda("tsmc", "python", "pipeline.py", "--config", config, "--output", decoded), TVMC)
        finally:
            staged.unlink(missing_ok=True)
        candidates = sorted(decoded.glob("*.obj"))
        if not candidates:
            raise JobFailure("TVMC pipeline produced no decoded OBJ mesh")
        final = self.results / "tvmc.obj"
        self.restore(candidates[0], final)
        metrics = json.loads((decoded / "metrics.json").read_text()) if (decoded / "metrics.json").exists() else {}
        return {"output": str(final.relative_to(self.job_dir)), "native_metrics": metrics, "variant": "10 duplicated frames · QP 10"}

    def run_tsmc(self) -> dict[str, Any]:
        frames = 10
        centers_count = 2000
        track_root = TSMC / "arap-volume-tracking"
        tools_root = TSMC / "tsmc"
        editor_root = TSMC / "tvm-editing"
        editor_build = editor_root / "TVMEditor.Test/bin/Release/net5.0"
        sequence = self.job_dir / "tsmc_sequence"
        self.duplicate_obj(sequence, [f"mesh_{i:04}.obj" for i in range(frames)])
        staged = track_root / "data" / self.slug
        staged.symlink_to(sequence, target_is_directory=True)
        centers = self.job_dir / "tsmc_centers"
        xml = self.job_dir / "tsmc_tracking.xml"
        self.tvmc_xml(xml, f"data/{self.slug}", str(centers), "mesh_0", 0, 9)
        data_meshes = TSMC / "data" / self.slug / "meshes"
        self.duplicate_obj(data_meshes / "gt", [f"mesh_{i:02}.obj" for i in range(frames)])
        self.duplicate_obj(data_meshes / "dynamic", [f"mesh_{i:02}.obj" for i in range(frames)])
        static_dir = data_meshes / "static"
        static_dir.mkdir(parents=True, exist_ok=True)
        (static_dir / "mesh_00.obj").write_text("v 0 0 0\nv 0.000000001 0 0\nv 0 0.000000001 0\nf 1 2 3\n")
        data_root = editor_build / "Data" / f"{self.slug}_{centers_count}"
        output_root = editor_build / "output" / f"{self.slug}_{centers_count}"
        common = ["--dataset", self.slug, "--num_frames", str(frames), "--num_centers", str(centers_count)]
        compression_common = ["--dataset", self.slug, "--num_frames", str(frames)]
        reference = data_root / "reference_mesh/others/decoded_decimated_reference_mesh.obj"
        try:
            self.command("TSMC", ["dotnet", str(track_root / "bin/Client.dll"), str(xml)], track_root)
            self.command("TSMC", conda("tsmc", "python", "get_reference_center.py", *common, "--centers_dir", centers,
                                        "--random_state", "0", "--group_idx", "1"), tools_root)
            self.command("TSMC", conda("tsmc", "python", "get_transformation.py", *common, "--centers_dir", centers,
                                        "--firstIndex", "0", "--lastIndex", "9", "--group_idx", "1"), tools_root)
            editor = editor_build / "TVMEditor.Test"
            self.command("TSMC", [str(editor), "basketball", "1", "0", "9", str(data_root), str(output_root)], editor_root)
            self.command("TSMC", conda("tsmc", "python", "extract_reference_mesh.py", *common,
                                        "--inputDir", output_root / "output", "--outputDir", data_root / "reference_mesh",
                                        "--firstIndex", "0", "--lastIndex", "9", "--key", "4"), tools_root)
            self.command("TSMC", [str(editor), "basketball", "2", "0", "9", str(data_root), str(output_root)], editor_root)
            self.command("TSMC", conda("tsmc", "python", "get_displacements.py", *common, "--target_mesh_path", sequence,
                                        "--firstIndex", "0", "--lastIndex", "9", "--group_idx", "1"), tools_root)
            self.command("TSMC", conda("tsmc", "python", "compress_displacements.py", *compression_common,
                                        "--num_eigenvectors", str(self.settings.get("tsmc_eigenvectors", 5)),
                                        "--displacement_path", output_root / "reference", "--output_path", output_root / "reference",
                                        "--firstIndex", "0", "--lastIndex", "9", "--reference_mesh_path", reference), tools_root)
            evaluation_warning = None
            try:
                self.command("TSMC", conda("tsmc", "python", "evaluation.py", *common, "--input_path", output_root / "reference",
                                            "--dynamic_static_path", data_meshes, "--firstIndex", "0", "--lastIndex", "9",
                                            "--reference_mesh_path", reference, "--group_idx", "1"), tools_root)
            except JobFailure as error:
                # evaluation.py writes the decoded meshes before its optional Open3D
                # visualization/metric pass. That pass segfaults on some headless
                # NVIDIA hosts, but the codec output is already complete and can be
                # evaluated by this dashboard's common surface-metric implementation.
                decoded = output_root / "reference/decoded_reconstructed_meshes" / f"{self.slug}_000.obj"
                fallback = output_root / "reference/test" / f"{self.slug}_000.obj"
                if not decoded.exists() and not fallback.exists():
                    raise
                evaluation_warning = (
                    "TSMC native evaluation exited after writing the decoded meshes; "
                    f"the dashboard will use its shared metrics instead ({error})."
                )
        finally:
            staged.unlink(missing_ok=True)
        candidate = output_root / "reference/decoded_reconstructed_meshes" / f"{self.slug}_000.obj"
        if not candidate.exists():
            candidate = output_root / "reference/test" / f"{self.slug}_000.obj"
        if not candidate.exists():
            raise JobFailure("TSMC pipeline produced no decoded frame-0 OBJ mesh")
        final = self.results / "tsmc.obj"
        self.restore(candidate, final)
        result = {"output": str(final.relative_to(self.job_dir)), "variant": "all-dynamic adapter · 10 duplicated frames"}
        if evaluation_warning:
            result["warning"] = evaluation_warning
        return result

    def run(self) -> None:
        RUNS_ROOT.mkdir(parents=True, exist_ok=True)
        with (RUNS_ROOT / ".runner.lock").open("w") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            self.status = load_status(self.job_id)
            if self.status.get("state") == "cancelled":
                return
            self.status["state"] = "running"
            self.save()
            try:
                self.prepare()
                for method, callback in (
                    ("N4MC", self.run_n4mc),
                    ("QNDF", self.run_qndf),
                    ("TVMC", self.run_tvmc),
                    ("TSMC", self.run_tsmc),
                ):
                    self.execute_method(method, callback)
                completed = sum(item["state"] == "completed" for item in self.status["methods"].values())
                self.status["state"] = "completed" if completed == 4 else ("partial" if completed else "failed")
            except Exception as error:
                self.status["state"] = "failed"
                self.status["error"] = str(error)
                with (self.job_dir / "runner.log").open("a", encoding="utf-8") as handle:
                    handle.write(traceback.format_exc())
            self.status["finished_at"] = utc_now()
            self.save()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--job", required=True)
    args = parser.parse_args()
    Runner(args.job).run()


if __name__ == "__main__":
    main()
