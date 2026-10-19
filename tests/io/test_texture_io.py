from dataclasses import replace

import numpy as np
import pytest

from open4d.core import Frame, Sequence, MemoryFrameProvider, MeshTexture, TextureImage
from open4d.io import write_sequence, open_sequence
from tests.codecs.test_owned_attributes import textured_mesh


def test_obj_sequence_retains_owned_pixels_and_corner_uvs(tmp_path):
    mesh = replace(textured_mesh(), attributes={}, attribute_domains={})
    source = Sequence(MemoryFrameProvider([Frame(0, 0., mesh)]))
    write_sequence(source, tmp_path / "sequence", format="obj")
    with open_sequence(tmp_path / "sequence") as decoded:
        actual = decoded[0].geometry
        np.testing.assert_array_equal(actual.texture_coordinates, mesh.texture_coordinates)
        np.testing.assert_array_equal(actual.texture.images[0].pixels, mesh.texture.images[0].pixels)


@pytest.mark.parametrize("name", ["../../outside", "image/path", "..", "a\\b", "name#comment"])
def test_material_names_cannot_be_output_paths(name):
    with pytest.raises(ValueError):
        MeshTexture((TextureImage(np.zeros((2, 2, 3), np.uint8)),), material_names=(name,))
