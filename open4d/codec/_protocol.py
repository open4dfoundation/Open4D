"""Contracts shared by sequence codec implementations."""

from __future__ import annotations

from pathlib import Path
from collections.abc import Iterable
from typing import TYPE_CHECKING, Protocol, runtime_checkable

from open4d.core import Sequence

if TYPE_CHECKING:
    from open4d.gaussians import GaussianSplats, NeuralGaussianFrame


class CodecError(RuntimeError):
    """Encoding or decoding a sequence artifact failed."""


@runtime_checkable
class Codec(Protocol):
    """A mesh or Gaussian sequence encoder and decoder."""

    id: str
    suffixes: tuple[str, ...]

    def encode(self, sequence: Sequence | Iterable[GaussianSplats], destination: Path, **options) -> Path:
        """Encode *sequence* into *destination*."""

    def decode(self, source: Path, **options) -> Sequence | tuple[NeuralGaussianFrame, ...]:
        """Open a decoded sequence backed by *source*."""
