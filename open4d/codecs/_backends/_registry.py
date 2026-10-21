"""The local record of managed and attached research backends.

This module stays import-light: runtime path resolution consults it on every
adapter call that falls back to a default runtime. It reads one JSON file and
never imports research code, Torch or the installer.
"""

from __future__ import annotations

import json
import os
from pathlib import Path
import tempfile

SCHEMA = "open4d.backend-registry/v1"
STATES = ("installed", "attached")


def home(value: str | os.PathLike[str] | None = None) -> Path:
    """Backend store: argument, OPEN4D_BACKEND_HOME, then the user cache."""
    selected = value or os.environ.get("OPEN4D_BACKEND_HOME")
    if selected:
        return Path(selected).expanduser().absolute()
    if os.name == "nt":
        base = os.environ.get("LOCALAPPDATA") or "~/AppData/Local"
        return Path(base).expanduser() / "open4d" / "backends"
    base = os.environ.get("XDG_CACHE_HOME") or "~/.cache"
    return Path(base).expanduser() / "open4d" / "backends"


def registry_path(store: str | os.PathLike[str] | None = None) -> Path:
    return home(store) / "registry.json"


def _empty() -> dict:
    return {"schema": SCHEMA, "backends": {}}


def load(store: str | os.PathLike[str] | None = None) -> dict:
    path = registry_path(store)
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return _empty()
    except (OSError, ValueError) as error:
        raise ValueError(f"unreadable backend registry {path}: {error}") from error
    if not isinstance(data, dict) or data.get("schema") != SCHEMA or not isinstance(data.get("backends"), dict):
        raise ValueError(f"{path} is not an {SCHEMA} registry")
    return data


def save(data: dict, store: str | os.PathLike[str] | None = None) -> Path:
    """Replace the registry atomically; readers see the old or new file."""
    path = registry_path(store)
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".registry-", suffix=".json", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            json.dump(data, stream, indent=2, sort_keys=True, allow_nan=False)
            stream.write("\n")
        os.replace(temporary, path)
    except BaseException:
        Path(temporary).unlink(missing_ok=True)
        raise
    return path


def record(entry: dict, store: str | os.PathLike[str] | None = None) -> Path:
    """Register one backend, replacing only its own previous entry."""
    if entry.get("state") not in STATES:
        raise ValueError(f"registry state must be one of {STATES}")
    data = load(store)
    data["backends"][entry["name"]] = entry
    return save(data, store)


def remove(name: str, store: str | os.PathLike[str] | None = None) -> bool:
    data = load(store)
    removed = data["backends"].pop(name, None) is not None
    if removed:
        save(data, store)
    return removed


def entry(name: str, store: str | os.PathLike[str] | None = None) -> dict | None:
    return load(store)["backends"].get(name)


def _usable(store=None):
    """Entries for runtime lookup; a damaged registry never breaks adapters."""
    try:
        backends = load(store)["backends"]
    except ValueError:
        return []
    # A backend named after the component wins, so qndf beats qndf_int8 for
    # the shared qndf runtime; other providers follow in name order.
    return sorted(backends.values(), key=lambda item: str(item.get("name")))


def _providers(component: str, store=None):
    entries = _usable(store)
    exact = [item for item in entries if item.get("name") == component]
    return exact + [item for item in entries if item.get("name") != component]


def runtime(component: str, store=None) -> Path | None:
    """The registered runtime directory for one research component."""
    for item in _providers(component, store):
        path = (item.get("components") or {}).get(component)
        if isinstance(path, str) and Path(path).is_dir():
            return Path(path)
    return None


def python(component: str, store=None) -> str | None:
    """The interpreter registered with the backend that provides a component."""
    for item in _providers(component, store):
        if component in (item.get("components") or {}):
            value = item.get("python")
            return value if isinstance(value, str) and Path(value).is_file() else None
    return None


def tool(component: str, name: str, store=None) -> str | None:
    for item in _providers(component, store):
        if component in (item.get("components") or {}):
            value = (item.get("tools") or {}).get(name)
            return value if isinstance(value, str) and Path(value).is_file() else None
    return None


def worker(component: str, store=None) -> dict | None:
    """The opted-in worker entry for a component, if its registry asks for one."""
    for item in _providers(component, store):
        if component in (item.get("components") or {}):
            return item if item.get("worker") and item.get("python") else None
    return None


def environment(item: dict) -> dict[str, str]:
    """Variables that point adapters at one entry, overriding nothing else."""
    values = {}
    for component, path in (item.get("components") or {}).items():
        values[f"OPEN4D_{component.upper()}_ROOT"] = path
        if item.get("python"):
            values[f"OPEN4D_{component.upper()}_PYTHON"] = item["python"]
    if (item.get("tools") or {}).get("dotnet"):
        values["DOTNET"] = item["tools"]["dotnet"]
    values.update(item.get("environment") or {})
    return values
