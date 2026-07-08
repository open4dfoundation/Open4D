"""Shared machinery for the Open4D compression-pipeline adapters.

Every algorithm (N4MC, TSMC, TVMC) is exposed as a :class:`Codec` with the
same tiny contract::

    codec = get_codec("n4mc")            # or TSMCCodec(), TVMCCodec()
    cap = codec.available()              # what's runnable in this environment
    result = codec.compress(source, ...) # runs the REAL pipeline

:meth:`Codec.compress` always returns a :class:`CompressionResult` carrying
per-stage wall-clock timings, produced artifact paths, and any metrics the
pipeline reported. The heavy pipelines are orchestrated as their real
subprocess stages (that is how they actually run); this layer just gives them
a uniform Python entry point, times each stage, and collects the outputs.
"""
from __future__ import annotations

import os
import subprocess
import time
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from typing import Callable, Dict, List, Optional, Sequence


class PipelineError(RuntimeError):
    """Raised when a pipeline stage fails (non-zero exit, missing output)."""


@dataclass
class StageTiming:
    """Wall-clock timing (and status) for one stage of a pipeline."""

    name: str
    seconds: float
    ok: bool = True
    detail: str = ""

    def __repr__(self) -> str:
        flag = "ok " if self.ok else "ERR"
        return f"[{flag}] {self.name:<28} {self.seconds:8.2f}s {self.detail}"


@dataclass
class Capability:
    """Result of a codec's environment probe."""

    ok: bool
    missing: List[str] = field(default_factory=list)
    notes: List[str] = field(default_factory=list)

    def __repr__(self) -> str:
        head = "available" if self.ok else "NOT available"
        lines = [f"Capability({head})"]
        for m in self.missing:
            lines.append(f"  missing: {m}")
        for n in self.notes:
            lines.append(f"  note:    {n}")
        return "\n".join(lines)


@dataclass
class CompressionResult:
    """Structured result of a compression run."""

    codec: str
    source: str
    workdir: str
    stages: List[StageTiming] = field(default_factory=list)
    artifacts: Dict[str, str] = field(default_factory=dict)
    metrics: Dict[str, float] = field(default_factory=dict)
    ok: bool = True

    @property
    def total_seconds(self) -> float:
        return sum(s.seconds for s in self.stages)

    def reconstruction_paths(self) -> List[str]:
        """Paths of reconstructed mesh artifacts, sorted by frame."""
        return sorted(
            p for k, p in self.artifacts.items() if k.startswith("rec_mesh")
        )

    def to_mesh_sequence(self):
        """Load reconstructed meshes back into an :class:`open4d.core.MeshSequence`."""
        import trimesh

        from open4d.core import MeshSequence

        seq = MeshSequence(name=f"{self.codec}:reconstruction")
        for i, path in enumerate(self.reconstruction_paths()):
            m = trimesh.load_mesh(path, process=False)
            seq.append(m.vertices, m.faces, timestamp=float(i))
        return seq

    def stage_table(self) -> str:
        rows = [repr(s) for s in self.stages]
        rows.append("-" * 52)
        rows.append(f"{'TOTAL':<34} {self.total_seconds:8.2f}s")
        return "\n".join(rows)

    def __repr__(self) -> str:
        status = "ok" if self.ok else "FAILED"
        return (
            f"CompressionResult(codec={self.codec!r}, {status}, "
            f"stages={len(self.stages)}, total={self.total_seconds:.2f}s, "
            f"artifacts={len(self.artifacts)})"
        )


class StageRunner:
    """Times pipeline stages, whether subprocess commands or Python callables."""

    def __init__(self) -> None:
        self.stages: List[StageTiming] = []
        self.log_tail: Dict[str, str] = {}

    def run_cmd(
        self,
        name: str,
        argv: Sequence[str],
        cwd: Optional[str] = None,
        env: Optional[Dict[str, str]] = None,
        check: bool = True,
        timeout: Optional[float] = None,
    ) -> subprocess.CompletedProcess:
        """Run a subprocess stage, recording its wall-clock time."""
        full_env = dict(os.environ)
        if env:
            full_env.update(env)
        t0 = time.perf_counter()
        proc = subprocess.run(
            list(argv),
            cwd=cwd,
            env=full_env,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
        dt = time.perf_counter() - t0
        ok = proc.returncode == 0
        self.stages.append(StageTiming(name, dt, ok, "" if ok else f"exit {proc.returncode}"))
        self.log_tail[name] = (proc.stdout or "")[-4000:] + (proc.stderr or "")[-4000:]
        if check and not ok:
            tail = self.log_tail[name][-1500:]
            raise PipelineError(f"stage {name!r} failed (exit {proc.returncode}):\n{tail}")
        return proc

    def run_py(self, name: str, fn: Callable, *args, **kwargs):
        """Run an in-process Python stage, recording its wall-clock time."""
        t0 = time.perf_counter()
        try:
            out = fn(*args, **kwargs)
        except Exception as exc:  # noqa: BLE001
            dt = time.perf_counter() - t0
            self.stages.append(StageTiming(name, dt, False, f"{type(exc).__name__}: {exc}"))
            raise
        dt = time.perf_counter() - t0
        self.stages.append(StageTiming(name, dt, True))
        return out


class Codec(ABC):
    """Uniform entry point for an Open4D compression pipeline."""

    #: short identifier, e.g. "n4mc"
    name: str = ""
    #: one-line human description
    description: str = ""

    @abstractmethod
    def available(self) -> Capability:
        """Probe the environment; report whether/what can run."""

    @abstractmethod
    def compress(self, source, *, workdir: Optional[str] = None, **params) -> CompressionResult:
        """Run the real pipeline on ``source`` and return a structured result."""

    def _require_available(self) -> None:
        cap = self.available()
        if not cap.ok:
            raise PipelineError(
                f"{self.name} cannot run in this environment:\n{cap}"
            )

    def __repr__(self) -> str:
        return f"<{type(self).__name__} name={self.name!r}>"
