import json
import os
from pathlib import Path
import subprocess
import sys

import pytest

from open4d.codec import encode_sequence
from open4d.codec.tests.test_research_cpu import moving_cube


@pytest.mark.gpu
def test_n4mc_cuda_decode_is_stable_in_fresh_processes(tmp_path):
    torch = pytest.importorskip("torch")
    pytest.importorskip("point_cloud_utils")
    pytest.importorskip("skimage")
    if not torch.cuda.is_available():
        pytest.skip("CUDA is unavailable")
    artifact = encode_sequence(
        moving_cube(), tmp_path / "cube.o4d", codec="n4mc", device="cuda",
        resolution=15, epochs=30, hidden_channels=(4, 8), latent_channels=4,
        learning_rate=3e-3,
    )
    program = """
import hashlib, json, sys, torch
from open4d.codec import decode_sequence
from open4d.codec._research import research_module
torch.backends.cudnn.benchmark = True
torch.backends.cudnn.deterministic = False
before = (torch.backends.cudnn.enabled, torch.backends.cudnn.benchmark,
          torch.backends.cudnn.deterministic, torch.backends.cudnn.allow_tf32)
model = research_module('n4mc.models').TSDFCompressionAutoencoder
original = model.decode_quantized_latent
def checked(self, *arguments):
    assert torch.backends.cudnn.deterministic
    assert not torch.backends.cudnn.benchmark
    assert torch.backends.cudnn.enabled == before[0]
    assert torch.backends.cudnn.allow_tf32 == before[3]
    return original(self, *arguments)
model.decode_quantized_latent = checked
with decode_sequence(sys.argv[1], device='cuda') as decoded:
    frames = [{'index': f.frame_index, 'timestamp': f.timestamp,
               'positions': hashlib.sha256(f.geometry.positions.tobytes()).hexdigest(),
               'triangles': hashlib.sha256(f.geometry.triangles.tobytes()).hexdigest()}
              for f in decoded]
after = (torch.backends.cudnn.enabled, torch.backends.cudnn.benchmark,
         torch.backends.cudnn.deterministic, torch.backends.cudnn.allow_tf32)
assert after == before
print(json.dumps(frames))
"""
    root = Path(__file__).resolve().parents[3]
    environment = dict(os.environ)
    environment["PYTHONPATH"] = os.pathsep.join(filter(None, (
        str(root), environment.get("PYTHONPATH"),
    )))
    results = [json.loads(subprocess.check_output(
        [sys.executable, "-c", program, str(artifact)], env=environment, text=True,
    )) for _ in range(2)]
    assert results[0] == results[1]
    assert [frame["index"] for frame in results[0]] == [41, 73]
    assert [frame["timestamp"] for frame in results[0]] == [1.25, 2.75]
