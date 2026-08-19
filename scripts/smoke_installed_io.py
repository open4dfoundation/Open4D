#!/usr/bin/env python3
"""Load a real mesh through an installed wheel, independent of the checkout."""

from pathlib import Path
import tempfile

import numpy as np

from open4d.io import inspect_sequence, open_sequence


with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / "triangle.obj"
    path.write_text("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", encoding="ascii")
    info = inspect_sequence(path)
    frame = open_sequence(path)[0]
    assert info.frame_count == 1 and info.format == "obj"
    np.testing.assert_array_equal(frame.geometry.triangles, [[0, 1, 2]])
    print(f"loaded {len(frame.geometry.positions)} vertices from {path.name}")
