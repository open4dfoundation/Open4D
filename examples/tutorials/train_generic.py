"""Train a caller-owned model without downloading neural codecs or weights.

Run ``python examples/tutorials/train_generic.py`` with ``open4d[torch]``.
The bounded CPU example learns y=2x+1 from synthetic tensors and prints its loss.
It does not train a compression codec. Model architecture, optimizer, data,
device selection and independent model/data licenses remain caller-owned.
Multiple epochs require re-iterable batches; train/eval states are restored.
"""

import argparse
import json

import open4d as o4d


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--epochs", type=int, default=40)
    args = parser.parse_args()
    if not 1 <= args.epochs <= 1000:
        parser.error("--epochs must be between 1 and 1000")
    try:
        import torch
    except ImportError:
        parser.error("install open4d[torch] to run this CPU training example")
    torch.manual_seed(0)
    model = torch.nn.Linear(1, 1).eval()
    inputs = torch.linspace(-1, 1, 32).reshape(-1, 1)
    batches = [(inputs, 2 * inputs + 1)]
    history = o4d.ml.train(model, batches,
        optimizer=torch.optim.SGD(model.parameters(), lr=0.2),
        loss_fn=torch.nn.functional.mse_loss, epochs=args.epochs)
    assert not model.training
    print(json.dumps({"synthetic": True, "device": "cpu", "steps": history.steps,
                      "initial_loss": history.losses[0], "final_loss": history.losses[-1],
                      "weight": float(model.weight.detach()[0, 0]),
                      "bias": float(model.bias.detach()[0]), "eval_restored": not model.training}))


if __name__ == "__main__":
    main()
