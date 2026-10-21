"""Training contracts for caller-owned Torch models."""

import numpy as np
import pytest

from open4d.ml import train

pytestmark = pytest.mark.torch
torch = pytest.importorskip("torch")


def test_linear_fit_converges_and_restores_eval():
    model = torch.nn.Linear(1, 1)
    with torch.no_grad():
        model.weight.zero_()
        model.bias.zero_()
    model.eval()
    x = torch.linspace(-1, 1, 32).reshape(-1, 1)
    optimizer = torch.optim.SGD(model.parameters(), lr=0.2)
    observed = []
    history = train(model, [(x, 2 * x + 1)], optimizer=optimizer,
                    loss_fn=torch.nn.functional.mse_loss, epochs=100,
                    on_epoch=lambda epoch, loss: observed.append((epoch, loss)))
    assert history.steps == 100 and len(observed) == 100
    assert history.losses[-1] < 1e-8
    assert not model.training
    np.testing.assert_allclose(model(x).detach().numpy(), (2 * x + 1).numpy(), atol=0.0001)


def test_nonfinite_loss_does_not_update_model():
    model = torch.nn.Linear(1, 1).eval()
    original = model.weight.detach().clone()
    optimizer = torch.optim.SGD(model.parameters(), lr=1)
    with pytest.raises(ValueError, match="not finite"):
        train(model, [(torch.ones(1, 1), torch.full((1, 1), float("nan")))],
              optimizer=optimizer, loss_fn=torch.nn.functional.mse_loss)
    assert torch.equal(model.weight, original)
    assert not model.training


def test_rejects_exhaustible_multiepoch_data_and_wrong_optimizer():
    model = torch.nn.Linear(1, 1)
    optimizer = torch.optim.SGD(model.parameters(), lr=1)
    with pytest.raises(ValueError, match="re-iterable"):
        train(model, iter([]), optimizer=optimizer,
              loss_fn=torch.nn.functional.mse_loss, epochs=2)
    other = torch.nn.Linear(1, 1)
    with pytest.raises(ValueError, match="belong to model"):
        train(model, [], optimizer=torch.optim.SGD(other.parameters(), lr=1),
              loss_fn=torch.nn.functional.mse_loss)


def test_empty_batches_restore_mode():
    model = torch.nn.Linear(1, 1).eval()
    with pytest.raises(ValueError, match="no training examples"):
        train(model, [], optimizer=torch.optim.SGD(model.parameters(), lr=1),
              loss_fn=torch.nn.functional.mse_loss)
    assert not model.training


def test_mixed_child_modes_are_restored_even_when_training_fails():
    model = torch.nn.Sequential(torch.nn.Linear(1, 1), torch.nn.Dropout())
    model.train()
    model[1].eval()
    with pytest.raises(ValueError, match="no training examples"):
        train(model, [], optimizer=torch.optim.SGD(model.parameters(), lr=1),
              loss_fn=torch.nn.functional.mse_loss)
    assert model.training and model[0].training and not model[1].training
