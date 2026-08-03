"""Run the all-dynamic TSMC adapter on the ten-frame basketball sequence."""

from __future__ import annotations

from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[1]
TRACK = ROOT / "arap-volume-tracking"
TOOLS = ROOT / "tsmc"
EDITOR = ROOT / "tvm-editing"
EDITOR_BUILD = EDITOR / "TVMEditor.Test/bin/Release/net5.0"
SOURCE = ROOT.parent / "tvmc/arap-volume-tracking/data/basketball_player"
SLUG = "basketball_compare"
FRAMES = 10
CENTERS = 2000
OUTPUT = ROOT / "outputs/basketball_sequence_tsmc"
STATUS = OUTPUT / "status.json"


def now() -> str:
    return datetime.now(timezone.utc).isoformat()


def write_json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n")
    temporary.replace(path)


def run(command: list[str], cwd: Path, capture: Path | None = None, allow_failure: bool = False) -> int:
    printable = " ".join(map(str, command))
    print("+", printable, flush=True)
    result = subprocess.run(command, cwd=cwd, text=True, stdout=subprocess.PIPE if capture else None,
                            stderr=subprocess.STDOUT if capture else None)
    if capture:
        capture.write_text(result.stdout or "")
        print(result.stdout or "", flush=True)
    if result.returncode and not allow_failure:
        raise subprocess.CalledProcessError(result.returncode, command)
    return result.returncode


def xml(path: Path, centers_dir: Path) -> None:
    path.write_text(f"""<?xml version="1.0"?>
<Config xmlns:xsd="http://www.w3.org/2001/XMLSchema" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
  <firstIndex>0</firstIndex><lastIndex>9</lastIndex><inDir>data/{SLUG}</inDir>
  <fileNamePrefix>mesh_0</fileNamePrefix><outDir>{centers_dir}</outDir>
  <volumeGridResolution>512</volumeGridResolution><pointCount>{CENTERS}</pointCount>
  <gradientThreshold>0.0001</gradientThreshold><smoothSigma>0.125</smoothSigma>
  <smoothSigma2>0.125</smoothSigma2><falloffStrength>0.05</falloffStrength>
  <applySmooth>1</applySmooth><applyLloyd>1</applyLloyd>
</Config>\n""")


def main() -> None:
    OUTPUT.mkdir(parents=True, exist_ok=True)
    status = {"state": "preparing", "started_at": now(), "updated_at": now(), "frames": FRAMES,
              "variant": "all-dynamic adapter", "eigenvectors": 5}
    write_json(STATUS, status)

    sequence = OUTPUT / "source"
    sequence.mkdir(exist_ok=True)
    sources = sorted(SOURCE.glob("basketball_player_fr*.obj"))
    if len(sources) != FRAMES:
        raise RuntimeError(f"expected {FRAMES} source frames, found {len(sources)}")
    for index, source in enumerate(sources):
        link = sequence / f"mesh_{index:04d}.obj"
        link.unlink(missing_ok=True)
        link.symlink_to(source)

    staged = TRACK / "data" / SLUG
    if staged.exists() or staged.is_symlink():
        if not staged.is_symlink():
            raise RuntimeError(f"refusing to replace non-symlink {staged}")
        staged.unlink()
    staged.symlink_to(sequence, target_is_directory=True)

    data_meshes = ROOT / "data" / SLUG / "meshes"
    for kind in ("gt", "dynamic"):
        target = data_meshes / kind
        target.mkdir(parents=True, exist_ok=True)
        for index, source in enumerate(sources):
            link = target / f"mesh_{index:02d}.obj"
            link.unlink(missing_ok=True)
            link.symlink_to(source)
    static = data_meshes / "static"
    static.mkdir(parents=True, exist_ok=True)
    (static / "mesh_00.obj").write_text("v 0 0 0\nv 0.000000001 0 0\nv 0 0.000000001 0\nf 1 2 3\n")

    centers = OUTPUT / "centers"
    tracking_xml = OUTPUT / "tracking.xml"
    xml(tracking_xml, centers)
    data_root = EDITOR_BUILD / "Data" / f"{SLUG}_{CENTERS}"
    editor_output = EDITOR_BUILD / "output" / f"{SLUG}_{CENTERS}"
    common = ["--dataset", SLUG, "--num_frames", str(FRAMES), "--num_centers", str(CENTERS)]
    python = os.environ.get("PYTHON", os.sys.executable)
    reference = data_root / "reference_mesh/others/decoded_decimated_reference_mesh.obj"

    try:
        status.update(state="tracking", updated_at=now()); write_json(STATUS, status)
        private_dotnet = Path.home() / ".dotnet/dotnet"
        dotnet = os.environ.get("DOTNET", str(private_dotnet) if private_dotnet.exists() else "dotnet")
        tracked = sorted(centers.glob(f"mesh_0res_{CENTERS}_*.xyz"))
        if len(tracked) == FRAMES:
            print(f"Reusing {len(tracked)} tracked center files from {centers}", flush=True)
        else:
            run([dotnet, str(TRACK / "bin/Client.dll"), str(tracking_xml)], TRACK)
        status.update(state="fitting", updated_at=now()); write_json(STATUS, status)
        run([python, "get_reference_center.py", *common, "--centers_dir", str(centers),
             "--random_state", "0", "--group_idx", "1"], TOOLS)
        run([python, "get_transformation.py", *common, "--centers_dir", str(centers),
             "--firstIndex", "0", "--lastIndex", "9", "--group_idx", "1"], TOOLS)
        editor = EDITOR_BUILD / "TVMEditor.Test.dll"
        run([dotnet, str(editor), "basketball", "1", "0", "9", str(data_root), str(editor_output)], EDITOR)
        run([python, "extract_reference_mesh.py", *common, "--inputDir", str(editor_output / "output"),
             "--outputDir", str(data_root / "reference_mesh"), "--firstIndex", "0", "--lastIndex", "9",
             "--key", "4"], TOOLS)
        run([dotnet, str(editor), "basketball", "2", "0", "9", str(data_root), str(editor_output)], EDITOR)
        status.update(state="compressing", updated_at=now()); write_json(STATUS, status)
        run([python, "get_displacements.py", *common, "--target_mesh_path", str(sequence),
             "--firstIndex", "0", "--lastIndex", "9", "--group_idx", "1"], TOOLS)
        run([python, "compress_displacements.py", "--dataset", SLUG, "--num_frames", "10",
             "--num_eigenvectors", "5", "--displacement_path", str(editor_output / "reference"),
             "--output_path", str(editor_output / "reference"), "--firstIndex", "0", "--lastIndex", "9",
             "--reference_mesh_path", str(reference)], TOOLS)
        status.update(state="evaluating", updated_at=now()); write_json(STATUS, status)
        native_log = OUTPUT / "native_evaluation.log"
        returncode = run([python, "evaluation.py", *common, "--input_path", str(editor_output / "reference"),
                          "--dynamic_static_path", str(data_meshes), "--firstIndex", "0", "--lastIndex", "9",
                          "--reference_mesh_path", str(reference), "--group_idx", "1"], TOOLS,
                         capture=native_log, allow_failure=True)
        decoded_root = editor_output / "reference/decoded_reconstructed_meshes"
        fallback = editor_output / "reference/test"
        decoded_dir = OUTPUT / "decoded"
        decoded_dir.mkdir(exist_ok=True)
        for index, source in enumerate(sources):
            candidate = decoded_root / f"{SLUG}_{index:03d}.obj"
            if not candidate.exists():
                candidate = fallback / f"{SLUG}_{index:03d}.obj"
            if not candidate.exists():
                raise FileNotFoundError(candidate)
            shutil.copy2(candidate, decoded_dir / f"decoded_{source.name}")

        native_metrics = {}
        for line in reversed(native_log.read_text(errors="replace").splitlines()):
            if line.startswith("{"):
                try:
                    native_metrics = json.loads(line)
                    break
                except json.JSONDecodeError:
                    pass
        write_json(OUTPUT / "native_metrics.json", native_metrics)
        status.update(state="complete", updated_at=now(), finished_at=now(),
                      native_evaluation_returncode=returncode,
                      warning=None if returncode == 0 else "native headless evaluation exited after mesh decoding",
                      outputs={"decoded": "decoded", "native_metrics": "native_metrics.json"})
        write_json(STATUS, status)
    except Exception as error:
        status.update(state="failed", updated_at=now(), error=str(error)); write_json(STATUS, status)
        raise
    finally:
        staged.unlink(missing_ok=True)


if __name__ == "__main__":
    main()
