#!/usr/bin/env python3
"""Load a real mesh through an installed wheel, independent of the checkout."""

from pathlib import Path
import tempfile

import numpy as np

from open4d.codec import CodecError, available_codecs, decode_sequence, encode_sequence
from open4d.io import inspect_sequence, open_sequence
from open4d.visualization import visualize


with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / "triangle.obj"
    path.write_text("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", encoding="ascii")
    info = inspect_sequence(path)
    frame = open_sequence(path)[0]
    assert info.frame_count == 1 and info.format == "obj"
    np.testing.assert_array_equal(frame.geometry.triangles, [[0, 1, 2]])
    artifact = encode_sequence(open_sequence(path), Path(directory) / "triangle.o4d")
    decoded = decode_sequence(artifact)
    np.testing.assert_array_equal(decoded[0].geometry.positions, frame.geometry.positions)
    assert callable(visualize)
    installed = {info.id for info in available_codecs()}
    for codec, suffix in (("temporal-delta", ".td4d"), ("temporal-pca", ".tp4d")):
        temporal_artifact = encode_sequence(
            open_sequence(path), Path(directory) / f"triangle{suffix}", codec=codec
        )
        temporal = decode_sequence(temporal_artifact)
        assert len(temporal) == 1 and len(temporal[0].geometry.triangles) == 1
        temporal.close()
    source_only = {"klt", "n4mc", "qndf", "qndf-int8"}
    assert not installed & source_only
    try:
        encode_sequence(open_sequence(path), Path(directory) / "triangle.k4d", codec="klt")
    except CodecError as error:
        assert "source checkout" in str(error)
    else:
        raise AssertionError("source-only KLT was advertised by the installed wheel")
    decoded.close()
    print(f"loaded and round-tripped {len(frame.geometry.positions)} vertices")
