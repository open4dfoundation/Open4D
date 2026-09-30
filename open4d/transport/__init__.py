"""Send decoded mesh frames over TCP and receive them on the other side."""

from ._tcp import receive, send

__all__ = ["receive", "send"]
