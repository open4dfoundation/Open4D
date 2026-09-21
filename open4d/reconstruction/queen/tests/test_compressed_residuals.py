"""Native compressed I/O regressions; run in the configured QUEEN CUDA environment."""
from types import SimpleNamespace

import pytest

torch = pytest.importorskip("torch")
if not torch.cuda.is_available():
    pytest.skip("QUEEN requires CUDA", allow_module_level=True)

from scene.decoders import DecoderIdentity
from scene.gaussian_model import GaussianModel


class FixedGate(torch.nn.Module):
    def __init__(self, mask):
        super().__init__()
        self.mask = mask

    def sample_gate(self, stochastic=False):
        return self.mask

    def forward(self, values):
        return values * self.mask[:, None]


def model_for_io(count, gated, empty_gate=False):
    model = GaussianModel.__new__(GaussianModel)
    model.param_names = ["xyz"]
    model.max_sh_degree = 1
    model.latent_decoders = {"xyz": DecoderIdentity()}
    model.latent_args = SimpleNamespace(gate_params=["on" if gated else "none"])
    previous = torch.linspace(0, 1, count * 3, device="cuda").reshape(count, 3)
    current = previous + .0123
    model._latents = {"xyz": torch.nn.Parameter(current)}
    model.prev_atts = {"xyz": previous}
    model.xyz_before = previous
    model.mapping = torch.arange(count, device="cuda")
    model.gate_params = {"xyz": gated}
    mask = torch.zeros(count, device="cuda", dtype=torch.bool) if empty_gate else torch.ones(count, device="cuda", dtype=torch.bool)
    model.gate_atts = FixedGate(mask) if gated else None
    return model, previous if empty_gate else current


@pytest.mark.parametrize("count", [12, 900, 32769])
@pytest.mark.parametrize("gated,empty_gate", [(True, False), (True, True), (False, False)])
def test_compressed_positions_round_trip(tmp_path, count, gated, empty_gate):
    encoder, expected = model_for_io(count, gated, empty_gate)
    path = tmp_path / "frame.pkl"
    encoder.save_compressed_pkl(path, SimpleNamespace())
    decoder, _ = model_for_io(count, gated, empty_gate)
    decoder.load_compressed_pkl(path)
    torch.testing.assert_close(decoder._latents["xyz"], expected, rtol=0, atol=5.1e-5)
