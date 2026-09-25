"""Bounded native-tool execution with robust diagnostics."""

from collections import deque
import math
import os
import signal
import subprocess
import tempfile

from ._protocol import CodecError


def run(command, label, *, cwd=None):
    timeout = float(os.environ.get("OPEN4D_NATIVE_TIMEOUT", "3600"))
    if not math.isfinite(timeout) or timeout <= 0:
        raise ValueError("OPEN4D_NATIVE_TIMEOUT must be finite and positive")
    with tempfile.TemporaryFile(mode="w+", encoding="utf-8", errors="replace") as log:
        with subprocess.Popen(
            command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT,
            start_new_session=os.name != "nt",
        ) as process:
            try:
                process.wait(timeout=timeout)
            except BaseException as error:
                if os.name != "nt":
                    try:
                        os.killpg(process.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                else:
                    process.kill()
                process.wait()
                if isinstance(error, subprocess.TimeoutExpired):
                    raise CodecError(f"{label} timed out after {timeout:g} seconds") from error
                raise
            if process.returncode:
                log.seek(0)
                detail = "".join(deque(log, maxlen=16)).strip()
                raise CodecError(f"{label} exited {process.returncode}: {detail or 'no diagnostic output'}")
