"""Open4D players — interactive playback of ``.o4d`` geometry streams.

These viewers are built on ``PyQt6`` + ``pyqtgraph``. Those are optional
dependencies, so importing this subpackage raises a clear error if the GUI
stack is missing rather than failing at ``import open4d``:

    >>> from open4d.player import play_o4d_mesh
    >>> play_o4d_mesh("clip.o4d", fps=30.0)
"""
try:
    from .mesh import O4DMeshPlayer, play_o4d_mesh
    from .pointcloud import O4DPlayer, play_o4d_pointcloud
    from .draco_pointcloud import O4DDracoPlayer, play_o4d_draco_pointcloud
except ImportError as exc:  # pragma: no cover - depends on optional GUI stack
    raise ImportError(
        "open4d.player requires the GUI extras (PyQt6, pyqtgraph). "
        "Install them with:  pip install PyQt6 pyqtgraph"
    ) from exc

__all__ = [
    "O4DMeshPlayer",
    "play_o4d_mesh",
    "O4DPlayer",
    "play_o4d_pointcloud",
    "O4DDracoPlayer",
    "play_o4d_draco_pointcloud",
]
