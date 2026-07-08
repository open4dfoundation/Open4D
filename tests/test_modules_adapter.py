"""Tests for the open4d.modules compression-codec adapter layer.

Fast tests only: they exercise the registry, capability probing, the dry-run
stage plans, and the CompressionResult contract. They do NOT run the heavy
pipelines (N4MC training / TSMC / TVMC) — see scripts/run_n4mc_adapter.py for a
real end-to-end N4MC run through the same API.

Runs standalone (`python tests/test_modules_adapter.py`) or under pytest.
"""
import os
import sys

_REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)


def test_importing_modules_is_lazy():
    """Touching the codec API must not import heavy deps (torch/open3d/pcu).

    Run in a clean subprocess: heavy deps get imported by other tests'
    ``available()`` calls, so an in-process check would be contaminated.
    """
    import subprocess
    code = (
        "import sys; import open4d as o4d; o4d.modules.list_codecs();"
        "bad=[m for m in ('torch','open3d','point_cloud_utils') if m in sys.modules];"
        "print('EAGER:'+','.join(bad)); sys.exit(1 if bad else 0)"
    )
    proc = subprocess.run(
        [sys.executable, "-c", code], cwd=_REPO_ROOT, capture_output=True, text=True
    )
    assert proc.returncode == 0, f"heavy deps imported eagerly -> {proc.stdout.strip()}"


def test_registry_lists_all_codecs():
    from open4d.modules import list_codecs
    assert list_codecs() == ["n4mc", "tsmc", "tvmc"]


def test_get_codec_returns_codec_instances():
    from open4d.modules import get_codec
    from open4d.modules.pipelines import Codec
    for name in ("n4mc", "tsmc", "tvmc"):
        c = get_codec(name)
        assert isinstance(c, Codec)
        assert c.name == name
        assert c.description


def test_get_codec_unknown_raises():
    from open4d.modules import get_codec
    try:
        get_codec("does-not-exist")
    except KeyError:
        return
    raise AssertionError("expected KeyError for unknown codec")


def test_capability_probe_shape():
    from open4d.modules import get_codec
    from open4d.modules.pipelines import Capability
    for name in ("n4mc", "tsmc", "tvmc"):
        cap = get_codec(name).available()
        assert isinstance(cap, Capability)
        assert isinstance(cap.ok, bool)
        assert isinstance(cap.missing, list)
        assert isinstance(cap.notes, list)


def test_tsmc_dry_run_plan():
    from open4d.modules import get_codec
    res = get_codec("tsmc").compress("answering", dry_run=True)
    assert res.codec == "tsmc"
    assert len(res.stages) == 8
    # first stage is reference-center, last is evaluation; all carry a plan
    assert res.stages[0].name.endswith("reference_center")
    assert res.stages[-1].name.endswith("evaluation")
    assert all("[plan]" in s.detail for s in res.stages)
    # dataset name is threaded into the commands
    assert all("answering" in s.detail for s in res.stages)


def test_tvmc_dry_run_plan():
    from open4d.modules import get_codec
    res = get_codec("tvmc").compress("dancer", first_index=5, last_index=14, dry_run=True)
    assert res.codec == "tvmc"
    assert len(res.stages) == 9
    # TVMC includes the ARAP tracking build+run up front
    assert res.stages[0].name.startswith("1a_arap")
    assert "config-dancer-max.xml" in res.stages[1].detail
    assert any("draco_encoder" in s.detail for s in res.stages)


def test_compression_result_contract():
    from open4d.modules.pipelines import CompressionResult, StageTiming
    r = CompressionResult(codec="x", source="s", workdir="/tmp")
    r.stages = [StageTiming("a", 1.5, True), StageTiming("b", 2.5, True)]
    assert abs(r.total_seconds - 4.0) < 1e-9
    assert "TOTAL" in r.stage_table()
    assert r.reconstruction_paths() == []  # no mesh artifacts yet


def test_bad_source_type_rejected():
    from open4d.modules import get_codec
    for name in ("tsmc", "tvmc"):
        try:
            get_codec(name).compress(12345, dry_run=True)  # not a dataset name
        except TypeError:
            continue
        raise AssertionError(f"{name} should reject non-string source")


def _run_standalone():
    tests = sorted(
        (n, o) for n, o in globals().items() if n.startswith("test_") and callable(o)
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
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(_run_standalone())
