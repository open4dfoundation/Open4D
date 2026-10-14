"""Contract tests for the dependency axis: what a seek actually costs."""

from __future__ import annotations

import numpy as np
import pytest

from open4d import (
    Dependency,
    DependencyMode,
    Frame,
    Sequence,
    TriangleMesh,
)

pytestmark = pytest.mark.cpu


def mesh() -> TriangleMesh:
    return TriangleMesh(
        np.asarray([[0, 0, 0], [1, 0, 0], [0, 1, 0]], dtype=np.float32),
        np.asarray([[0, 1, 2]], dtype=np.uint32),
    )


class StubProvider:
    """A provider that declares a dependency. Frames themselves are irrelevant here.

    Deliberately not `MemoryFrameProvider`: that holds already-decoded frames, so
    declaring anything but INDEPENDENT on it would be a lie.
    """

    def __init__(self, count: int, dependency: Dependency | None = None) -> None:
        self._count = count
        self.dependency = dependency

    @property
    def frame_count(self) -> int:
        return self._count

    def get_frame(self, index: int) -> Frame:
        return Frame(index, float(index), mesh())


# -------------------------------------------------------------- validation ---


def test_default_is_independent():
    assert Dependency().mode is DependencyMode.INDEPENDENT
    assert Dependency().key_frames == ()


def test_gop_requires_key_frames():
    with pytest.raises(ValueError, match="at least one key frame"):
        Dependency(mode=DependencyMode.GOP)


def test_gop_requires_frame_zero_to_be_a_key():
    with pytest.raises(ValueError, match="no entry point"):
        Dependency(mode=DependencyMode.GOP, key_frames=(4, 8))


def test_key_frames_are_sorted_and_deduplicated():
    assert Dependency(
        mode=DependencyMode.GOP, key_frames=(8, 0, 4, 4)
    ).key_frames == (0, 4, 8)


@pytest.mark.parametrize(
    "mode", [DependencyMode.INDEPENDENT, DependencyMode.SEQUENTIAL]
)
def test_key_frames_are_rejected_where_they_mean_nothing(mode):
    with pytest.raises(ValueError, match="meaningless"):
        Dependency(mode=mode, key_frames=(0,))


def test_mode_must_be_a_dependency_mode():
    with pytest.raises(TypeError, match="DependencyMode"):
        Dependency(mode="gop")


def test_key_frames_reject_negatives_and_bools():
    with pytest.raises(ValueError, match="nonnegative"):
        Dependency(mode=DependencyMode.GOP, key_frames=(0, -1))
    with pytest.raises(TypeError, match="integers"):
        Dependency(mode=DependencyMode.GOP, key_frames=(0, True))


# ------------------------------------------------------------------ key_for ---


def test_key_for_finds_the_group_a_frame_belongs_to():
    dependency = Dependency(mode=DependencyMode.GOP, key_frames=(0, 4, 8))
    assert [dependency.key_for(i) for i in range(10)] == [
        0, 0, 0, 0, 4, 4, 4, 4, 8, 8
    ]


def test_key_for_is_none_when_no_key_is_involved():
    assert Dependency().key_for(3) is None
    assert Dependency(mode=DependencyMode.SEQUENTIAL).key_for(3) is None


# -------------------------------------------------------------------- chain ---


def test_independent_frames_decode_alone():
    assert Dependency().chain(7) == (7,)


def test_independent_ignores_decoder_position():
    """It carries no state, so caching is the consumer's business, not the model's."""
    assert Dependency().chain(7, decoded=7) == (7,)
    assert Dependency().chain(7, decoded=6) == (7,)


def test_gop_decodes_from_its_key_frame():
    dependency = Dependency(mode=DependencyMode.GOP, key_frames=(0, 4, 8))
    assert dependency.chain(6) == (4, 5, 6)
    assert dependency.chain(4) == (4,)
    assert dependency.chain(0) == (0,)


def test_gop_reuses_decoder_state_on_a_forward_seek_inside_the_group():
    dependency = Dependency(mode=DependencyMode.GOP, key_frames=(0, 4, 8))
    assert dependency.chain(6, decoded=5) == (6,)
    assert dependency.chain(7, decoded=4) == (5, 6, 7)


def test_gop_restarts_at_the_key_when_seeking_backwards():
    dependency = Dependency(mode=DependencyMode.GOP, key_frames=(0, 4, 8))
    assert dependency.chain(5, decoded=7) == (4, 5)


def test_gop_ignores_state_from_another_group():
    dependency = Dependency(mode=DependencyMode.GOP, key_frames=(0, 4, 8))
    assert dependency.chain(9, decoded=2) == (8, 9)


def test_sequential_replays_from_zero_without_state():
    dependency = Dependency(mode=DependencyMode.SEQUENTIAL)
    assert dependency.chain(3) == (0, 1, 2, 3)


def test_sequential_reuses_state_going_forward():
    dependency = Dependency(mode=DependencyMode.SEQUENTIAL)
    assert dependency.chain(5, decoded=3) == (4, 5)


def test_sequential_backward_seek_is_a_full_replay():
    """The asymmetry that justifies the whole model: ReRF's stream cannot rewind."""
    dependency = Dependency(mode=DependencyMode.SEQUENTIAL)
    assert dependency.chain(2, decoded=9) == (0, 1, 2)


@pytest.mark.parametrize("mode", [DependencyMode.GOP, DependencyMode.SEQUENTIAL])
def test_a_frame_already_decoded_needs_no_work(mode):
    keys = (0,) if mode is DependencyMode.GOP else ()
    assert Dependency(mode=mode, key_frames=keys).chain(5, decoded=5) == ()


def test_chain_rejects_negative_ordinals():
    with pytest.raises(ValueError, match="nonnegative"):
        Dependency().chain(-1)
    with pytest.raises(ValueError, match="nonnegative"):
        Dependency().chain(1, decoded=-1)


# ----------------------------------------------------------------- Sequence ---


def test_sequence_defaults_to_independent_for_existing_providers():
    """Every provider that predates this axis keeps working, unchanged."""
    assert Sequence(StubProvider(3)).dependency.mode is DependencyMode.INDEPENDENT


def test_sequence_surfaces_a_declared_dependency():
    declared = Dependency(mode=DependencyMode.GOP, key_frames=(0, 2))
    assert Sequence(StubProvider(4, declared)).dependency == declared


def test_sequence_rejects_a_non_dependency_declaration():
    with pytest.raises(TypeError, match="must be a Dependency"):
        Sequence(StubProvider(2, "gop"))


def test_decode_chain_delegates_and_bounds_check():
    sequence = Sequence(
        StubProvider(8, Dependency(mode=DependencyMode.GOP, key_frames=(0, 4)))
    )
    assert sequence.decode_chain(6) == (4, 5, 6)
    assert sequence.decode_chain(6, decoded=5) == (6,)
    with pytest.raises(IndexError):
        sequence.decode_chain(8)


def test_random_access_still_works_on_a_dependent_sequence():
    """`dependency` is advisory: the provider replays internally to honour indexing."""
    sequence = Sequence(
        StubProvider(6, Dependency(mode=DependencyMode.SEQUENTIAL))
    )
    assert sequence[4].frame_index == 4


def test_a_view_of_a_dependent_sequence_declares_the_safe_mode():
    """Key ordinals are the parent's and cannot be rebased onto a slice."""
    sequence = Sequence(
        StubProvider(9, Dependency(mode=DependencyMode.GOP, key_frames=(0, 4, 8)))
    )
    view = sequence[2:7]
    assert view.dependency.mode is DependencyMode.SEQUENTIAL
    assert view.dependency.key_frames == ()


def test_a_view_of_an_independent_sequence_stays_independent():
    view = Sequence(StubProvider(5))[1:4]
    assert view.dependency.mode is DependencyMode.INDEPENDENT
