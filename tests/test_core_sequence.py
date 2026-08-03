from dataclasses import FrozenInstanceError

import numpy as np
import pytest

from open4d.core import (
    Frame,
    MemoryFrameProvider,
    Sequence,
    SequenceView,
    TopologyMode,
    TriangleMesh,
)


def mesh(offset: float = 0.0) -> TriangleMesh:
    positions = np.array(
        [[offset, 0.0, 0.0], [offset + 1.0, 0.0, 0.0], [offset, 1.0, 0.0]],
        dtype=np.float32,
    )
    triangles = np.array([[0, 1, 2]], dtype=np.uint32)
    return TriangleMesh(positions, triangles)


def frames() -> list[Frame]:
    return [Frame(index, index * 0.5, mesh(index)) for index in range(3)]


def test_valid_mesh_preserves_storage_and_attributes() -> None:
    positions = mesh().positions
    triangles = np.array([[0, 1, 2]], dtype=np.int64)
    colors = np.ones((3, 4), dtype=np.float32)
    normals = np.tile([0.0, 0.0, 1.0], (3, 1)).astype(np.float32)
    coordinates = np.zeros((1, 3, 2), dtype=np.float32)
    confidence = np.ones(3, dtype=np.float32)

    value = TriangleMesh(
        positions,
        triangles,
        colors=colors,
        normals=normals,
        texture_coordinates=coordinates,
        attributes={"confidence": confidence},
    )

    assert value.positions is positions
    assert value.triangles is triangles
    assert value.attributes["confidence"] is confidence
    with pytest.raises(TypeError):
        value.attributes["other"] = confidence
    with pytest.raises(FrozenInstanceError):
        value.positions = positions.copy()
    positions[0, 0] = 2.0
    assert value.positions[0, 0] == 2.0


@pytest.mark.parametrize(
    ("positions", "triangles", "error", "message"),
    [
        (
            np.zeros((3, 2), dtype=np.float32),
            np.zeros((0, 3), dtype=np.int32),
            ValueError,
            "positions",
        ),
        (
            np.zeros((3, 3), dtype=np.int32),
            np.zeros((0, 3), dtype=np.int32),
            TypeError,
            "floating-point",
        ),
        (np.full((3, 3), np.nan), np.zeros((0, 3), dtype=np.int32), ValueError, "finite"),
        (np.zeros((3, 3)), np.zeros((1, 2), dtype=np.int32), ValueError, "triangles"),
        (np.zeros((3, 3)), np.zeros((1, 3), dtype=np.float32), TypeError, "integer"),
    ],
)
def test_invalid_mesh_arrays(
    positions: np.ndarray,
    triangles: np.ndarray,
    error: type[Exception],
    message: str,
) -> None:
    with pytest.raises(error, match=message):
        TriangleMesh(positions, triangles)


def test_out_of_range_triangle_indices() -> None:
    with pytest.raises(ValueError, match="between 0 and 2"):
        TriangleMesh(np.zeros((3, 3)), np.array([[0, 1, 3]]))
    with pytest.raises(ValueError, match="between 0 and 2"):
        TriangleMesh(np.zeros((3, 3)), np.array([[0, -1, 2]]))


def test_optional_mesh_attribute_validation() -> None:
    with pytest.raises(ValueError, match="colors"):
        TriangleMesh(mesh().positions, mesh().triangles, colors=np.ones((2, 3)))
    with pytest.raises(TypeError, match="normals"):
        TriangleMesh(
            mesh().positions,
            mesh().triangles,
            normals=np.ones((3, 3), dtype=np.int32),
        )
    with pytest.raises(ValueError, match="reserved"):
        TriangleMesh(
            mesh().positions,
            mesh().triangles,
            attributes={"positions": np.ones(3)},
        )


def test_frame_validation_and_metadata() -> None:
    value = Frame(4, 1.25, mesh(), metadata={"camera": "left"})
    assert value.frame_index == 4
    assert value.timestamp == 1.25
    assert value.metadata == {"camera": "left"}
    with pytest.raises(TypeError):
        value.metadata["camera"] = "right"
    with pytest.raises(ValueError, match="nonnegative"):
        Frame(-1, 0.0, mesh())
    with pytest.raises(ValueError, match="finite"):
        Frame(0, float("inf"), mesh())


class CountingProvider:
    def __init__(self) -> None:
        self.values = frames()
        self.calls: list[int] = []
        self.metadata = {"dataset": "tiny"}
        self.topology = TopologyMode.CHANGING
        self.timestamps = (0.0, 0.5, 1.0)
        self.has_constant_vertex_count = True
        self.has_vertex_correspondence = False

    @property
    def frame_count(self) -> int:
        return len(self.values)

    def get_frame(self, index: int) -> Frame:
        self.calls.append(index)
        return self.values[index]


def test_sequence_is_lazy_and_supports_indexing() -> None:
    provider = CountingProvider()
    sequence = Sequence(provider)

    assert provider.calls == []
    assert len(sequence) == sequence.frame_count == 3
    assert sequence.metadata == {"dataset": "tiny"}
    assert sequence.timestamps == (0.0, 0.5, 1.0)
    assert sequence.duration == 1.0
    assert sequence.fps == 2.0
    assert provider.calls == []

    assert sequence[1].frame_index == 1
    assert sequence[-1].frame_index == 2
    assert sequence.frame(0).frame_index == 0
    assert provider.calls == [1, 2, 0]
    with pytest.raises(IndexError):
        _ = sequence[3]
    with pytest.raises(IndexError):
        _ = sequence[-4]


def test_sequence_slicing_is_lazy_and_composable() -> None:
    provider = CountingProvider()
    sequence = Sequence(provider)

    view = sequence[::2]
    nested = view[::-1]

    assert isinstance(view, SequenceView)
    assert len(view) == 2
    assert len(nested) == 2
    assert provider.calls == []
    assert nested[0].frame_index == 2
    assert provider.calls == [2]


def test_iteration_and_topology_declarations() -> None:
    provider = CountingProvider()
    sequence = Sequence(provider)

    assert [frame.frame_index for frame in sequence] == [0, 1, 2]
    assert sequence.topology is TopologyMode.CHANGING
    assert sequence.has_constant_topology is False
    assert sequence.has_constant_vertex_count is True
    assert sequence.has_vertex_correspondence is False

    fixed = Sequence(
        MemoryFrameProvider(frames(), topology=TopologyMode.FIXED)
    )
    assert fixed.has_constant_topology is True
    assert fixed.has_constant_vertex_count is True
    assert fixed.has_vertex_correspondence is True
