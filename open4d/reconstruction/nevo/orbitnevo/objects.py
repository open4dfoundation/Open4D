"""Which ORBIT objects this baseline serves.

Vendored from `baselines/objects.py` in the 4DVideoStreaming repository, reduced
to the one function `prepare.py` uses and with the dependency on that repo's
`vstream.config` made lazy.

There, `vstream.config.OBJECTS` was the single source of truth for the scene the
comparison streamed, so every baseline read the same list and none could silently
serve a different scene. That package is not part of Open4D, so the fallback can
no longer be resolved here: passing `--objects` works exactly as before, and
omitting it now raises instead of quietly reading a list that does not exist.
"""
from __future__ import annotations

from typing import Iterable


def configured_object_names() -> tuple[str, ...]:
    """Names of the objects enabled in the comparison's shared scene config.

    Imported lazily and by name so that this module -- and therefore
    `orbitnevo.prepare` -- imports without the 4DVideoStreaming `vstream`
    package present. Only the no-`--objects` path needs it.
    """
    try:
        from vstream import config  # type: ignore[import-not-found]
    except ImportError as exc:
        raise RuntimeError(
            "the configured ORBIT scene lives in vstream/config.py in the "
            "4DVideoStreaming repository, which is not vendored here. Pass "
            "--objects to name the objects explicitly."
        ) from exc
    return tuple(spec.name for spec in config.OBJECTS)


def resolve_object_names(requested: Iterable[str] | None) -> tuple[str, ...]:
    """The explicitly requested names if there are any, else the configured set."""
    names = tuple(requested or ())
    return names or configured_object_names()
