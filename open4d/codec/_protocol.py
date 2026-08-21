"""Contracts shared by sequence codec implementations."""

from __future__ import annotations

from pathlib import Path
from typing import Protocol, runtime_checkable

from open4d.core import Sequence


class CodecError(RuntimeError):
    """Encoding or decoding a sequence artifact failed."""


@runtime_checkable
class Codec(Protocol):
    """A replaceable triangle-mesh sequence encoder and decoder."""

    id: str
    suffixes: tuple[str, ...]

    def encode(self, sequence: Sequence, destination: Path, **options) -> Path:
        """Encode *sequence* into *destination*."""

    def decode(self, source: Path, **options) -> Sequence:
        """Open a decoded sequence backed by *source*."""
