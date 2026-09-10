#!/usr/bin/env python3
"""Check I/O and CLI commands from an installed wheel."""

import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import sysconfig
import tempfile

import numpy as np

import integrations
import open4d
from open4d.codec import CodecError, available_codecs
from open4d.io import inspect_sequence, open_sequence, write_sequence


for module in (open4d, integrations):
    installed_path = Path(module.__file__).resolve()
    assert installed_path.is_relative_to(Path(sysconfig.get_path("purelib")).resolve()), installed_path
print(f"Installed package: {open4d.__file__}")
assert "scipy" not in sys.modules, "importing open4d must not import SciPy"

with tempfile.TemporaryDirectory() as directory:
    command = Path(sysconfig.get_path("scripts")) / ("open4d.exe" if os.name == "nt" else "open4d")
    assert command.is_file(), "installed console command is missing"
    sample = Path(directory) / "wave sample"
    subprocess.run(
        [str(command), "demo", str(sample), "--side", "4", "--frames", "3"],
        check=True, cwd=directory,
    )
    result = subprocess.run(
        [str(command), "inspect", str(sample), "--json"],
        check=True, cwd=directory, capture_output=True, text=True,
    )
    info = json.loads(result.stdout)
    assert info["frame_count"] == 3 and info["first_frame"]["vertices"] == 16
    assert (sample / "LICENSE").is_file()
    subprocess.run([sys.executable, "-m", "open4d", "--help"], check=True, cwd=directory)
    if importlib.util.find_spec("PyQt6") is None:
        result = subprocess.run(
            [str(command), "view", str(sample)], cwd=directory, capture_output=True, text=True,
        )
        assert result.returncode == 1 and "open4d[player]" in result.stderr
        assert "Traceback" not in result.stderr
    path = Path(directory) / "triangle.obj"
    path.write_text("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", encoding="ascii")
    info = inspect_sequence(path)
    with open_sequence(path) as source:
        frame = source[0]
        if importlib.util.find_spec("scipy") is None:
            try:
                open4d.compare_meshes(frame.geometry, frame.geometry)
            except ImportError as error:
                assert "open4d[metrics]" in str(error)
            else:
                raise AssertionError("a missing metrics dependency was not reported")
        else:
            assert open4d.compare_meshes(frame.geometry, frame.geometry).symmetric_rms == 0
            assert open4d.compare_sequences(source, source).symmetric_rms == 0
        for format in ("obj", "ply"):
            exported = write_sequence(
                source, Path(directory) / f"roundtrip.{format}", allow_lossy=True,
            )
            with open_sequence(exported) as restored:
                np.testing.assert_array_equal(restored[0].geometry.positions, frame.geometry.positions)
                np.testing.assert_array_equal(restored[0].geometry.triangles, frame.geometry.triangles)
    assert info.frame_count == 1 and info.format == "obj"
    np.testing.assert_array_equal(frame.geometry.triangles, [[0, 1, 2]])
    assert all(callable(getattr(open4d, name)) for name in
               ("encode", "decode", "visualize", "reconstruct", "stream", "receive"))
    installed = {info.id for info in available_codecs()}
    assert {"klt", "n4mc", "qndf", "qndf-int8", "vdmc", "faster_vdmc", "tvmc", "tsmc", "vega"} <= installed
    assert not installed & {"npz", "raw", "draco", "temporal-delta", "temporal-pca"}
    from open4d.codec._research import research_module
    os.environ.pop("OPEN4D_RESEARCH_ROOT", None)
    try:
        research_module("klt.klt")
    except CodecError as error:
        assert "OPEN4D_RESEARCH_ROOT" in str(error)
    else:
        raise AssertionError("wheel unexpectedly includes KLT research source")

    from concurrent.futures import ThreadPoolExecutor
    from open4d.demo import mesh_sequence

    with mesh_sequence(side=3, frames=2) as source:
        with open4d.receive(port=0, timeout=5) as receiver, ThreadPoolExecutor(1) as pool:
            transfer = pool.submit(open4d.stream, source, *receiver.address, realtime=False)
            restored = list(receiver)
            assert transfer.result() == 2
        for expected, actual in zip(source, restored):
            np.testing.assert_array_equal(actual.geometry.positions, expected.geometry.positions)
            assert actual.timestamp == expected.timestamp
    print("Installed I/O, CLI, codec discovery and mesh streaming passed")
