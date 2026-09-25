"""Publish completed outputs while preserving existing destinations."""

import errno
import os
import shutil
from pathlib import Path


def publish_file(temporary: Path, destination: Path, *, overwrite: bool = False) -> None:
    """Move a completed temporary file on the same filesystem into place."""
    if overwrite:
        temporary.replace(destination)
    else:
        try:
            os.link(temporary, destination)
        except OSError as error:
            if error.errno not in {errno.EPERM, errno.EOPNOTSUPP, errno.ENOSYS, errno.EXDEV}:
                raise
            # Filesystems such as exFAT have no hard links. Exclusive creation
            # retains no-clobber semantics, though this fallback is not atomic
            # for readers observing the destination while it is being copied.
            with temporary.open("rb") as source:
                with destination.open("xb") as target:
                    try:
                        shutil.copyfileobj(source, target)
                    except BaseException:
                        destination.unlink(missing_ok=True)
                        raise
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
