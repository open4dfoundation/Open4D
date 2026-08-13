"""The contract a method adapter satisfies.

An adapter does not reimplement a trainer. It translates Open4D's arguments into
the upstream CLI, runs the upstream script as a subprocess from its own root, and
records what it did. Keeping upstream at arm's length like this is what makes a
pin bump a two-line change instead of a merge, and it is the only way to have
both methods in one module at all: their module names collide, so they cannot
share a process.
"""

from __future__ import annotations

import os
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Protocol

from .. import env, paths
from ..data import layouts
from ..io import manifest


@dataclass
class RunSpec:
    """What the user asked for, before any method-specific translation."""

    scene: Path
    run_dir: Path
    config: Path | None = None
    #: Everything after `--` on the command line, passed through untranslated.
    passthrough: tuple[str, ...] = ()
    #: Print the translated upstream command and stop. Works without a GPU, which
    #: is the only way to check an adapter's translation off the training host.
    dry_run: bool = False


class Method(Protocol):
    """A pinned upstream trainer, wrapped."""

    name: str
    upstream: str

    def train_command(self, spec: RunSpec) -> list[str]: ...

    def render_command(self, spec: RunSpec) -> list[str]: ...


def run(method: Method, spec: RunSpec, command: list[str], *, verb: str) -> int:
    """Execute an upstream command, bracketing it with manifest bookkeeping."""
    tree = paths.upstream(method.upstream)
    if not tree.exists():
        raise FileNotFoundError(f"{tree} is missing; run scripts/setup.sh")

    if spec.dry_run:
        print(f"(cd {tree} && PYTHONPATH={tree} {' '.join(command)})")
        return 0

    spec.run_dir.mkdir(parents=True, exist_ok=True)
    scene = layouts.detect(spec.scene)
    environment = env.report()

    if verb == "train":
        manifest.start(
            spec.run_dir,
            method=method.name,
            scene=scene.root,
            layout=scene.layout.value,
            frame_count=scene.frame_count,
            config=spec.config,
            environment=environment,
            command=command,
        )
    else:
        manifest.update(spec.run_dir, **{f"{verb}_command": command})

    # Upstream scripts resolve configs, weights, and submodule imports relative to
    # their own root, so cwd is the checkout and PYTHONPATH points at it. Nothing
    # from the other upstream is on the path.
    child_env = dict(os.environ, PYTHONPATH=str(tree))
    print(f"$ (cd {tree} && {' '.join(command)})")
    started = time.monotonic()
    status = subprocess.run(command, cwd=tree, env=child_env, check=False).returncode
    elapsed = time.monotonic() - started

    if verb == "train":
        manifest.finish(spec.run_dir, seconds=elapsed, exit_status=status)
    else:
        manifest.update(spec.run_dir, **{f"{verb}_seconds": round(elapsed, 3)})

    if status != 0:
        print(f"{method.name} {verb} exited {status} after {elapsed:.1f}s")
    return status
