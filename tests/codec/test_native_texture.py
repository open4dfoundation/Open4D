"""Content checks against actual native UV and texture-video encoders."""

import os
from pathlib import Path
from dataclasses import replace

import numpy as np
import pytest

from open4d.codec import O4DCodec, inspect_o4d
from open4d.core import Frame, Sequence, MemoryFrameProvider, MeshTexture, TextureImage
from open4d.demo import mesh_sequence


@pytest.mark.parametrize("identifier", ("vdmc", "faster_vdmc"))
def test_native_texture_contains_color_after_cold_decode(tmp_path, identifier):
    encoder = os.environ.get("OPEN4D_" + identifier.upper() + "_ENCODER")
    decoder = os.environ.get("OPEN4D_" + identifier.upper() + "_DECODER")
    config = os.environ.get("OPEN4D_" + identifier.upper() + "_TEXTURE_CONFIG")
    if not all(path and Path(path).is_file() for path in (encoder, decoder, config)):
        pytest.skip("set native encoder, decoder and TEXTURE_CONFIG for this codec")
    geometry = mesh_sequence(side=12, frames=1)[0].geometry
    xy = geometry.positions[:, :2]
    uv = (xy - xy.min(0)) / np.ptp(xy, axis=0)
    pixels = np.zeros((128, 128, 3), np.uint8)
    pixels[8:-8, 8:-8] = [210, 70, 30]
    pixels[32:96, 32:96] = [20, 220, 40]
    mesh = replace(geometry, colors=None, normals=None, attributes={},
                   texture_coordinates=uv, texture=MeshTexture((TextureImage(pixels),)))
    sequence = Sequence(MemoryFrameProvider([Frame(0, 0, mesh)]))
    codec = O4DCodec(identifier)
    path = codec.encode(sequence, tmp_path / "native.o4d", encoder=encoder,
                        attributes="texture", texture_encoder_config=config)
    manifest = inspect_o4d(path)
    assert manifest["native"]["profile"] == identifier + "/2"
    # Only native bytes and sequence metadata travel, not a PNG/NumPy fallback.
    assert {f["name"] for f in manifest["files"]} == {"sequence.vmesh"}
    with codec.decode(path, decoder=decoder) as decoded:
        result = decoded[0].geometry
        assert result.texture_coordinates is not None
        assert 0 <= result.texture_coordinates.min() <= result.texture_coordinates.max() <= 1
        image = result.texture.images[0].pixels
        assert image.mean() > 10 and image.std() > 10
        assert np.any((image[:, :, 1] > 120) & (image[:, :, 0] < 100))
