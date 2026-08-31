"""Contract tests for frames, providers, sequences, views, and cleanup."""

from __future__ import annotations

from types import MappingProxyType

import numpy as np
import pytest

from open4d import Frame, MemoryFrameProvider, Sequence, TopologyMode, TriangleMesh

pytestmark = pytest.mark.cpu


def mesh(offset: float = 0.0) -> TriangleMesh:
    return TriangleMesh(
        np.asarray([[offset, 0, 0], [offset, 1, 0], [offset, 0, 1]], dtype=np.float32),
        np.asarray([[0, 1, 2]], dtype=np.uint32),
    )


def frames(count: int = 4) -> list[Frame]:
    return [Frame(10 + index, index * 0.25, mesh(index), {"ordinal": index}) for index in range(count)]


@pytest.mark.parametrize("value", [-1, -100])
def test_frame_rejects_negative_indices(value):
    with pytest.raises(ValueError, match="nonnegative"):
        Frame(value, 0.0, mesh())


@pytest.mark.parametrize("value", [True, 1.5, "1", None])
def test_frame_rejects_non_integer_indices(value):
    with pytest.raises(TypeError, match="integer"):
        Frame(value, 0.0, mesh())


def test_frame_accepts_numpy_integer_and_normalizes_it():
    frame = Frame(np.int64(8), np.float32(0.5), mesh())
    assert frame.frame_index == 8
    assert type(frame.frame_index) is int
    assert frame.timestamp == pytest.approx(0.5)
    assert type(frame.timestamp) is float


@pytest.mark.parametrize("value", [True, "0", None])
def test_frame_rejects_non_real_timestamps(value):
    with pytest.raises(TypeError, match="real"):
        Frame(0, value, mesh())


@pytest.mark.parametrize("value", [np.nan, np.inf, -np.inf])
def test_frame_rejects_nonfinite_timestamps(value):
    with pytest.raises(ValueError, match="finite"):
        Frame(0, value, mesh())


def test_frame_requires_geometry_and_mapping_metadata():
    with pytest.raises(TypeError, match="TriangleMesh"):
        Frame(0, 0.0, object())
    with pytest.raises(TypeError, match="mapping"):
        Frame(0, 0.0, mesh(), [])


def test_frame_metadata_is_a_read_only_snapshot():
    source = {"capture": "left"}
    frame = Frame(0, 0.0, mesh(), source)
    source["capture"] = "right"
    assert isinstance(frame.metadata, MappingProxyType)
    assert frame.metadata["capture"] == "left"
    with pytest.raises(TypeError):
        frame.metadata["new"] = 1


def test_memory_provider_validates_inputs_and_snapshots_frames():
    source = frames(2)
    provider = MemoryFrameProvider(source, metadata={"name": "tiny"})
    source.clear()
    assert provider.frame_count == 2
    assert provider.timestamps == (0.0, 0.25)
    assert provider.metadata == {"name": "tiny"}
    with pytest.raises(TypeError, match="sequence"):
        MemoryFrameProvider(iter(frames()))
    with pytest.raises(TypeError, match="Frame"):
        MemoryFrameProvider([object()])
    with pytest.raises(TypeError, match="TopologyMode"):
        MemoryFrameProvider([], topology="fixed")


@pytest.mark.parametrize("name", ["has_constant_vertex_count", "has_vertex_correspondence"])
def test_memory_provider_rejects_invalid_optional_flags(name):
    with pytest.raises(TypeError, match=name):
        MemoryFrameProvider([], **{name: 1})


def test_memory_provider_bounds_and_index_types():
    provider = MemoryFrameProvider(frames(2))
    assert provider.get_frame(0).frame_index == 10
    for index in (-1, 2):
        with pytest.raises(IndexError):
            provider.get_frame(index)
    with pytest.raises(TypeError, match="integer"):
        provider.get_frame(True)


class RecordingProvider:
    def __init__(self, values=None, *, timestamps=None):
        self.values = frames(5) if values is None else values
        self.calls: list[int] = []
        self.metadata = {"capture": "recording"}
        self.topology = TopologyMode.CHANGING
        if timestamps is not None:
            self.timestamps = timestamps

    @property
    def frame_count(self):
        return len(self.values)

    def get_frame(self, index):
        self.calls.append(index)
        return self.values[index]


def test_sequence_construction_is_lazy_and_metadata_is_snapshotted():
    provider = RecordingProvider()
    sequence = Sequence(provider)
    assert provider.calls == []
    provider.metadata["capture"] = "changed"
    assert sequence.metadata == {"capture": "recording"}
    assert sequence[1].frame_index == 11
    assert provider.calls == [1]


@pytest.mark.parametrize("count", [-1, True, 1.5])
def test_sequence_rejects_invalid_provider_counts(count):
    class Provider:
        frame_count = count
        def get_frame(self, index):
            raise AssertionError
    with pytest.raises(ValueError, match="nonnegative integer"):
        Sequence(Provider())


def test_sequence_requires_provider_protocol_and_valid_declarations():
    with pytest.raises(TypeError, match="FrameProvider"):
        Sequence(object())
    provider = RecordingProvider()
    provider.metadata = []
    with pytest.raises(TypeError, match="metadata"):
        Sequence(provider)
    provider = RecordingProvider()
    provider.topology = "changing"
    with pytest.raises(TypeError, match="topology"):
        Sequence(provider)


def test_sequence_integer_indexing_negative_index_and_bounds():
    sequence = Sequence(RecordingProvider())
    assert sequence[0].frame_index == 10
    assert sequence[-1].frame_index == 14
    assert sequence[np.int64(2)].frame_index == 12
    for index in (-6, 5):
        with pytest.raises(IndexError):
            sequence[index]
    for index in (True, 1.5, "1"):
        with pytest.raises(TypeError, match="indices"):
            sequence[index]


def test_sequence_validates_provider_results_and_propagates_provider_errors():
    provider = RecordingProvider([object()])
    sequence = Sequence(provider)
    with pytest.raises(TypeError, match="return a Frame"):
        sequence[0]

    marker = RuntimeError("decoder failed")
    class FailingProvider:
        frame_count = 1
        def get_frame(self, index):
            raise marker
    with pytest.raises(RuntimeError) as caught:
        Sequence(FailingProvider())[0]
    assert caught.value is marker


def test_iteration_decodes_in_ordinal_order():
    provider = RecordingProvider()
    assert [frame.frame_index for frame in Sequence(provider)] == [10, 11, 12, 13, 14]
    assert provider.calls == [0, 1, 2, 3, 4]


def test_provider_timestamps_avoid_frame_decoding_and_are_cached():
    provider = RecordingProvider(timestamps=(0.0, 0.1, 0.2, 0.3, 0.4))
    sequence = Sequence(provider)
    assert sequence.timestamps == provider.timestamps
    assert provider.calls == []
    provider.timestamps = (99.0,) * 5
    assert sequence.timestamps == (0.0, 0.1, 0.2, 0.3, 0.4)


def test_timestamps_fall_back_to_frames_only_once():
    provider = RecordingProvider()
    sequence = Sequence(provider)
    assert sequence.timestamps == (0.0, 0.25, 0.5, 0.75, 1.0)
    assert provider.calls == [0, 1, 2, 3, 4]
    assert sequence.timestamps == (0.0, 0.25, 0.5, 0.75, 1.0)
    assert provider.calls == [0, 1, 2, 3, 4]


@pytest.mark.parametrize(
    "timestamps, error, message",
    [
        ((0.0,), ValueError, "length"),
        ((0.0, 0.2, 0.1, 0.3, 0.4), ValueError, "nondecreasing"),
        ((0.0, 0.1, np.inf, 0.3, 0.4), ValueError, "finite"),
        ((0.0, 0.1, True, 0.3, 0.4), TypeError, "real"),
        ((0.0, 0.1, "0.2", 0.3, 0.4), TypeError, "real"),
    ],
)
def test_sequence_rejects_invalid_provider_timestamps(timestamps, error, message):
    with pytest.raises(error, match=message):
        _ = Sequence(RecordingProvider(timestamps=timestamps)).timestamps


def test_duration_and_average_fps_cover_empty_single_duplicate_and_regular_times():
    assert Sequence(MemoryFrameProvider([])).duration == 0.0
    assert Sequence(MemoryFrameProvider([])).fps is None
    assert Sequence(MemoryFrameProvider(frames(1))).duration == 0.0
    duplicate = [Frame(0, 1.0, mesh()), Frame(1, 1.0, mesh())]
    assert Sequence(MemoryFrameProvider(duplicate)).fps is None
    sequence = Sequence(RecordingProvider(timestamps=(1.0, 1.25, 1.5, 1.75, 2.0)))
    assert sequence.duration == pytest.approx(1.0)
    assert sequence.fps == pytest.approx(4.0)


@pytest.mark.parametrize(
    "mode, constant",
    [(TopologyMode.FIXED, True), (TopologyMode.CHANGING, False), (TopologyMode.UNKNOWN, None)],
)
def test_topology_declarations(mode, constant):
    provider = MemoryFrameProvider(frames(), topology=mode)
    sequence = Sequence(provider)
    assert sequence.topology is mode
    assert sequence.has_constant_topology is constant


def test_fixed_topology_implies_default_correspondence_flags_but_explicit_values_win():
    fixed = Sequence(MemoryFrameProvider(frames(), topology=TopologyMode.FIXED))
    assert fixed.has_constant_vertex_count is True
    assert fixed.has_vertex_correspondence is True
    explicit = Sequence(MemoryFrameProvider(
        frames(), topology=TopologyMode.CHANGING,
        has_constant_vertex_count=True, has_vertex_correspondence=False,
    ))
    assert explicit.has_constant_vertex_count is True
    assert explicit.has_vertex_correspondence is False


def test_invalid_optional_provider_flag_is_reported_when_consumed():
    provider = RecordingProvider()
    provider.has_vertex_correspondence = "yes"
    sequence = Sequence(provider)
    with pytest.raises(TypeError, match="has_vertex_correspondence"):
        _ = sequence.has_vertex_correspondence


def test_slices_are_lazy_views_and_preserve_source_identity():
    provider = RecordingProvider(timestamps=(0.0, 0.25, 0.5, 0.75, 1.0))
    sequence = Sequence(provider)
    view = sequence[1:5:2]
    assert len(view) == 2
    assert provider.calls == []
    assert [frame.frame_index for frame in view] == [11, 13]
    assert provider.calls == [1, 3]
    assert view.timestamps == (0.25, 0.75)
    assert view.metadata == sequence.metadata
    assert view.topology == sequence.topology


def test_nested_and_reverse_views_map_indices_correctly():
    sequence = Sequence(MemoryFrameProvider(frames()))
    reverse = sequence[::-1]
    assert [frame.frame_index for frame in reverse] == [13, 12, 11, 10]
    assert reverse.timestamps == (0.75, 0.5, 0.25, 0.0)
    assert reverse.duration == pytest.approx(0.75)
    assert reverse.fps == pytest.approx(4.0)
    assert [frame.frame_index for frame in sequence[1:][::2]] == [11, 13]


def test_empty_view_has_zero_duration_and_no_fps():
    view = Sequence(MemoryFrameProvider(frames()))[0:0]
    assert len(view) == 0
    assert view.timestamps == ()
    assert view.duration == 0.0
    assert view.fps is None


def test_close_is_idempotent_and_context_manager_closes_on_errors():
    class Provider(RecordingProvider):
        def __init__(self):
            super().__init__()
            self.close_count = 0
        def close(self):
            self.close_count += 1
    provider = Provider()
    sequence = Sequence(provider)
    with pytest.raises(RuntimeError, match="body"):
        with sequence:
            raise RuntimeError("body")
    sequence.close()
    assert provider.close_count == 1


def test_failed_close_can_be_retried_and_noncallable_close_is_rejected():
    class RetryProvider(RecordingProvider):
        attempts = 0
        def close(self):
            self.attempts += 1
            if self.attempts == 1:
                raise OSError("busy")
    sequence = Sequence(RetryProvider())
    with pytest.raises(OSError, match="busy"):
        sequence.close()
    sequence.close()
    assert sequence._provider.attempts == 2

    provider = RecordingProvider()
    provider.close = "not callable"
    with pytest.raises(TypeError, match="callable"):
        Sequence(provider).close()


def test_closing_a_view_does_not_close_its_parent():
    class Provider(RecordingProvider):
        closed = False
        def close(self):
            self.closed = True
    provider = Provider()
    parent = Sequence(provider)
    parent[:2].close()
    assert provider.closed is False
    parent.close()
    assert provider.closed is True


def test_closed_sequence_rejects_frame_and_timing_access_but_keeps_declarations():
    sequence = Sequence(MemoryFrameProvider(
        frames(), metadata={"capture": "test"}, topology=TopologyMode.FIXED
    ))
    sequence.close()

    assert sequence.closed is True
    assert len(sequence) == 4
    assert sequence.metadata == {"capture": "test"}
    assert sequence.topology is TopologyMode.FIXED
    with pytest.raises(RuntimeError, match="closed"):
        _ = sequence[0]
    with pytest.raises(RuntimeError, match="closed"):
        _ = sequence.timestamps
