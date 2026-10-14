"""Shared training utilities for user-supplied models and licensed datasets.

Neural compression implementations belong to codec plugins. Importing this
module does not import Torch or download any models or checkpoints.
"""

from ._training import TrainingHistory, train

__all__ = ["TrainingHistory", "train"]
