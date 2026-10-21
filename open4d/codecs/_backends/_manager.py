"""Staged backend installation with immutable source revisions and output hashes."""

from contextlib import contextmanager
import datetime as dt
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile
import time

from . import _recipe as recipes, _registry as registry


@contextmanager
def _lock(store):
    root = registry.home(store)
    root.mkdir(parents=True, exist_ok=True)
    path = root / ".install.lock"
    try:
        descriptor = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    except FileExistsError as error:
        raise recipes.RecipeError(f"backend store is busy: {path}; check its recorded PID before removing a stale lock") from error
    try:
        with os.fdopen(descriptor, "w") as stream:
            stream.write(str(os.getpid()))
        yield root
    finally:
        path.unlink(missing_ok=True)


def _run(argv, *, cwd=None, env=None, log=None, timeout=1800):
    with tempfile.TemporaryFile(mode="w+", encoding="utf-8") as output:
        try:
            subprocess.run([str(x) for x in argv], cwd=cwd, env=env, stdout=output,
                           stderr=subprocess.STDOUT, check=True, timeout=timeout)
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
            output.seek(0)
            detail = output.read()
            if log is not None:
                log.write(detail)
            raise recipes.RecipeError(f"backend command failed: {argv!r}\n{detail[-8000:]}") from error
        output.seek(0)
        text = output.read()
        if log is not None:
            log.write(text)
        return text.strip()


def plan(name, *, profile=None, recipe_path=None):
    recipe = recipes.load(name, recipe_path)
    if recipe["mode"] == "attach-only":
        return {"name": name, "mode": recipe["mode"], "installable": False,
                "license": recipe["license"], "reason": recipe["summary"], "profiles": []}
    selected, spec = recipes.profile(recipe, profile)
    problems = list(recipe["_unresolved"][selected])
    if (spec["platform"]["system"] != platform.system()
            or platform.machine() not in spec["platform"]["machine"]):
        problems.append(f"profile {selected} does not support {platform.system()} {platform.machine()}")
    tools = {}
    for command, required in spec.get("commands", {}).items():
        executable = shutil.which(command)
        if executable is None:
            problems.append(f"missing build command: {command} ({required})")
            continue
        version = recipes.first_version(_run([executable, "--version"], timeout=15))
        tools[command] = {"path": executable, "version": version}
        if version is None or not recipes.version_satisfies(version, required):
            problems.append(f"{command} {version} does not satisfy {required}")
    return {"name": name, "mode": recipe["mode"], "profile": selected,
            "recipe_sha256": recipe["_sha256"], "license": recipe["license"],
            "sources": spec.get("sources", []), "steps": spec.get("steps", []),
            "tools": tools, "unresolved": problems, "installable": not problems}


def doctor(name, *, store=None):
    item = registry.entry(name, store)
    if item is None:
        return {"name": name, "ok": False, "errors": ["no installed or attached backend"]}
    errors = []
    for component, value in item.get("components", {}).items():
        if not Path(value).is_dir():
            errors.append(f"missing {component} directory: {value}")
    for value, expected in item.get("hashes", {}).items():
        path = Path(value)
        if not path.is_file() or path.is_symlink() or recipes.file_digest(path) != expected:
            errors.append(f"missing or changed runtime output: {value}")
    interpreter = item.get("python")
    if interpreter and not Path(interpreter).is_file():
        errors.append(f"missing Python interpreter: {interpreter}")
    for role, value in item.get("tools", {}).items():
        if not Path(value).is_file():
            errors.append(f"missing {role}: {value}")
    return {"name": name, "state": item["state"], "ok": not errors, "errors": errors,
            "components": item.get("components", {}), "python": interpreter,
            "tools": item.get("tools", {}), "runtime_verified": item.get("runtime_verified", False)}


def _smoke(recipe, spec, values, staging, log):
    smoke = recipe.get("smoke", {})
    if smoke.get("codec") in ("vdmc", "faster_vdmc"):
        import numpy as np
        from open4d.codecs import O4DCodec
        from open4d.core import Frame, Sequence, MemoryFrameProvider, TriangleMesh
        from open4d.demo import mesh_sequence
        frames = [Frame(f.frame_index, f.timestamp,
                        TriangleMesh(f.geometry.positions, f.geometry.triangles))
                  for f in mesh_sequence(side=8, frames=2)]
        tools = {k: v.format(**values) for k, v in spec["register"]["tools"].items()}
        artifact = staging / "smoke.o4d"
        codec = O4DCodec(smoke["codec"])
        codec.encode(Sequence(MemoryFrameProvider(frames)), artifact, encoder=tools["encoder"])
        with codec.decode(artifact, decoder=tools["decoder"]) as decoded:
            if len(decoded) != len(frames):
                raise recipes.RecipeError("native V-DMC roundtrip changed frame count")
            for expected, actual in zip(frames, decoded):
                positions = actual.geometry.positions
                if not len(positions) or not len(actual.geometry.triangles) or not np.isfinite(positions).all():
                    raise recipes.RecipeError("native V-DMC roundtrip produced invalid geometry")
                for bound in (np.min, np.max):
                    if not np.allclose(bound(positions, axis=0), bound(expected.geometry.positions, axis=0), atol=.25):
                        raise recipes.RecipeError("native V-DMC roundtrip changed scene bounds")
        artifact.unlink()
        return True
    if smoke.get("codec") != "draco-native":
        return False
    from open4d.io._mesh import read_ply
    import numpy as np
    source, encoded, decoded = (staging / name for name in ("smoke.ply", "smoke.drc", "smoke-decoded.ply"))
    source.write_text("ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\nproperty float y\n"
                      "property float z\nelement face 1\nproperty list uchar int vertex_indices\n"
                      "end_header\n0 0 0\n1 0 0\n0 1 0\n3 0 1 2\n")
    tools = {k: v.format(**values) for k, v in spec["register"]["tools"].items()}
    _run([tools["encoder"], "-i", source, "-o", encoded], log=log)
    _run([tools["decoder"], "-i", encoded, "-o", decoded], log=log)
    positions, triangles, _ = read_ply(decoded)
    if len(positions) != 3 or len(triangles) != 1 or not np.isfinite(positions).all():
        raise recipes.RecipeError("native Draco roundtrip produced invalid geometry")
    if not np.allclose(np.sort(positions, axis=0), np.sort([[0., 0, 0], [1., 0, 0], [0., 1., 0]], axis=0), atol=1e-3):
        raise recipes.RecipeError("native Draco roundtrip changed geometry outside tolerance")
    for path in (source, encoded, decoded):
        path.unlink()
    return True


def attach(name, root, *, python=None, tools=None, store=None, recipe_path=None):
    recipe = recipes.load(name, recipe_path)
    root = Path(root).expanduser().resolve(strict=True)
    rules = recipe.get("attach", {}).get("components", {})
    components = {}
    for component in recipe["components"]:
        rule = rules.get(component, {"candidates": ["."], "markers": []})
        candidates = [root / value for value in rule["candidates"]]
        match = next((p for p in candidates if p.is_dir()
                      and all((p / marker).is_file() for marker in rule["markers"])), None)
        if match is None:
            raise recipes.RecipeError(f"{root} has no {component} runtime matching {rule['markers']}")
        components[component] = str(match.resolve())
    interpreter = shutil.which(str(python)) if python else None
    if python and interpreter is None:
        raise recipes.RecipeError(f"missing Python interpreter: {python}")
    resolved = {}
    for role, value in (tools or {}).items():
        executable = shutil.which(str(value))
        if executable is None:
            raise recipes.RecipeError(f"missing executable for {role}: {value}")
        resolved[role] = str(Path(executable).resolve())
    item = {"name": name, "state": "attached", "components": components,
            "python": str(Path(interpreter).resolve()) if interpreter else None,
            "tools": resolved, "recipe_sha256": recipe["_sha256"],
            "runtime_verified": False, "attached_at": dt.datetime.now(dt.timezone.utc).isoformat()}
    with _lock(store):
        registry.record(item, store)
    return doctor(name, store=store)


def install(name, *, profile=None, store=None, jobs=2, accept_license=False, recipe_path=None):
    if type(jobs) is not int or jobs < 1:
        raise ValueError("jobs must be a positive integer")
    proposal = plan(name, profile=profile, recipe_path=recipe_path)
    if not proposal["installable"]:
        raise recipes.RecipeError(json.dumps(proposal, indent=2))
    if not accept_license:
        raise recipes.RecipeError("read the recipe's separate runtime license and pass accept_license=True or --accept-license")
    recipe = recipes.load(name, recipe_path)
    _, spec = recipes.profile(recipe, proposal["profile"])
    with _lock(store) as root:
        destination = root / f"{name}-{recipe['recipe_version']}-{recipe['_sha256'][:16]}"
        previous = registry.entry(name, store)
        if previous and previous.get("root") == str(destination):
            report = doctor(name, store=store)
            if not report["ok"]:
                raise recipes.RecipeError(json.dumps(report))
            return report
        if destination.exists():
            raise recipes.RecipeError(f"unregistered install already exists: {destination}")
        with tempfile.TemporaryDirectory(prefix=f".{name}-", dir=root) as temporary:
            staging = Path(temporary)
            logpath = root / f"{name}-install.log"
            started = time.monotonic()
            with logpath.open("w", encoding="utf-8") as log:
                for source in spec.get("sources", []):
                    target = staging / source["path"]
                    target.mkdir(parents=True)
                    _run(["git", "init", target], log=log)
                    _run(["git", "-C", target, "remote", "add", "origin", source["url"]], log=log)
                    _run(["git", "-C", target, "fetch", "--depth", "1", "origin", source["commit"]], log=log)
                    if source.get("sparse"):
                        _run(["git", "-C", target, "sparse-checkout", "set", *source["sparse"]], log=log)
                    _run(["git", "-C", target, "checkout", "--detach", "FETCH_HEAD"], log=log)
                    if _run(["git", "-C", target, "rev-parse", "HEAD"]) != source["commit"]:
                        raise recipes.RecipeError("fetched source disagrees with its pin")
                values = {"root": str(staging), "jobs": str(jobs), "python": sys.executable,
                          "host_python": sys.executable, "dotnet": shutil.which("dotnet") or "dotnet",
                          "tools": str(staging / "tools")}
                for item in spec.get("files", []):
                    target = staging / item["target"]
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(recipes.DATA / item["file"], target)
                for item in spec.get("patches", []):
                    _run(["git", "apply", "--check", recipes.DATA / item["file"]], cwd=staging / item["target"], log=log)
                    _run(["git", "apply", recipes.DATA / item["file"]], cwd=staging / item["target"], log=log)
                # Recipes requiring an environment lock must implement an explicit venv step.
                for step in spec.get("steps", []):
                    argv = [value.format(**values) for value in step["argv"]]
                    env = {**os.environ, **{k: v.format(**values) for k, v in step.get("env", {}).items()}}
                    _run(argv, cwd=step.get("cwd", "{root}").format(**values), env=env, log=log)
                hashes = {}
                verified = _smoke(recipe, spec, values, staging, log)
                for item in spec.get("outputs", []):
                    output = staging / item["path"]
                    if output.is_symlink():
                        # CMake's executable VERSION creates an alias. Materialize
                        # confined aliases so the installed path survives relocation.
                        target = output.resolve(strict=True)
                        if not target.is_relative_to(staging) or not target.is_file():
                            raise recipes.RecipeError(f"runtime output links outside the staged install: {output}")
                        output.unlink()
                        shutil.copy2(target, output)
                    if not output.is_file() or output.is_symlink():
                        raise recipes.RecipeError(f"build did not produce {item['path']}")
                    if item.get("executable") and not os.access(output, os.X_OK):
                        raise recipes.RecipeError(f"build output is not executable: {output}")
                    hashes[str(destination / item["path"])] = recipes.file_digest(output)
                final_values = {**values, "root": str(destination)}
                register = spec.get("register", {})
                item = {"name": name, "state": "installed", "root": str(destination),
                        "components": {k: str(destination / v) for k, v in recipe["components"].items()},
                        "python": register.get("python", "").format(**final_values) or None,
                        "tools": {k: v.format(**final_values) for k, v in register.get("tools", {}).items()},
                        "environment": {k: v.format(**final_values) for k, v in register.get("environment", {}).items()},
                        "hashes": hashes, "recipe_sha256": recipe["_sha256"],
                        "source_revisions": {s["id"]: s["commit"] for s in spec.get("sources", [])},
                        "build_tools": proposal["tools"], "elapsed_seconds": time.monotonic() - started,
                        "runtime_verified": verified}
                (staging / "installation.json").write_text(json.dumps(item, indent=2) + "\n")
                os.replace(staging, destination)
                try:
                    registry.record(item, store)
                except BaseException:
                    shutil.rmtree(destination)
                    raise
        return doctor(name, store=store)
