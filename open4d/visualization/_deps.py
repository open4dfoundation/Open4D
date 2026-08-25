"""Lazy optional dependency loading for visualization backends."""

from importlib import import_module
from typing import Any


class VisualizationDependencyError(ImportError):
    """A requested visualization backend is not installed."""


def require(module: str, extra: str = "player") -> Any:
    try:
        return import_module(module)
    except ImportError as error:
        raise VisualizationDependencyError(
            f"Visualization needs {module!r}. Install it with: "
            f"python -m pip install 'open4d[{extra}]'"
        ) from error
