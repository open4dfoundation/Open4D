"""Bounded native-tool execution with robust diagnostics."""

from collections import deque
import math
import os
import signal
import subprocess
import tempfile

from ._protocol import CodecError


def _timeout():
    # Native encodes of long sequences can legitimately run for hours, so the
    # bound is opt-in rather than a default that kills working jobs.
    value = os.environ.get("OPEN4D_NATIVE_TIMEOUT", "").strip()
    if not value:
        return None
    try:
        timeout = float(value)
    except ValueError:
        timeout = math.nan
    if not math.isfinite(timeout) or timeout <= 0:
        raise CodecError(f"OPEN4D_NATIVE_TIMEOUT must be a finite positive number of seconds, got {value!r}")
    return timeout


def run(command, label, *, cwd=None):
    timeout = _timeout()
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
