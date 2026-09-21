"""Load optional research source without changing Python's import path."""

from hashlib import sha256
from importlib import import_module
from importlib.machinery import ModuleSpec
import os
from pathlib import Path
import sys
from types import ModuleType

from ._protocol import CodecError


def research_module(name: str):
    checkout = os.environ.get("OPEN4D_RESEARCH_ROOT")
    root = (
        Path(checkout).expanduser().resolve() / "open4d/codecs"
        if checkout else Path(__file__).resolve().parents[1] / "codecs"
    )
    component = name.split(".", 1)[0]
    if not (root / component).is_dir():
        raise CodecError(
            f"{component} research source is not included in this installation. "
            "Set OPEN4D_RESEARCH_ROOT to an Open4D source checkout and install "
            f"open4d[{component}]."
        )
    namespace = "_open4d_research_" + sha256(str(root).encode()).hexdigest()[:16]
    if namespace not in sys.modules:
        package = ModuleType(namespace)
        package.__path__ = [str(root)]
        package.__spec__ = ModuleSpec(namespace, loader=None, is_package=True)
        package.__spec__.submodule_search_locations = package.__path__
        sys.modules[namespace] = package
    return import_module(f"{namespace}.{name}")
