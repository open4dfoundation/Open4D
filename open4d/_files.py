"""Publish completed outputs while preserving existing destinations."""

import os
from pathlib import Path


def publish_file(temporary: Path, destination: Path, *, overwrite: bool = False) -> None:
    """Move a completed temporary file on the same filesystem into place."""
    if overwrite:
        temporary.replace(destination)
    else:
        os.link(temporary, destination)
        temporary.unlink()


def publish_directory(temporary: Path, destination: Path) -> None:
    """Reserve an unused directory name and move completed output into place."""
    if os.name == "nt":
        temporary.rename(destination)
        return
    destination.mkdir()
    try:
        temporary.rename(destination)
    except BaseException:
        try:
            destination.rmdir()
        except OSError:
            pass
        raise
