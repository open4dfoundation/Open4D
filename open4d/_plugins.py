"""Metadata-only discovery of optional, separately installed extensions."""

from importlib import metadata
import re
import warnings


def entry_points(group: str) -> dict[str, object]:
    """Discover unique names without importing extension code.

    Ambiguous registrations are ignored rather than selected by installation
    order. Listing installed extensions never imports Torch, CUDA or runtimes.
    """
    found = metadata.entry_points()
    entries = (found.select(group=group) if hasattr(found, "select")
               else found.get(group, ()))
    by_name = {}
    duplicates = set()
    for entry in entries:
        if not re.fullmatch(r"[a-z0-9][a-z0-9_-]*", entry.name):
            warnings.warn(f"Ignoring invalid {group} plugin name {entry.name!r}", RuntimeWarning)
            continue
        if entry.name in by_name:
            duplicates.add(entry.name)
        else:
            by_name[entry.name] = entry
    for name in sorted(duplicates):
        warnings.warn(f"Ignoring duplicate {group} plugin {name!r}; uninstall one provider", RuntimeWarning)
        del by_name[name]
    return dict(sorted(by_name.items()))
