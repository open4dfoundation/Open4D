"""Reconstruction inputs and the built-in RGB-D method."""

from ._multiview import MultiviewCapture

from ._registry import MethodInfo, available_methods, register_method

__all__ = ["MultiviewCapture", "MethodInfo", "available_methods", "register_method"]
