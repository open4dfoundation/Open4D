import numpy as np

from open4d import TopologyMode, open_o4d_mesh_sequence
from open4d.io.o4d_mesh_io import O4DMeshReader, O4DMeshWriter


def test_o4d_mesh_sequence_decodes_frames_lazily(tmp_path, monkeypatch) -> None:
    path = tmp_path / "tiny.o4d"
    positions = np.array(
        [[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]],
        dtype=np.float32,
    )
    triangles = np.array([[0, 1, 2]], dtype=np.uint32)
    with O4DMeshWriter(str(path), meta={"dataset": "tiny"}) as writer:
        writer.write_keyframe(positions, triangles, frame_index=4, timestamp=1.0)
        writer.write_keyframe(
            positions + 1.0, triangles, frame_index=8, timestamp=1.5
        )

    calls = []
    original = O4DMeshReader.get_frame

    def counted_get_frame(self, frame_index):
        calls.append(frame_index)
        return original(self, frame_index)

    monkeypatch.setattr(O4DMeshReader, "get_frame", counted_get_frame)

    with open_o4d_mesh_sequence(str(path)) as sequence:
        assert len(sequence) == 2
        assert sequence.timestamps == (1.0, 1.5)
        assert sequence.metadata == {"dataset": "tiny"}
        assert sequence.topology is TopologyMode.UNKNOWN
        assert calls == []

        frame = sequence[1]
        assert frame.frame_index == 8
        np.testing.assert_array_equal(frame.geometry.positions, positions + 1.0)
        np.testing.assert_array_equal(frame.geometry.triangles, triangles)
        assert calls == [8]
