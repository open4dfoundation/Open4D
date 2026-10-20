"""Installed reconstruction methods, independent of research runtime licenses."""

from dataclasses import dataclass
from open4d._plugins import entry_points


@dataclass(frozen=True)
class MethodInfo:
    id: str
    backend: str


_METHODS = {}


def register_method(identifier, implementation, *, replace=False):
    """Register a callable accepting ``source, output=None, **options``."""
    if not isinstance(identifier, str) or not identifier:
        raise ValueError("method identifier must be a non-empty string")
    if not callable(implementation):
        raise TypeError("reconstruction method must be callable")
    if identifier in ("rgbd", "tsdf") or (identifier in _METHODS and not replace):
        raise ValueError(f"method {identifier!r} is already registered")
    _METHODS[identifier] = implementation


def available_methods():
    """List built-in RGB-D and installed metadata; do not import providers."""
    methods = {"rgbd": MethodInfo("rgbd", "open3d")}
    methods.update((name, MethodInfo(name, "custom")) for name in _METHODS)
    for name in entry_points("open4d.reconstruction"):
        if name not in methods and name != "tsdf":
            methods[name] = MethodInfo(name, "entry-point")
    return tuple(methods[name] for name in sorted(methods))


def get_method(identifier):
    """Resolve a selected extension, raising an actionable installation error."""
    if identifier in _METHODS:
        return _METHODS[identifier]
    if identifier in ("rgbd", "tsdf"):
        from .rgbd import reconstruct
        return reconstruct
    entry = entry_points("open4d.reconstruction").get(identifier)
    if entry is None:
        raise ValueError(
            f"Reconstruction method {identifier!r} is not installed; "
            "inspect available_methods(). Research adapters can be installed "
            "from an Open4D checkout with 'pip install ./open4d/codecs/_adapters/research'; "
            "their external runtimes retain their own license terms."
        )
    try:
        implementation = entry.load()
        if not callable(implementation):
            raise TypeError("entry point must expose a callable reconstruction method")
        return implementation
    except Exception as error:
        raise RuntimeError(f"Cannot load reconstruction plugin {identifier!r}: {error}") from error


def queen_reconstruction(source, output=None, **options):
    """MIT adapter only; requires independently acquired QUEEN runtime."""
    if output is None:
        raise TypeError("Gaussian reconstruction requires an output folder")
    from open4d.gaussians import reconstruct_gaussians
    return reconstruct_gaussians(source, output, method="queen", **options)


def gstream_reconstruction(source, output=None, **options):
    """MIT adapter only; requires independently acquired 3DGStream runtime."""
    if output is None:
        raise TypeError("Gaussian reconstruction requires an output folder")
    from open4d.gaussians import reconstruct_gaussians
    return reconstruct_gaussians(source, output, method="3dgstream", **options)
