"""Exercise TSMC's all-dynamic orchestration without its heavy backends."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import sys

import pytest
import numpy as np

pytestmark = pytest.mark.cpu


@pytest.mark.parametrize("frames,group", [(2, 1), (3, 1), (4, 1), (5, 1), (6, 1), (2, 2)])
def test_reference_centers_accept_short_sequences(tmp_path, frames, group):
    pytest.importorskip("open3d")
    pytest.importorskip("sklearn")
    repository = Path(__file__).resolve().parents[2]
    script = repository / "open4d/codecs/tsmc/tsmc/get_reference_center.py"
    points = np.array([[0., 0, 0], [1., 0, 0], [0., 1, 0], [0., 0, 1]])
    for index in range(frames * group):
        np.savetxt(tmp_path/f"centers_{index:03d}.xyz", points + index)
    result = subprocess.run([sys.executable, str(script), "--dataset", "short",
                             "--num_frames", str(frames), "--num_centers", "4",
                             "--centers_dir", str(tmp_path), "--group_idx", str(group)],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    selected = (group - 1) * frames + min(4, frames - 1)
    np.testing.assert_allclose(np.loadtxt(tmp_path/'reference/reference_centers_aligned.xyz'), points + selected)


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
