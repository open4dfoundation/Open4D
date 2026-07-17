from __future__ import annotations

import json
import os
import signal
import subprocess
import sys
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parent
RUNS_ROOT = ROOT / "runs"


def atomic_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    temporary.replace(path)


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def create_job(upload_name: str, upload_bytes: bytes, settings: dict[str, Any]) -> str:
    RUNS_ROOT.mkdir(parents=True, exist_ok=True)
    job_id = datetime.now().strftime("%Y%m%d_%H%M%S") + "_" + uuid.uuid4().hex[:8]
    job_dir = RUNS_ROOT / job_id
    job_dir.mkdir(mode=0o700)
    input_path = job_dir / "input.obj"
    input_path.write_bytes(upload_bytes)
    status = {
        "job_id": job_id,
        "name": Path(upload_name).stem,
        "created_at": utc_now(),
        "updated_at": utc_now(),
        "state": "queued",
        "settings": settings,
        "input": "input.obj",
        "normalized_input": None,
        "pid": None,
        "error": None,
        "methods": {
            name: {"state": "pending", "started_at": None, "finished_at": None, "output": None, "error": None}
            for name in ("N4MC", "QNDF", "TVMC", "TSMC")
        },
    }
    atomic_json(job_dir / "status.json", status)
    process = subprocess.Popen(
        [sys.executable, str(ROOT / "job_runner.py"), "--job", job_id],
        cwd=ROOT,
        stdout=(job_dir / "runner.log").open("ab"),
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )
    status["pid"] = process.pid
    status["updated_at"] = utc_now()
    atomic_json(job_dir / "status.json", status)
    return job_id


def load_status(job_id: str) -> dict[str, Any]:
    return json.loads((RUNS_ROOT / job_id / "status.json").read_text(encoding="utf-8"))


def list_jobs() -> list[dict[str, Any]]:
    if not RUNS_ROOT.exists():
        return []
    jobs = []
    for path in RUNS_ROOT.glob("*/status.json"):
        try:
            item = json.loads(path.read_text(encoding="utf-8"))
            if not {"job_id", "name", "state", "methods", "input"}.issubset(item):
                continue
            jobs.append(item)
        except (OSError, json.JSONDecodeError):
            continue
    return sorted(jobs, key=lambda item: item.get("created_at", ""), reverse=True)


def cancel_job(job_id: str) -> bool:
    status = load_status(job_id)
    pid = status.get("pid")
    if not pid or status.get("state") not in {"queued", "running"}:
        return False
    try:
        os.killpg(int(pid), signal.SIGTERM)
    except ProcessLookupError:
        pass
    status["state"] = "cancelled"
    status["updated_at"] = utc_now()
    status["error"] = "Cancelled by user"
    for method in status["methods"].values():
        if method["state"] in {"pending", "running"}:
            method["state"] = "cancelled"
    atomic_json(RUNS_ROOT / job_id / "status.json", status)
    return True
