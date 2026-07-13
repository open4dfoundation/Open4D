#!/usr/bin/env python3
"""Config-driven, resumable runner for the complete TVMC pipeline."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parent
TRACKING = ROOT / "arap-volume-tracking"
PYTHON_TOOLS = ROOT / "TVMC"
EDITING = ROOT / "tvm-editing"
EDITOR_BUILD = EDITING / "TVMEditor.Test/bin/Release/net5.0"
STAGES = (
    "track",
    "reference-centers",
    "transformations",
    "deform-to-reference",
    "extract-reference",
    "deform-reference",
    "displacements",
    "evaluation",
)


class PipelineError(RuntimeError):
    pass


def load_config(path: Path) -> dict[str, Any]:
    try:
        config = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise PipelineError(f"configuration not found: {path}") from exc
    except json.JSONDecodeError as exc:
        raise PipelineError(f"invalid JSON in {path}: {exc}") from exc
    required = {
        "dataset",
        "editor_dataset",
        "first_index",
        "last_index",
        "num_centers",
        "key_frame",
        "file_prefix",
        "tracking_config",
        "centers_dir",
    }
    missing = sorted(required - config.keys())
    if missing:
        raise PipelineError(f"configuration is missing: {', '.join(missing)}")
    expected_frames = config["last_index"] - config["first_index"] + 1
    config.setdefault("num_frames", expected_frames)
    if config["num_frames"] != expected_frames:
        raise PipelineError("num_frames must equal last_index - first_index + 1")
    return config


def resolve(value: str, base: Path = ROOT) -> Path:
    path = Path(value).expanduser()
    return path if path.is_absolute() else (base / path).resolve()


def executable(name_or_path: str) -> str | None:
    path = Path(name_or_path)
    if path.is_absolute() or path.parent != Path("."):
        return str(path) if path.is_file() and os.access(path, os.X_OK) else None
    return shutil.which(name_or_path)


def select_stages(start: str, end: str, only: str | None) -> tuple[str, ...]:
    if only:
        return (only,)
    first, last = STAGES.index(start), STAGES.index(end)
    if first > last:
        raise PipelineError("--from must not come after --to")
    return STAGES[first : last + 1]


def paths(config: dict[str, Any]) -> dict[str, Path]:
    dataset, centers = config["dataset"], config["num_centers"]
    data_root = EDITOR_BUILD / "Data" / f"{dataset}_{centers}"
    output_root = EDITOR_BUILD / "output" / f"{dataset}_{centers}"
    return {
        "mesh_dir": TRACKING / "data" / dataset,
        "centers_dir": resolve(config["centers_dir"]),
        "data_root": data_root,
        "output_root": output_root,
        "reference_mesh": data_root / "reference_mesh/decimated_reference_mesh.obj",
        "editor_dll": EDITOR_BUILD / "TVMEditor.Test.dll",
    }


def preflight(config: dict[str, Any], stages: tuple[str, ...], args: argparse.Namespace) -> dict[str, str]:
    found: dict[str, str] = {}
    errors: list[str] = []
    p = paths(config)

    if "track" in stages:
        found["dotnet"] = executable("dotnet") or ""
        if not found["dotnet"]:
            errors.append("dotnet is missing (run ./setup.sh or use ./run_pipeline.sh --docker ...)")
        tracking_config = TRACKING / config["tracking_config"]
        if not tracking_config.is_file():
            errors.append(f"tracking config is missing: {tracking_config}")
        if not p["mesh_dir"].is_dir():
            errors.append(f"input meshes are missing: {p['mesh_dir']}")
        else:
            missing_meshes = [
                p["mesh_dir"] / f"{config['file_prefix']}{index:03}.obj"
                for index in range(config["first_index"], config["last_index"] + 1)
            ]
            missing_meshes = [path for path in missing_meshes if not path.is_file()]
            if missing_meshes:
                sample = ", ".join(path.name for path in missing_meshes[:3])
                suffix = "..." if len(missing_meshes) > 3 else ""
                errors.append(f"{len(missing_meshes)} input mesh(es) are missing ({sample}{suffix})")

    python_stages = set(stages) - {"track", "deform-to-reference", "deform-reference"}
    if python_stages:
        try:
            subprocess.run(
                [sys.executable, "-c", "import numpy, open3d, scipy, sklearn, trimesh"],
                check=True,
                capture_output=True,
                text=True,
            )
        except subprocess.CalledProcessError:
            errors.append("Python dependencies are missing (run ./setup.sh)")

    needs_editor = bool({"deform-to-reference", "deform-reference"} & set(stages))
    if needs_editor:
        found.setdefault("dotnet", executable("dotnet") or "")
        if not found["dotnet"]:
            errors.append("dotnet is missing (run ./setup.sh or use Docker)")
        if not p["editor_dll"].is_file():
            errors.append(f"mesh editor is not built: {p['editor_dll']} (run ./setup.sh)")

    if "reference-centers" in stages and "track" not in stages and not p["centers_dir"].is_dir():
        errors.append(f"tracked centers are missing: {p['centers_dir']}")

    if "evaluation" in stages:
        encoder = args.encoder or os.environ.get("DRACO_ENCODER") or str(ROOT / "draco/build/draco_encoder")
        decoder = args.decoder or os.environ.get("DRACO_DECODER") or str(ROOT / "draco/build/draco_decoder")
        found["encoder"] = executable(encoder) or ""
        found["decoder"] = executable(decoder) or ""
        if not found["encoder"] or not found["decoder"]:
            errors.append("Draco encoder/decoder are missing (run ./setup.sh, or pass --encoder and --decoder)")

    if errors:
        raise PipelineError("preflight failed:\n  - " + "\n  - ".join(errors))
    return found


def command_plan(config: dict[str, Any], stages: tuple[str, ...], tools: dict[str, str], args: argparse.Namespace):
    p = paths(config)
    first, last = str(config["first_index"]), str(config["last_index"])
    frames, centers = str(config["num_frames"]), str(config["num_centers"])
    dataset = config["dataset"]
    common = ["--dataset", dataset, "--num_frames", frames, "--num_centers", centers]
    indexed = ["--firstIndex", first, "--lastIndex", last]
    python = sys.executable
    editor_args = [config["editor_dataset"], None, first, last, str(p["data_root"]), str(p["output_root"])]

    for stage in stages:
        if stage == "track":
            yield stage, TRACKING, [tools["dotnet"], "build", str(TRACKING / "Client/Client.csproj"), "-c", "Release"]
            yield stage, TRACKING, [tools["dotnet"], str(TRACKING / "bin/Client.dll"), config["tracking_config"]]
            improvement = config.get("improvement_config")
            if improvement:
                yield stage, TRACKING, [tools["dotnet"], str(TRACKING / "bin/Client.dll"), improvement]
        elif stage == "reference-centers":
            cmd = [python, str(PYTHON_TOOLS / "get_reference_center.py"), *common, "--centers_dir", str(p["centers_dir"]), "--random_state", str(args.random_state), "--jobs", str(args.jobs)]
            yield stage, PYTHON_TOOLS, cmd
        elif stage == "transformations":
            yield stage, PYTHON_TOOLS, [python, str(PYTHON_TOOLS / "get_transformation.py"), *common, "--centers_dir", str(p["centers_dir"]), *indexed]
        elif stage == "deform-to-reference":
            editor_args[1] = "1"
            yield stage, EDITING, [tools["dotnet"], str(p["editor_dll"]), *editor_args]
        elif stage == "extract-reference":
            yield stage, PYTHON_TOOLS, [python, str(PYTHON_TOOLS / "extract_reference_mesh.py"), *common, "--inputDir", str(p["output_root"] / "output"), "--outputDir", str(p["data_root"] / "reference_mesh"), *indexed, "--key", str(config["key_frame"])]
        elif stage == "deform-reference":
            editor_args[1] = "2"
            yield stage, EDITING, [tools["dotnet"], str(p["editor_dll"]), *editor_args]
        elif stage == "displacements":
            yield stage, PYTHON_TOOLS, [python, str(PYTHON_TOOLS / "get_displacements.py"), *common, "--target_mesh_path", str(p["mesh_dir"]), *indexed]
        elif stage == "evaluation":
            output = resolve(args.output) if args.output else PYTHON_TOOLS / f"{dataset}_outputs"
            yield stage, PYTHON_TOOLS, [python, str(PYTHON_TOOLS / "evaluation.py"), *common, *indexed, "--fileNamePrefix", config["file_prefix"], "--encoderPath", tools["encoder"], "--decoderPath", tools["decoder"], "--qp", str(args.qp), "--outputPath", str(output)]


def shell_line(command: list[str]) -> str:
    import shlex
    return shlex.join(command)


def main() -> int:
    parser = argparse.ArgumentParser(description="Run TVMC end to end or resume at any stage.")
    parser.add_argument("dataset", nargs="?", default="basketball", help="bundled dataset preset (default: basketball)")
    parser.add_argument("--config", type=Path, help="custom dataset JSON configuration")
    parser.add_argument("--list", action="store_true", help="list bundled dataset presets and exit")
    parser.add_argument("--from", dest="start", choices=STAGES, default=STAGES[0], help="first stage to run")
    parser.add_argument("--to", dest="end", choices=STAGES, default=STAGES[-1], help="last stage to run")
    parser.add_argument("--only", choices=STAGES, help="run exactly one stage")
    parser.add_argument("--dry-run", action="store_true", help="print commands without checking or running them")
    parser.add_argument("--random-state", type=int, default=0, help="reproducible MDS seed (default: 0)")
    parser.add_argument("--jobs", type=int, default=1, help="parallel MDS jobs (default: 1; safest on macOS)")
    parser.add_argument("--qp", type=int, default=10, help="Draco displacement quantization bits")
    parser.add_argument("--encoder", help="path to draco_encoder")
    parser.add_argument("--decoder", help="path to draco_decoder")
    parser.add_argument("--output", help="evaluation output directory")
    args = parser.parse_args()

    if args.list:
        for path in sorted((ROOT / "configs").glob("*.json")):
            print(path.stem)
        return 0

    config_path = args.config or ROOT / "configs" / f"{args.dataset}.json"
    try:
        config = load_config(config_path)
        stages = select_stages(args.start, args.end, args.only)
        tools = {} if args.dry_run else preflight(config, stages, args)
        if args.dry_run:
            tools = {"dotnet": "dotnet", "encoder": args.encoder or "draco_encoder", "decoder": args.decoder or "draco_decoder"}
        print(f"TVMC dataset: {config['dataset']}")
        print(f"Stages: {' -> '.join(stages)}")
        for stage, cwd, command in command_plan(config, stages, tools, args):
            print(f"\n[{stage}] {shell_line(command)}", flush=True)
            if not args.dry_run:
                subprocess.run(command, cwd=cwd, check=True)
        print("\nDry run complete." if args.dry_run else "\nTVMC pipeline complete.")
        return 0
    except (PipelineError, subprocess.CalledProcessError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return exc.returncode if isinstance(exc, subprocess.CalledProcessError) else 2


if __name__ == "__main__":
    raise SystemExit(main())
