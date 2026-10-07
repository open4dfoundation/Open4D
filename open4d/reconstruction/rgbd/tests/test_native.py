from pathlib import Path
import os
import shutil
import subprocess

import pytest


def test_pipe_termination_drains_pending_data_and_wakes_waiters(tmp_path):
    compiler = shutil.which("c++")
    if compiler is None:
        pytest.skip("a C++ compiler is required")
    root = Path(__file__).resolve().parents[1]
    executable = tmp_path / "pipe_test"
    subprocess.run([compiler, "-std=c++17", "-pthread", "-I", str(root / "include"),
                    str(root / "tests" / "pipe_test.cpp"), "-o", str(executable)],
                   check=True, capture_output=True, text=True, timeout=30)
    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stderr


@pytest.mark.skipif(os.name != "posix", reason="native sockets require POSIX")
def test_native_socket_timeouts_interruptions_and_cleanup(tmp_path):
    compiler = shutil.which("c++")
    if compiler is None:
        pytest.skip("a C++ compiler is required")
    root = Path(__file__).resolve().parents[1]
    executable = tmp_path / "network_stream_test"
    subprocess.run([compiler, "-std=c++17", "-pthread", "-I", str(root / "include" / "streaming"),
                    str(root / "tests" / "network_stream_test.cpp"),
                    str(root / "src" / "streaming" / "network_stream.cpp"), "-o", str(executable)],
                   check=True, capture_output=True, text=True, timeout=30)
    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=20)
    assert result.returncode == 0, result.stderr
