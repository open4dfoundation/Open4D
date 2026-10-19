"""Installed extension discovery and the NumPy-only geometry boundary."""

from dataclasses import dataclass
import sys
import numpy as np
import pytest

from open4d import Frame, MemoryFrameProvider, Sequence, PointCloud, GaussianCloud
from open4d.codecs import available_codecs, codec_info, encode_sequence, decode_sequence, CodecError
from open4d.codecs._npz import NumPyZipCodec, REFERENCE_CODECS
from open4d.reconstruction import available_methods
from open4d.reconstruction._registry import get_method

pytestmark = pytest.mark.cpu


@dataclass
class Entry:
    name: str
    group: str
    provider: object
    calls: int = 0

    def load(self):
        self.calls += 1
        if isinstance(self.provider, Exception):
            raise self.provider
        return self.provider


class Entries(list):
    def select(self, *, group):
        return Entries(entry for entry in self if entry.group == group)


@pytest.fixture
def entries(monkeypatch):
    from open4d import _plugins
    installed = Entries()
    monkeypatch.setattr(_plugins.metadata, "entry_points", lambda: installed)
    return installed


def test_core_registry_is_useful_without_research_plugins(entries):
    assert {info.id for info in available_codecs()} == {codec.id for codec in REFERENCE_CODECS}
    with pytest.raises(CodecError, match="pip install ./plugins/research"):
        encode_sequence(None, "missing.o4d", codec="klt")
    assert [info.id for info in available_methods()] == ["rgbd"]
    with pytest.raises(ValueError, match="not installed"):
        get_method("queen")


def test_plugin_discovery_is_lazy_and_uninstallation_is_observed(entries, tmp_path):
    entry = Entry("external", "open4d.codecs", lambda: NumPyZipCodec("external"))
    entries.append(entry)
    assert "external" in {info.id for info in available_codecs()}
    assert entry.calls == 0
    from open4d.codecs._api import _codec
    assert _codec("external", tmp_path / "take.o4d").id == "external"
    assert entry.calls == 1
    _codec("external", tmp_path / "take.o4d")
    assert entry.calls == 1
    entries.clear()
    assert "external" not in {info.id for info in available_codecs()}


def test_selected_plugin_capabilities_are_resolved_without_loading_other_providers(entries):
    selected = Entry("selected", "open4d.codecs", lambda: NumPyZipCodec("selected"))
    unrelated = Entry("unrelated", "open4d.codecs", ImportError("optional CUDA missing"))
    entries.extend([selected, unrelated])
    assert codec_info("selected").representation == "plugin"
    assert codec_info("selected").preserves == ()
    assert selected.calls == unrelated.calls == 0
    assert codec_info("selected", resolve=True).representation == "geometry_sequence"
    assert selected.calls == 1 and unrelated.calls == 0
    assert codec_info("selected", resolve=True).representation == "geometry_sequence"
    assert selected.calls == 1
    # The global discovery contract stays metadata-only, even after selection.
    assert {info.id for info in available_codecs()} >= {"selected", "unrelated"}
    assert unrelated.calls == 0


def test_bad_plugin_does_not_break_core_discovery_or_operations(entries, tmp_path):
    entries.append(Entry("broken", "open4d.codecs", ImportError("optional CUDA missing")))
    assert "broken" in {info.id for info in available_codecs()}
    from open4d.codecs._api import _codec
    with pytest.raises(CodecError, match="optional CUDA missing"):
        _codec("broken", tmp_path / "take.o4d")
    assert _codec("npz", tmp_path / "take.o4d").id == "npz"


def test_provider_cannot_override_builtin_or_misrepresent_id(entries, tmp_path):
    builtin = Entry("npz", "open4d.codecs", ImportError("should never load"))
    mismatch = Entry("wrong", "open4d.codecs", lambda: NumPyZipCodec("npz"))
    entries.extend([builtin, mismatch])
    from open4d.codecs._api import _codec
    assert _codec("npz", tmp_path / "take.o4d").id == "npz"
    assert builtin.calls == 0
    with pytest.raises(CodecError, match="id='wrong'"):
        _codec("wrong", tmp_path / "take.o4d")


def test_duplicate_plugin_names_are_rejected_without_import(entries):
    first = Entry("duplicate", "open4d.codecs", lambda: NumPyZipCodec("duplicate"))
    second = Entry("duplicate", "open4d.codecs", lambda: NumPyZipCodec("duplicate"))
    entries.extend([first, second])
    with pytest.warns(RuntimeWarning, match="duplicate"):
        assert "duplicate" not in {info.id for info in available_codecs()}
    assert first.calls == second.calls == 0


def test_reconstruction_provider_is_loaded_only_when_selected(entries):
    method = lambda source, output=None, **options: (source, output, options)
    entry = Entry("external", "open4d.reconstruction", method)
    entries.append(entry)
    assert {info.id for info in available_methods()} == {"rgbd", "external"}
    assert entry.calls == 0
    assert get_method("external")("input", "output", quality=2) == ("input", "output", {"quality": 2})
    assert entry.calls == 1
    assert get_method("tsdf") is get_method("rgbd")


@pytest.mark.parametrize("codec", REFERENCE_CODECS, ids=lambda codec: codec.id)
def test_all_array_profiles_preserve_point_and_gaussian_sequences(codec, tmp_path):
    points = PointCloud([[0., 1., 2.], [2., 3., 4.]], colors=[[1., 0., 0.], [0., 1., 0.]],
                        normals=[[0., 0., 1.]] * 2, attributes={"labels": np.array([3, 4], dtype=np.int16)})
    splats = GaussianCloud([[1., 2., 3.]], [[.1, .2, .3]], [[1., 1., 0., 0.]], [.7],
                           colors=[[.2, .3, .4]], attributes={"sh": np.zeros((1, 12), np.float32)})
    source = Sequence(MemoryFrameProvider([
        Frame(7, .2, points, {"kind": "points"}), Frame(9, .7, splats, {"kind": "gaussians"})
    ], metadata={"name": "mixed"}))
    artifact = encode_sequence(source, tmp_path / "geometry.o4d", codec=codec.id)
    with decode_sequence(artifact) as decoded:
        assert decoded.timestamps == (.2, .7) and decoded.metadata == source.metadata
        for expected, actual in zip(source, decoded):
            assert type(actual.geometry) is type(expected.geometry)
            assert actual.frame_index == expected.frame_index and actual.metadata == expected.metadata
            for name in ("positions", "colors", "normals", "scales", "rotations", "opacities"):
                if hasattr(expected.geometry, name):
                    np.testing.assert_array_equal(getattr(actual.geometry, name), getattr(expected.geometry, name))
            for name, value in expected.geometry.attributes.items():
                np.testing.assert_array_equal(actual.geometry.attributes[name], value)


def test_external_source_root_can_be_named_independently_and_does_not_change_sys_path(tmp_path, monkeypatch):
    from open4d.codecs._research import research_module
    source = tmp_path / "licensed-backend-checkout"
    source.mkdir()
    (source / "helper.py").write_text("VALUE = 37\n")
    (source / "engine.py").write_text("from .helper import VALUE\n")
    monkeypatch.setenv("OPEN4D_CUSTOM_ROOT", str(source))
    before = tuple(sys.path)
    assert research_module("custom.engine").VALUE == 37
    assert tuple(sys.path) == before


def test_array_v1_mesh_files_remain_decodable(tmp_path):
    import json
    from open4d.core import TriangleMesh
    from open4d.codecs import pack_o4d, unpack_o4d, inspect_o4d
    codec = NumPyZipCodec()
    source = Sequence(MemoryFrameProvider([Frame(2, .5, TriangleMesh(
        [[0., 0., 0.], [1., 0., 0.], [0., 1., 0.]], [[0, 1, 2]]))]))
    artifact = codec.encode(source, tmp_path / "new.o4d")
    native = unpack_o4d(artifact, tmp_path / "native")
    metadata = json.loads((native / "metadata.json").read_text())
    metadata["native"]["profile"] = "npz/1"
    metadata["frames"][0].pop("representation")
    (native / "metadata.json").write_text(json.dumps(metadata))
    legacy = pack_o4d(native, tmp_path / "legacy.o4d")
    assert inspect_o4d(legacy)["native_version"] == 1
    with decode_sequence(legacy) as restored:
        assert type(restored[0].geometry) is TriangleMesh
        np.testing.assert_array_equal(restored[0].geometry.triangles, [[0, 1, 2]])


@pytest.mark.parametrize("rle", (False, True))
def test_array_encoder_enforces_aggregate_budget_and_does_not_publish(tmp_path, monkeypatch, rle):
    from open4d.codecs import _npz
    codec = NumPyZipCodec("rle" if rle else "npz", rle=rle)
    geometry = PointCloud([[0., 0., 0.]], attributes={
        "first": np.zeros((1, 2000), np.uint8), "second": np.zeros((1, 2000), np.uint8),
    })
    sequence = Sequence(MemoryFrameProvider([Frame(0, 0., geometry)]))
    monkeypatch.setattr(_npz, "_MAX_FRAME_BYTES", 3000)
    destination = tmp_path / "bounded.o4d"
    with pytest.raises(CodecError, match="aggregate frame"):
        codec.encode(sequence, destination)
    assert not destination.exists()


@pytest.mark.parametrize("rle", (False, True))
def test_decoder_checks_aggregate_expansion_before_materializing_arrays(tmp_path, monkeypatch, rle):
    from open4d.codecs import _npz
    codec = NumPyZipCodec("rle" if rle else "npz", rle=rle)
    geometry = PointCloud([[0., 0., 0.]], attributes={
        "first": np.zeros((1, 2000), np.uint8), "second": np.zeros((1, 2000), np.uint8),
    })
    sequence = Sequence(MemoryFrameProvider([Frame(0, 0., geometry)]))
    artifact = codec.encode(sequence, tmp_path / "bounded.o4d")
    monkeypatch.setattr(_npz, "_MAX_FRAME_BYTES", 3000)
    monkeypatch.setattr(_npz, "_array_from_bytes", lambda *args: pytest.fail("budget must precede array allocation"))
    with pytest.raises(CodecError, match="aggregate frame"):
        codec.decode(artifact)


def test_array_member_count_is_bounded_on_both_sides(tmp_path, monkeypatch):
    from open4d.codecs import _npz
    codec = NumPyZipCodec()
    geometry = PointCloud([[0., 0., 0.]], attributes={"a": np.zeros(1), "b": np.zeros(1)})
    sequence = Sequence(MemoryFrameProvider([Frame(0, 0., geometry)]))
    artifact = codec.encode(sequence, tmp_path / "many.o4d")
    monkeypatch.setattr(_npz, "_MAX_FRAME_MEMBERS", 2)
    with pytest.raises(CodecError, match="member count"):
        codec.encode(sequence, tmp_path / "rejected.o4d")
    with pytest.raises(CodecError, match="member count"):
        codec.decode(artifact)


def test_aggregate_budget_uses_the_artifact_rle_setting_not_the_registry_default(tmp_path, monkeypatch):
    from open4d.codecs import _npz
    codec = NumPyZipCodec("npz", rle=True)
    geometry = PointCloud([[0., 0., 0.]], attributes={
        "first": np.zeros((1, 2000), np.uint8), "second": np.zeros((1, 2000), np.uint8),
    })
    source = Sequence(MemoryFrameProvider([Frame(0, 0., geometry)]))
    artifact = codec.encode(source, tmp_path / "packed.o4d")
    monkeypatch.setattr(_npz, "_MAX_FRAME_BYTES", 3000)
    monkeypatch.setattr(_npz, "_array_from_bytes", lambda *args: pytest.fail("budget must precede allocation"))
    with pytest.raises(CodecError, match="aggregate frame"):
        NumPyZipCodec().decode(artifact)


def test_cache_identity_accepts_mixed_entry_point_implementations(entries, tmp_path):
    from open4d.codecs._api import _codec

    class VendoredEntry(Entry):
        def __eq__(self, other):
            # Some vendored importlib.metadata versions compare private APIs
            # absent on the standard library's EntryPoint implementation.
            raise AttributeError("'EntryPoint' object has no attribute '_key'")

    first = VendoredEntry("mixed", "open4d.codecs", lambda: NumPyZipCodec("mixed"))
    first.value = "package:factory"
    entries.append(first)
    resolved = _codec("mixed", tmp_path / "take.o4d")
    second = Entry("mixed", "open4d.codecs", AssertionError("same provider must retain the loaded codec"))
    second.value = "package:factory"
    entries[0] = second
    assert _codec("mixed", tmp_path / "take.o4d") is resolved
    assert first.calls == 1 and second.calls == 0


@pytest.mark.parametrize("relative", (
    "open4d/codecs/external/source", "open4d/reconstruction/external",
    "codecs/external", "open4d/codecs/external",
))
def test_default_runtime_supports_current_and_legacy_checkouts(tmp_path, monkeypatch, relative):
    from open4d._research_paths import default_runtime
    runtime = tmp_path / relative
    runtime.mkdir(parents=True)
    monkeypatch.setenv("OPEN4D_RESEARCH_ROOT", str(tmp_path))
    assert default_runtime("external") == runtime


def test_new_runtime_layout_takes_precedence_and_checkout_depth_is_not_assumed(tmp_path, monkeypatch):
    from open4d._research_paths import default_runtime, runtime_checkout
    current = tmp_path / "open4d/codecs/external/source"
    legacy = tmp_path / "codecs/external"
    current.mkdir(parents=True)
    legacy.mkdir(parents=True)
    (tmp_path / "open4d/__init__.py").write_text("")
    monkeypatch.setenv("OPEN4D_RESEARCH_ROOT", str(tmp_path))
    assert default_runtime("external") == current
    assert runtime_checkout(current) == runtime_checkout(legacy) == tmp_path


@pytest.mark.parametrize("relative", ("open4d/codecs/portable/source", "codecs/portable", "open4d/codecs/portable"))
def test_research_module_reads_current_and_legacy_global_roots(tmp_path, monkeypatch, relative):
    from open4d.codecs._research import research_module
    runtime = tmp_path / relative
    runtime.mkdir(parents=True)
    (runtime / "engine.py").write_text("VALUE = 83\n")
    monkeypatch.setenv("OPEN4D_RESEARCH_ROOT", str(tmp_path))
    assert research_module("portable.engine").VALUE == 83
