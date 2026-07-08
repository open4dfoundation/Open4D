"""Tests for the Open4D package facade and the core MeshSequence prototype.

Runs two ways:

    python tests/test_core_mesh_sequence.py     # standalone, no pytest needed
    pytest tests/test_core_mesh_sequence.py     # if pytest is installed

The standalone runner discovers every top-level ``test_*`` function, executes
it, and prints a PASS/FAIL summary.
"""
import os
import sys
import tempfile

import numpy as np

# Make the repo root importable when run as a plain script (tests/ is on
# sys.path[0], the repo root is its parent). Harmless if open4d is installed.
_REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)


# --------------------------------------------------------------------------- #
# Small fixtures (a triangular prism that translates over 3 frames).
# --------------------------------------------------------------------------- #
def _prism_faces():
    return np.array(
        [[0, 1, 2], [3, 5, 4], [0, 3, 4], [0, 4, 1],
         [1, 4, 5], [1, 5, 2], [2, 5, 3], [2, 3, 0]],
        dtype=np.uint32,
    )


def _prism_vertices(offset):
    base = np.array(
        [[0, 0, 0], [1, 0, 0], [0.5, 1, 0],
         [0, 0, 1], [1, 0, 1], [0.5, 1, 1]],
        dtype=np.float64,   # deliberately float64 to check dtype coercion
    )
    return base + np.array([offset, 0, 0], dtype=np.float64)


def _make_sequence():
    from open4d.core import MeshSequence
    faces = _prism_faces()
    verts = [_prism_vertices(t) for t in range(3)]
    return MeshSequence.from_frames(verts, faces, timestamps=[0.0, 0.5, 1.0], name="prism")


# --------------------------------------------------------------------------- #
# 1. The package is importable as a module.
# --------------------------------------------------------------------------- #
def test_package_imports_as_module():
    import open4d as o4d
    assert o4d.__version__
    # importing the package must not eagerly pull optional/heavy deps
    for heavy in ("PyQt6", "torch", "DracoPy", "open4d.core.tsmc"):
        assert heavy not in sys.modules, f"{heavy} was imported eagerly"


def test_namespace_routing():
    import open4d as o4d
    # always-importable subpackages (numpy-only or docstring-only)
    for sub in ("io", "metrics", "modules", "core", "tools"):
        assert getattr(o4d, sub).__name__ == f"open4d.{sub}"
    # hoisted top-level symbols resolve to the same objects as their submodule
    assert o4d.MeshSequence is o4d.core.MeshSequence
    assert o4d.O4DMeshReader is o4d.io.O4DMeshReader


def test_player_subpackage_gui_guard():
    """o4d.player imports when the GUI stack is present, else errors clearly."""
    import open4d as o4d
    try:
        assert o4d.player.__name__ == "open4d.player"
    except ImportError as exc:
        assert "PyQt6" in str(exc)  # guarded, actionable message


def test_unknown_attribute_raises():
    import open4d as o4d
    try:
        o4d.definitely_not_a_thing
    except AttributeError:
        return
    raise AssertionError("expected AttributeError for unknown attribute")


# --------------------------------------------------------------------------- #
# 2. MeshSequence — the 4D contract.
# --------------------------------------------------------------------------- #
def test_build_and_length():
    seq = _make_sequence()
    assert len(seq) == 3
    assert seq.num_frames == 3
    assert seq.name == "prism"


def test_frame_layout_and_dtypes():
    seq = _make_sequence()
    frame = seq[0]
    from open4d.core import MeshFrame
    assert isinstance(frame, MeshFrame)
    # contract: float32 (N,3) C-contiguous verts, uint32 (M,3) C-contiguous faces
    assert frame.vertices.dtype == np.float32
    assert frame.faces.dtype == np.uint32
    assert frame.vertices.shape == (6, 3)
    assert frame.faces.shape == (8, 3)
    assert frame.vertices.flags["C_CONTIGUOUS"]
    assert frame.faces.flags["C_CONTIGUOUS"]
    assert frame.num_vertices == 6 and frame.num_faces == 8


def test_indexing_iteration_and_slicing():
    seq = _make_sequence()
    # negative index
    assert seq[-1].index == 2
    # iteration yields frames in order with correct indices
    assert [f.index for f in seq] == [0, 1, 2]
    # slice returns a new MeshSequence
    from open4d.core import MeshSequence
    sub = seq[1:]
    assert isinstance(sub, MeshSequence)
    assert len(sub) == 2
    assert np.isclose(sub[0].timestamp, 0.5)


def test_out_of_range_raises():
    seq = _make_sequence()
    try:
        seq[99]
    except IndexError:
        return
    raise AssertionError("expected IndexError")


def test_timestamps_and_duration():
    seq = _make_sequence()
    assert np.allclose(seq.timestamps, [0.0, 0.5, 1.0])
    assert np.isclose(seq.duration, 1.0)


def test_topology_constant_and_shared_faces():
    seq = _make_sequence()
    assert seq.is_topology_constant()
    assert np.array_equal(seq.faces, _prism_faces())


def test_varying_topology_detected():
    from open4d.core import MeshSequence
    seq = MeshSequence()
    seq.append(_prism_vertices(0), _prism_faces())
    seq.append(_prism_vertices(1), _prism_faces()[:-1])  # one fewer face -> topology changed
    assert not seq.is_topology_constant()
    try:
        _ = seq.faces
    except ValueError:
        return
    raise AssertionError("expected ValueError from .faces on varying topology")


def test_bad_shapes_rejected():
    from open4d.core import MeshSequence
    seq = MeshSequence()
    for bad in [np.zeros((5, 2)), np.zeros((5,)), np.zeros((5, 4))]:
        try:
            seq.append(bad, _prism_faces())
        except ValueError:
            continue
        raise AssertionError(f"bad vertex shape {bad.shape} should have been rejected")


def test_default_timestamps_are_frame_indices():
    from open4d.core import MeshSequence
    seq = MeshSequence()
    seq.append(_prism_vertices(0), _prism_faces())
    seq.append(_prism_vertices(1), _prism_faces())
    assert np.allclose(seq.timestamps, [0.0, 1.0])


def test_custom_backend_is_swappable():
    """The point of the FrameStore seam: a different backend, same API."""
    from open4d.core import MeshSequence, FrameStore

    class DictStore(FrameStore):
        def __init__(self):
            self._d = {}
            self._n = 0
        def __len__(self):
            return self._n
        def append(self, vertices, faces, timestamp):
            self._d[self._n] = (vertices, faces, timestamp)
            self._n += 1
        def vertices(self, i):
            return self._d[i][0]
        def faces(self, i):
            return self._d[i][1]
        def timestamp(self, i):
            return self._d[i][2]

    seq = MeshSequence(store=DictStore())
    seq.append(_prism_vertices(0), _prism_faces(), 0.0)
    seq.append(_prism_vertices(1), _prism_faces(), 0.25)
    assert len(seq) == 2
    assert np.isclose(seq[1].timestamp, 0.25)
    assert seq[1].vertices.dtype == np.float32  # validation still applied


def test_o4d_round_trip():
    """MeshSequence <-> .o4d container preserves geometry and timestamps."""
    from open4d.core import MeshSequence
    seq = _make_sequence()
    path = os.path.join(tempfile.mkdtemp(), "prism.o4d")
    seq.to_o4d(path)
    assert os.path.getsize(path) > 0

    loaded = MeshSequence.from_o4d(path)
    assert len(loaded) == len(seq)
    assert np.allclose(loaded.timestamps, seq.timestamps)
    for a, b in zip(seq, loaded):
        assert np.allclose(a.vertices, b.vertices)
        assert np.array_equal(a.faces, b.faces)


# --------------------------------------------------------------------------- #
# Standalone runner (used when pytest is not installed).
# --------------------------------------------------------------------------- #
def _run_standalone():
    tests = sorted(
        (name, obj)
        for name, obj in globals().items()
        if name.startswith("test_") and callable(obj)
    )
    passed = failed = 0
    for name, fn in tests:
        try:
            fn()
        except Exception as exc:  # noqa: BLE001
            failed += 1
            print(f"FAIL  {name}: {type(exc).__name__}: {exc}")
        else:
            passed += 1
            print(f"ok    {name}")
    print(f"\n{passed} passed, {failed} failed out of {len(tests)} tests")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(_run_standalone())
