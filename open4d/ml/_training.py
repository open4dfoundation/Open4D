"""Small supervised training loop with no bundled model or dataset."""

from __future__ import annotations

from collections.abc import Callable, Iterable
from dataclasses import dataclass
import math


@dataclass(frozen=True)
class TrainingHistory:
    """Mean loss per epoch and the total number of optimizer steps."""

    losses: tuple[float, ...]
    steps: int


def train(
    model,
    batches: Iterable,
    *,
    optimizer,
    loss_fn: Callable,
    epochs: int = 1,
    on_epoch: Callable[[int, float], None] | None = None,
) -> TrainingHistory:
    """Train a Torch model on ``(inputs, targets)`` batches.

    The caller owns model architecture, data, optimizer and device selection.
    Batches must be re-iterable for multiple epochs, and tensors must already
    be on the model's device. Tuple/list inputs call ``model(*inputs)``;
    dictionaries call ``model(**inputs)``. No model, data or weight downloads
    take place. The model's original train/eval state is restored on exit.
    ``on_epoch`` receives the zero-based epoch and its mean batch loss.
    """
    if not isinstance(epochs, int) or isinstance(epochs, bool) or epochs < 1:
        raise ValueError("epochs must be a positive integer")
    if not callable(loss_fn) or (on_epoch is not None and not callable(on_epoch)):
        raise TypeError("loss_fn and on_epoch must be callable")
    if epochs > 1 and iter(batches) is batches:
        raise ValueError("multiple epochs require re-iterable batches, such as a DataLoader")
    try:
        import torch
    except ImportError as error:
        raise ImportError("training requires Torch; install open4d[torch]") from error
    if not isinstance(model, torch.nn.Module):
        raise TypeError("model must be a torch.nn.Module")
    if not isinstance(optimizer, torch.optim.Optimizer):
        raise TypeError("optimizer must be a torch.optim.Optimizer")
    model_parameters = {id(parameter) for parameter in model.parameters()}
    optimizer_parameters = {
        id(parameter) for group in optimizer.param_groups for parameter in group["params"]
    }
    if not optimizer_parameters or not optimizer_parameters <= model_parameters:
        raise ValueError("optimizer parameters must belong to model")
    previous_modes = [(module, module.training) for module in model.modules()]
    losses = []
    steps = 0
    try:
        model.train()
        for epoch in range(epochs):
            total, count = 0.0, 0
            for inputs, targets in batches:
                optimizer.zero_grad(set_to_none=True)
                if isinstance(inputs, dict):
                    prediction = model(**inputs)
                elif isinstance(inputs, (list, tuple)):
                    prediction = model(*inputs)
                else:
                    prediction = model(inputs)
                loss = loss_fn(prediction, targets)
                if not isinstance(loss, torch.Tensor) or loss.ndim != 0:
                    raise ValueError("loss_fn must return a scalar Torch tensor")
                value = float(loss.detach().item())
                if not math.isfinite(value):
                    raise ValueError("loss is not finite; optimizer step was skipped")
                loss.backward()
                if any(
                    parameter.grad is not None
                    and not bool(torch.isfinite(parameter.grad).all())
                    for parameter in model.parameters()
                ):
                    optimizer.zero_grad(set_to_none=True)
                    raise ValueError("gradient is not finite; optimizer step was skipped")
                optimizer.step()
                total += value
                count += 1
                steps += 1
            if not count:
                raise ValueError(f"batches yielded no training examples in epoch {epoch}")
            mean_loss = total / count
            losses.append(mean_loss)
            if on_epoch is not None:
                on_epoch(epoch, mean_loss)
    finally:
        # train(mode) recurses: restoring only the root would overwrite a
        # caller's individually frozen BatchNorm/Dropout mode selections.
        for module, mode in previous_modes:
            module.training = mode
    return TrainingHistory(tuple(losses), steps)
