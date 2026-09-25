from __future__ import annotations

import numpy as np
import pytest

pytest.importorskip("pxr")

import open4d
from open4d import Frame, MemoryFrameProvider, Sequence, TopologyMode, TriangleMesh
from open4d.io import (
    EncodeError,
    MissingDependencyError,
    available_formats,
    inspect_sequence,
    open_sequence,
    write_sequence,
)

pytestmark = pytest.mark.cpu


def test_usd_preserves_user_metadata_and_large_frame_indices(tmp_path):
    geometry = TriangleMesh(np.zeros((1, 3), dtype=np.float32), np.empty((0, 3), dtype=np.uint32))
    source = Sequence(MemoryFrameProvider([Frame(2**40, 0, geometry)],
                                          metadata={"source": "camera1", "name": "capture"}))
    with open_sequence(write_sequence(source, tmp_path / "large.usda")) as decoded:
        assert decoded[0].frame_index == 2**40
        assert decoded.metadata["source"] == "camera1"
        assert decoded.metadata["name"] == "capture"
    too_large = Sequence(MemoryFrameProvider([Frame(2**63, 0, geometry)]))
    with pytest.raises(EncodeError, match="64-bit"):
        write_sequence(too_large, tmp_path / "too-large.usda")


def test_generic_usd_keeps_motion_in_parent_transforms(tmp_path):
    from pxr import Usd, UsdGeom

    path = tmp_path / "transform-animation.usda"
    stage = Usd.Stage.CreateNew(str(path))
    stage.SetTimeCodesPerSecond(10)
    parent = UsdGeom.Xform.Define(stage, "/Model")
    translation = parent.AddTranslateOp()
    translation.Set((0, 0, 0), 0)
    translation.Set((10, 0, 0), 10)
    mesh = UsdGeom.Mesh.Define(stage, "/Model/Mesh")
    mesh.CreatePointsAttr([(0, 0, 0), (1, 0, 0), (0, 1, 0)])
    mesh.CreateFaceVertexCountsAttr([3])
    mesh.CreateFaceVertexIndicesAttr([0, 1, 2])
    stage.GetRootLayer().Save()

    with open_sequence(path) as sequence:
        assert sequence.timestamps == (0, 1)
        np.testing.assert_array_equal(sequence[0].geometry.positions[0], [0, 0, 0])
        np.testing.assert_array_equal(sequence[1].geometry.positions[0], [10, 0, 0])


def test_generic_usd_does_not_claim_constant_point_count_or_identity(tmp_path):
    from pxr import Usd, UsdGeom

    path = tmp_path / "points.usda"
    stage = Usd.Stage.CreateNew(str(path))
    points = UsdGeom.Points.Define(stage, "/Points").CreatePointsAttr()
    points.Set([(0, 0, 0)], 0)
    points.Set([(0, 0, 0), (1, 0, 0)], 1)
    stage.GetRootLayer().Save()

    with open_sequence(path) as sequence:
        assert [len(frame.geometry.positions) for frame in sequence] == [1, 2]
        assert sequence.has_constant_vertex_count is None
        assert sequence.has_vertex_correspondence is None


def test_generic_usd_preserves_constant_color_opacity_and_vertex_normals(tmp_path):
    from pxr import Sdf, Usd, UsdGeom

    path = tmp_path / "attributes.usda"
    stage = Usd.Stage.CreateNew(str(path))
    mesh = UsdGeom.Mesh.Define(stage, "/Mesh")
    mesh.CreatePointsAttr([(0, 0, 0), (1, 0, 0), (0, 1, 0)])
    mesh.CreateFaceVertexCountsAttr([3])
    mesh.CreateFaceVertexIndicesAttr([0, 1, 2])
    mesh.CreateNormalsAttr([(0, 0, 1)] * 3)
    mesh.SetNormalsInterpolation("vertex")
    primvars = UsdGeom.PrimvarsAPI(mesh.GetPrim())
    primvars.CreatePrimvar("displayColor", Sdf.ValueTypeNames.Color3fArray, "constant").Set([(1, 0, 0)])
    primvars.CreatePrimvar("displayOpacity", Sdf.ValueTypeNames.FloatArray, "constant").Set([0.25])
    stage.GetRootLayer().Save()

    with open_sequence(path) as sequence:
        np.testing.assert_array_equal(sequence[0].geometry.colors, [[1, 0, 0, 0.25]] * 3)
        np.testing.assert_array_equal(sequence[0].geometry.normals, [[0, 0, 1]] * 3)


def test_usd_does_not_overwrite_a_concurrently_created_file(tmp_path, monkeypatch):
    from open4d.io import _usd

    destination = tmp_path / "concurrent.usda"
    original = _usd._author_stage

    def competing_writer(*args, **kwargs):
        original(*args, **kwargs)
        destination.write_bytes(b"other writer")

    monkeypatch.setattr(_usd, "_author_stage", competing_writer)
    with pytest.raises(FileExistsError):
        write_sequence(rich_sequence(), destination)
    assert destination.read_bytes() == b"other writer"


def rich_sequence() -> Sequence:
    first = TriangleMesh(
        positions=np.array([[0, 0, 0], [1, 0, 0], [0, 1, 0]], dtype=np.float32),
        triangles=[[0, 1, 2]],
        colors=[[1, 0, 0, 0.25], [0, 1, 0, 0.5], [0, 0, 1, 0.75]],
        normals=np.array([[0, 0, 1]] * 3, dtype=np.float32),
        texture_coordinates=np.array([[0, 0], [1, 0], [0, 1]], dtype=np.float32),
        attributes={
            "weights": np.arange(6, dtype=np.float32).reshape(3, 2),
            "labels": np.array([4, 5, 6], dtype=np.int32),
            "selected": np.array([True], dtype=np.bool_),
        },
    )
    second = TriangleMesh(
        positions=np.array(
            [[0, 0, 0], [2, 0, 0], [0, 2, 0], [0, 0, 2]], dtype=np.float32
        ),
        triangles=[[0, 1, 2], [0, 2, 3]],
        normals=np.array([[0, 0, 1]] * 4, dtype=np.float32),
        texture_coordinates=np.array(
            [[[0, 0], [1, 0], [0, 1]], [[0, 0], [0, 1], [1, 1]]],
            dtype=np.float32,
        ),
        attributes={
            "weights": np.arange(8, dtype=np.float32).reshape(4, 2),
            "labels": np.array([7, 8, 9, 10], dtype=np.int32),
            "visible": np.array([True, False, True, False], dtype=np.bool_),
        },
    )
    return Sequence(MemoryFrameProvider(
        [
            Frame(41, 5.0, first, {"camera": "left", "quality": 0.5}),
            Frame(43, 4.0, second, {"camera": "right", "tags": ["a", "b"]}),
        ],
        metadata={"capture": "rafa", "nested": {"take": 2}},
        topology=TopologyMode.CHANGING,
        has_constant_vertex_count=False,
        has_vertex_correspondence=False,
        allow_nonmonotonic_timestamps=True,
    ))


def assert_rich_round_trip(expected: Sequence, actual: Sequence) -> None:
    assert actual.metadata["capture"] == "rafa"
    assert actual.metadata["nested"] == {"take": 2}
    assert actual.topology is TopologyMode.CHANGING
    assert actual.has_constant_vertex_count is False
    assert actual.has_vertex_correspondence is False
    assert actual.allow_nonmonotonic_timestamps is True
    assert actual.timestamps == (5.0, 4.0)
    for left, right in zip(expected, actual, strict=True):
        assert right.frame_index == left.frame_index
        assert right.timestamp == left.timestamp
        assert right.metadata == left.metadata
        for name in (
            "positions", "triangles", "colors", "normals", "texture_coordinates"
        ):
            expected_value = getattr(left.geometry, name)
            actual_value = getattr(right.geometry, name)
            if expected_value is None:
                assert actual_value is None
            else:
                np.testing.assert_array_equal(actual_value, expected_value)
        assert set(right.geometry.attributes) == set(left.geometry.attributes)
        for name, value in left.geometry.attributes.items():
            np.testing.assert_array_equal(right.geometry.attributes[name], value)


@pytest.mark.parametrize("suffix", (".usda", ".usdc", ".usdz"))
def test_public_usd_round_trip_preserves_the_complete_model(tmp_path, suffix):
    source = rich_sequence()
    destination = tmp_path / f"capture{suffix}"

    output = open4d.save(source, destination, fps=30, up_axis="y")
    info = inspect_sequence(output)
    decoded = open4d.load(output)

    assert output == destination.absolute()
    assert output.is_file()
    assert info.storage == "container"
    assert info.format == "usd"
    assert info.frame_count == 2
    assert info.timing_source == "container"
    assert decoded.metadata["up_axis"] == "y"
    assert_rich_round_trip(source, decoded)
    decoded.close()


def test_public_usd_load_is_lazy(tmp_path, monkeypatch):
    path = open4d.save(rich_sequence(), tmp_path / "capture.usdc")
    calls = []
    from open4d.io import _usd

    real = _usd.UsdSequenceProvider.get_frame
    monkeypatch.setattr(
        _usd.UsdSequenceProvider,
        "get_frame",
        lambda self, index: (calls.append(index), real(self, index))[1],
    )

    decoded = open4d.load(path)
    assert calls == []
    assert len(decoded) == 2
    assert decoded.timestamps == (5.0, 4.0)
    assert calls == []
    _ = decoded[1]
    assert calls == [1]
    decoded.close()


def test_usd_empty_geometry_omits_extent_and_round_trips(tmp_path):
    empty = TriangleMesh(
        np.empty((0, 3), dtype=np.float32),
        np.empty((0, 3), dtype=np.uint32),
    )
    source = Sequence(MemoryFrameProvider([Frame(0, 0.0, empty)]))

    decoded = open4d.load(open4d.save(source, tmp_path / "empty.usdc"))

    assert decoded[0].geometry.positions.shape == (0, 3)
    assert decoded[0].geometry.triangles.shape == (0, 3)
    decoded.close()


def test_usd_empty_frame_clears_the_previous_extent_sample(tmp_path):
    from pxr import Usd, UsdGeom

    positions = np.array(
        [[0, 0, 0], [1, 0, 0], [0, 1, 0]], dtype=np.float32
    )
    triangles = np.array([[0, 1, 2]], dtype=np.uint32)
    empty_positions = np.empty((0, 3), dtype=np.float32)
    empty_triangles = np.empty((0, 3), dtype=np.uint32)
    source = Sequence(MemoryFrameProvider([
        Frame(0, 0.0, TriangleMesh(positions, triangles)),
        Frame(1, 1.0, TriangleMesh(empty_positions, empty_triangles)),
    ]))

    path = open4d.save(source, tmp_path / "mixed-empty.usda")
    stage = Usd.Stage.Open(str(path))
    mesh = UsdGeom.Mesh(stage.GetPrimAtPath("/Open4D/Sequence"))
    extent = mesh.GetExtentAttr()

    assert extent.GetTimeSamples() == [0.0, 1.0]
    assert len(extent.Get(0)) == 2
    assert len(extent.Get(1)) == 0


def test_usd_write_failure_preserves_an_existing_destination(tmp_path):
    destination = tmp_path / "capture.usdc"
    destination.write_bytes(b"keep me")
    bad = Sequence(MemoryFrameProvider(
        tuple(rich_sequence()), metadata={"invalid": object()},
        allow_nonmonotonic_timestamps=True,
    ))

    with pytest.raises(EncodeError, match="serializable"):
        open4d.save(bad, destination, overwrite=True)

    assert destination.read_bytes() == b"keep me"


def test_module_level_usd_apis_delegate_to_the_public_backend(tmp_path):
    destination = write_sequence(
        rich_sequence(), tmp_path / "capture.usdc", options={"up_axis": "z"}
    )

    decoded = open_sequence(destination)

    assert decoded.metadata["up_axis"] == "z"
    assert_rich_round_trip(rich_sequence(), decoded)
    decoded.close()


def test_generic_time_sampled_usd_is_a_lazy_sequence_import(tmp_path):
    from pxr import Usd, UsdGeom, Vt

    path = tmp_path / "third-party.usda"
    stage = Usd.Stage.CreateNew(str(path))
    stage.SetTimeCodesPerSecond(24)
    mesh = UsdGeom.Mesh.Define(stage, "/World/Mesh")
    mesh.CreateFaceVertexCountsAttr().Set(Vt.IntArray([3]))
    mesh.CreateFaceVertexIndicesAttr().Set(Vt.IntArray([0, 1, 2]))
    points = mesh.CreatePointsAttr()
    points.Set(Vt.Vec3fArray([(0, 0, 0), (1, 0, 0), (0, 1, 0)]), 0)
    points.Set(Vt.Vec3fArray([(1, 0, 0), (2, 0, 0), (1, 1, 0)]), 1)
    stage.GetRootLayer().Save()

    sequence = open4d.load(path)

    assert len(sequence) == 2
    assert sequence.timestamps == (0.0, 1 / 24)
    np.testing.assert_array_equal(sequence[1].geometry.positions[0], [1, 0, 0])
    sequence.close()


def test_generic_usd_discovers_frames_from_animated_topology(tmp_path):
    from pxr import Usd, UsdGeom, Vt

    path = tmp_path / "animated-topology.usda"
    stage = Usd.Stage.CreateNew(str(path))
    mesh = UsdGeom.Mesh.Define(stage, "/World/Mesh")
    mesh.CreatePointsAttr().Set(
        Vt.Vec3fArray([(0, 0, 0), (1, 0, 0), (0, 1, 0)])
    )
    counts = mesh.CreateFaceVertexCountsAttr()
    indices = mesh.CreateFaceVertexIndicesAttr()
    counts.Set(Vt.IntArray([]), 0)
    indices.Set(Vt.IntArray([]), 0)
    counts.Set(Vt.IntArray([3]), 1)
    indices.Set(Vt.IntArray([0, 1, 2]), 1)
    stage.GetRootLayer().Save()

    sequence = open4d.load(path)

    assert len(sequence) == 2
    assert len(sequence[0].geometry.triangles) == 0
    np.testing.assert_array_equal(sequence[1].geometry.triangles, [[0, 1, 2]])
    sequence.close()


def test_generic_usd_discovers_frames_from_animated_colors(tmp_path):
    from pxr import Sdf, Usd, UsdGeom, Vt

    path = tmp_path / "animated-colors.usda"
    stage = Usd.Stage.CreateNew(str(path))
    points = UsdGeom.Points.Define(stage, "/World/Points")
    points.CreatePointsAttr().Set(
        Vt.Vec3fArray([(0, 0, 0), (1, 0, 0), (0, 1, 0)])
    )
    colors = UsdGeom.PrimvarsAPI(points.GetPrim()).CreatePrimvar(
        "displayColor", Sdf.ValueTypeNames.Color3fArray, UsdGeom.Tokens.vertex
    )
    colors.Set(Vt.Vec3fArray([(1, 0, 0)] * 3), 0)
    colors.Set(Vt.Vec3fArray([(0, 1, 0)] * 3), 1)
    stage.GetRootLayer().Save()

    sequence = open4d.load(path)

    assert len(sequence) == 2
    np.testing.assert_array_equal(sequence[0].geometry.colors, [[1, 0, 0]] * 3)
    np.testing.assert_array_equal(sequence[1].geometry.colors, [[0, 1, 0]] * 3)
    sequence.close()


def test_usd_export_clears_standard_optional_attributes_when_absent(tmp_path):
    from pxr import Usd, UsdGeom

    positions = np.array(
        [[0, 0, 0], [1, 0, 0], [0, 1, 0]], dtype=np.float32
    )
    triangles = np.array([[0, 1, 2]], dtype=np.uint32)
    rgba = np.array(
        [[1, 0, 0, 0.25], [0, 1, 0, 0.5], [0, 0, 1, 0.75]],
        dtype=np.float32,
    )
    rgb = rgba[:, :3]
    normals = np.array([[0, 0, 1]] * 3, dtype=np.float32)
    source = Sequence(MemoryFrameProvider([
        Frame(0, 0.0, TriangleMesh(
            positions, triangles, colors=rgba, normals=normals
        )),
        Frame(1, 1.0, TriangleMesh(positions, triangles, colors=rgb)),
        Frame(2, 2.0, TriangleMesh(positions, triangles)),
    ]))

    path = open4d.save(source, tmp_path / "optional-streams.usda")
    stage = Usd.Stage.Open(str(path))
    mesh = UsdGeom.Mesh(stage.GetPrimAtPath("/Open4D/Sequence"))
    primvars = UsdGeom.PrimvarsAPI(mesh.GetPrim())
    colors = primvars.GetPrimvar("displayColor")
    opacity = primvars.GetPrimvar("displayOpacity")
    exported_normals = mesh.GetNormalsAttr()

    assert len(colors.Get(1)) == 3
    assert len(opacity.Get(1)) == 0
    assert len(exported_normals.Get(1)) == 0
    assert len(colors.Get(2)) == 0
    assert len(opacity.Get(2)) == 0
    assert len(exported_normals.Get(2)) == 0


def test_reader_accepts_the_prototype_open4d_usd_layout(tmp_path):
    from pxr import Sdf, Usd, UsdGeom, Vt

    path = tmp_path / "prototype.usda"
    stage = Usd.Stage.CreateNew(str(path))
    mesh = UsdGeom.Mesh.Define(stage, "/Open4D/Sequence")
    mesh.CreateFaceVertexCountsAttr().Set(Vt.IntArray([3]))
    mesh.CreateFaceVertexIndicesAttr().Set(Vt.IntArray([0, 1, 2]))
    mesh.CreatePointsAttr().Set(
        Vt.Vec3fArray([(0, 0, 0), (1, 0, 0), (0, 1, 0)]), 0
    )
    prim = mesh.GetPrim()
    prim.CreateAttribute("open4d:frameIndex", Sdf.ValueTypeNames.Int).Set(17, 0)
    prim.CreateAttribute("open4d:timestamp", Sdf.ValueTypeNames.Double).Set(2.5, 0)
    prim.CreateAttribute("open4d:keyFrame", Sdf.ValueTypeNames.Bool).Set(True, 0)
    stage.GetRootLayer().customLayerData = {
        "open4d": {
            "version": 1,
            "source": "prototype-capture",
            "key_frame_indices": Vt.IntArray([0]),
        }
    }
    stage.GetRootLayer().Save()

    sequence = open4d.load(path)
    frame = sequence[0]

    assert sequence.metadata["version"] == 1
    assert sequence.metadata["source"] == "prototype-capture"
    assert sequence.metadata["key_frame_indices"] == [0]
    assert frame.frame_index == 17
    assert frame.timestamp == 2.5
    assert frame.metadata["keyFrame"] is True
    sequence.close()


def test_usd_format_is_advertised_as_one_sequence_container_family():
    usd = next(info for info in available_formats() if info.id == "usd")

    assert usd.suffixes == (".usd", ".usda", ".usdc", ".usdz")
    assert usd.dependency_extra == "usd"


def test_missing_openusd_dependency_has_an_exact_install_hint(monkeypatch):
    import builtins
    from open4d.io import _usd

    real_import = builtins.__import__

    def without_pxr(name, *args, **kwargs):
        if name == "pxr":
            raise ImportError("not installed")
        return real_import(name, *args, **kwargs)

    monkeypatch.setattr(builtins, "__import__", without_pxr)

    with pytest.raises(MissingDependencyError, match=r"pip install 'open4d\[usd\]'"):
        _usd._pxr()
