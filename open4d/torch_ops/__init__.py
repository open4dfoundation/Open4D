"""Torch-backed mesh operations, replacing Open4D's use of PyTorch3D.

Importing this package requires torch, which the base Open4D install does not
pull in. Only the codecs that already depend on torch import from here.
"""

from .io import Faces, load_obj, save_obj
from .mesh import (
    Meshes,
    chamfer_distance,
    face_normals,
    point_face_distance,
    point_face_distance_bruteforce,
    sample_points_from_mesh,
    vertex_normals,
)

__all__ = [
    "Faces",
    "Meshes",
    "chamfer_distance",
    "face_normals",
    "load_obj",
    "point_face_distance",
    "point_face_distance_bruteforce",
    "sample_points_from_mesh",
    "save_obj",
    "vertex_normals",
]
