"""Load optional research source without changing Python's import path."""

from hashlib import sha256
from importlib import import_module
from importlib.machinery import ModuleSpec
import os
from pathlib import Path
import sys
from types import ModuleType

from ._protocol import CodecError
from open4d._research_paths import default_runtime


def research_module(name: str):
    component = name.split(".", 1)[0]
    external = os.environ.get(f"OPEN4D_{component.upper()}_ROOT")
    root = default_runtime(component).parent
    direct_root = None
    if external:
        # Each runtime may live outside the Open4D checkout. The component
        # directory is an importable research package, not redistributed here.
        component_root = Path(external).expanduser().resolve()
        if (component_root / component).is_dir():
            root = component_root
        else:
            root = component_root
            direct_root = component_root
    if not (direct_root or root / component).is_dir():
        raise CodecError(
            f"{component} research source is not included in this installation. "
            f"Set OPEN4D_{component.upper()}_ROOT to its licensed source package, "
            "or OPEN4D_RESEARCH_ROOT to an authorized Open4D source checkout, and install "
            f"open4d[{component}]."
        )
    namespace = "_open4d_research_" + sha256(str(root).encode()).hexdigest()[:16]
    if namespace not in sys.modules:
        package = ModuleType(namespace)
        package.__path__ = [str(root)]
        package.__spec__ = ModuleSpec(namespace, loader=None, is_package=True)
        package.__spec__.submodule_search_locations = package.__path__
        sys.modules[namespace] = package
    if direct_root is not None:
        qualified = f"{namespace}.{component}"
        if qualified not in sys.modules:
            package = ModuleType(qualified)
            package.__path__ = [str(direct_root)]
            package.__spec__ = ModuleSpec(qualified, loader=None, is_package=True)
            package.__spec__.submodule_search_locations = package.__path__
            sys.modules[qualified] = package
    return import_module(f"{namespace}.{name}")
