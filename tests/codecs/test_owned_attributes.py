import numpy as np
import pytest

from open4d.core import Frame, Sequence, MemoryFrameProvider, TriangleMesh, TextureImage, MeshTexture
from open4d.codecs import encode_sequence, decode_sequence
from open4d.codecs._npz import REFERENCE_CODECS
from open4d.codecs.draco import DRACO_CODEC


def textured_mesh():
    return TriangleMesh(np.array([[0, 0, 0], [1, 0, 0], [0, 1, 0], [0, 0, 1]], dtype=np.float32),
                        [[0, 1, 2], [0, 1, 3], [0, 2, 3], [1, 2, 3]],
                        texture_coordinates=np.zeros((4, 3, 2)),
                        attributes={"label": np.array([4, 7, 9, 12])},
                        attribute_domains={"label": "face"},
                        texture=MeshTexture((TextureImage(np.arange(24, dtype=np.uint8).reshape(2, 4, 3)),)))


@pytest.mark.parametrize("codec", REFERENCE_CODECS, ids=lambda c: c.id)
def test_array_codec_owns_textures_and_ambiguous_attribute_domains(tmp_path, codec):
    original = textured_mesh()
    artifact = encode_sequence(Sequence(MemoryFrameProvider([Frame(0, 0., original)])),
                               tmp_path / "textured.o4d", codec=codec)
    with decode_sequence(artifact, codec=codec) as decoded:
        actual = decoded[0].geometry
        assert actual.attribute_domains == {"label": "face"}
        np.testing.assert_array_equal(actual.texture.images[0].pixels, original.texture.images[0].pixels)
        np.testing.assert_array_equal(actual.attributes["label"], original.attributes["label"])


def test_draco_native_attributes_follow_uv_seams_and_materials(tmp_path):
    pytest.importorskip("DracoPy")
    mesh = textured_mesh()
    artifact = encode_sequence(Sequence(MemoryFrameProvider([Frame(0, 0., mesh)])),
                               tmp_path / "draco.o4d", codec=DRACO_CODEC)
    with decode_sequence(artifact, codec=DRACO_CODEC) as sequence:
        decoded = sequence[0].geometry
        assert decoded.attribute_domains["label"] == "face"
        np.testing.assert_array_equal(decoded.attributes["label"], mesh.attributes["label"])
        np.testing.assert_array_equal(decoded.texture.images[0].pixels, mesh.texture.images[0].pixels)
