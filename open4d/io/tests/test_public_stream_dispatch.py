"""Public TCP compatibility and optional browser streaming coexist."""
from concurrent.futures import ThreadPoolExecutor
import sys

import numpy as np
import pytest

import open4d
from open4d.demo import mesh_sequence


@pytest.mark.parametrize("entry", ["send", "stream"])
@pytest.mark.parametrize("address_style", ["positional", "keyword"])
def test_public_tcp_round_trip_without_browser_dependency(monkeypatch, entry, address_style):
    monkeypatch.setitem(sys.modules, "streamer", None)
    with mesh_sequence(side=3, frames=2) as source:
        with open4d.receive(port=0, timeout=3) as receiver, ThreadPoolExecutor(1) as pool:
            if address_style == "positional":
                result = pool.submit(getattr(open4d, entry), source, *receiver.address,
                                     realtime=False, timeout=3)
            else:
                result = pool.submit(getattr(open4d, entry), source,
                                     host=receiver.address[0], port=receiver.address[1], timeout=3)
            restored = list(receiver)
            assert result.result(timeout=3) == 2
        for expected, actual in zip(source, restored):
            np.testing.assert_array_equal(actual.geometry.positions, expected.geometry.positions)
            np.testing.assert_array_equal(actual.geometry.triangles, expected.geometry.triangles)
            assert actual.timestamp == expected.timestamp


@pytest.mark.parametrize("use_path", [True, False])
def test_browser_requests_report_missing_optional_streamer(monkeypatch, tmp_path, use_path):
    monkeypatch.setitem(sys.modules, "streamer", None)
    with mesh_sequence(side=3, frames=1) as frames:
        source = tmp_path/'capture.ply' if use_path else frames
        with pytest.raises(open4d.StreamerDependencyError, match="pip install -e open4d/streamer"):
            open4d.stream(source, name="capture", out_dir=tmp_path/'bundle', open_browser=False, block=False)
