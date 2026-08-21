"""Exercise TSMC's all-dynamic orchestration without its heavy backends."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess

import pytest

pytestmark = pytest.mark.cpu


def test_no_static_reaches_the_evaluation_stage(tmp_path: Path):
    repository = Path(__file__).resolve().parents[2]
    root = tmp_path / "tsmc"
    (root / "tsmc").mkdir(parents=True)
    (root / "tvm-editing/TVMEditor.Test/bin/Release/net10.0").mkdir(parents=True)
    (root / "tvm-editing/TVMEditor.Test/bin/Release/net10.0/TVMEditor.Test.dll").touch()
    (root / "arap-volume-tracking/data/combined-100-max-2000").mkdir(parents=True)
    (root / "data/demo/meshes").mkdir(parents=True)
    shutil.copy2(repository / "open4d/codecs/tsmc/run.sh", root / "run.sh")

    tools = tmp_path / "tools"
    tools.mkdir()
    log = tmp_path / "calls.log"
    fake = tools / "fake"
    fake.write_text(
        "#!/bin/sh\n"
        "printf '%s\\n' \"$*\" >> \"$OPEN4D_CALL_LOG\"\n"
        "if [ \"$1\" = './extract_reference_mesh.py' ]; then\n"
        "  while [ \"$1\" != '--outputDir' ]; do shift; done\n"
        "  mkdir -p \"$2\"\n"
        "  printf 'mesh' > \"$2/decimated_reference_mesh.obj\"\n"
        "fi\n",
        encoding="utf-8",
    )
    fake.chmod(0o755)
    (tools / "dotnet").symlink_to(fake)

    environment = os.environ | {
        "OPEN4D_CALL_LOG": str(log),
        "PATH": f"{tools}:{os.environ['PATH']}",
        "PYTHON": str(fake),
    }
    result = subprocess.run(
        ["bash", str(root / "run.sh"), "demo", "--groups", "1", "--no-static"],
        text=True, capture_output=True, env=environment,
    )

    assert result.returncode == 0, result.stderr
    evaluation = next(
        line for line in log.read_text(encoding="utf-8").splitlines()
        if line.startswith("evaluation.py ")
    )
    assert evaluation.endswith("--group_idx 1 --no-static")
