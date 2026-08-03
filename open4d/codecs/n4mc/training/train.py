from __future__ import annotations

import argparse
from datetime import datetime
from pathlib import Path
import json

import torch

from losses import compute_rd_loss
from training.common import (
    _autocast_context,
    build_dataloaders,
    build_model,
    checkpoint_payload,
    format_metrics,
    prepare_training_input,
    resolve_device,
    run_validation,
)
from utils import dump_config, ensure_dir, load_config, save_json, seed_everything
from utils.config import apply_overrides


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Train the TSDF compression baseline.")
    parser.add_argument("--config", required=True, help="Path to a YAML config file.")
    parser.add_argument(
        "--set",
        dest="overrides",
        action="append",
        default=[],
        help="Override config values, for example: --set training.epochs=1",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    config = apply_overrides(load_config(args.config), args.overrides)
    seed_everything(int(config["experiment"].get("seed", 0)))

    device = resolve_device(config.get("training", {}).get("device"))
    experiment_name = config["experiment"].get("name", "tsdf_codec")
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    run_dir = ensure_dir(Path(config["experiment"].get("output_root", "outputs")) / f"{experiment_name}_{timestamp}")
    dump_config(config, run_dir / "config.yaml")

    _, _, train_loader, val_loader = build_dataloaders(config)
    model = build_model(config, device)

    optimizer_cfg = config.get("optimizer", {})
    optimizer = torch.optim.AdamW(
        model.parameters(),
        lr=float(optimizer_cfg.get("lr", 1e-4)),
        weight_decay=float(optimizer_cfg.get("weight_decay", 0.0)),
    )

    training_cfg = config.get("training", {})
    use_amp = bool(training_cfg.get("amp", False)) and device.type == "cuda"
    scaler = torch.amp.GradScaler("cuda", enabled=use_amp)
    epochs = int(training_cfg.get("epochs", 1))
    log_interval = int(training_cfg.get("log_interval", 10))
    validate_every = int(training_cfg.get("validate_every", 1))
    grad_clip_norm = training_cfg.get("grad_clip_norm")

    rng = torch.Generator()
    rng.manual_seed(int(config["experiment"].get("seed", 0)))
    best_metric = float("inf")
    history_path = run_dir / "metrics.jsonl"

    for epoch in range(1, epochs + 1):
        model.train()
        for step, batch in enumerate(train_loader, start=1):
            batch_tsdf = batch["tsdf"].to(device, non_blocking=True)
            train_input = prepare_training_input(batch_tsdf, config["data"], generator=rng).to(device)

            optimizer.zero_grad(set_to_none=True)
            with _autocast_context(device, use_amp):
                outputs = model(train_input)
                total_loss, metrics = compute_rd_loss(outputs, train_input, config["loss"])

            scaler.scale(total_loss).backward()
            if grad_clip_norm is not None:
                scaler.unscale_(optimizer)
                torch.nn.utils.clip_grad_norm_(model.parameters(), float(grad_clip_norm))
            scaler.step(optimizer)
            scaler.update()

            if step % log_interval == 0 or step == 1:
                train_metrics = {key: float(value.item()) for key, value in metrics.items()}
                print(f"epoch={epoch:04d} step={step:04d} {format_metrics(train_metrics)}")

        if epoch % validate_every != 0:
            continue

        output_dir = run_dir / "log" / f"checkpoint_{epoch:04d}" / "rec_mesh"
        print(f"Saving predictions to {output_dir}")
        ensure_dir(output_dir)
        val_metrics = run_validation(model, val_loader, config, device, output_dir=output_dir, save_predictions=True)
        val_metric = float(val_metrics[config["training"].get("best_metric", "total_loss")])
        record = {"epoch": epoch, "split": "val", **val_metrics}
        with open(history_path, "a", encoding="utf-8") as handle:
            handle.write(json.dumps(record) + "\n")
        print(f"validation epoch={epoch:04d} {format_metrics(val_metrics)}")

        latest_path = run_dir / "latest.pt"
        torch.save(checkpoint_payload(model, optimizer, scaler, epoch, config, best_metric), latest_path)
        if val_metric < best_metric:
            best_metric = val_metric
            torch.save(checkpoint_payload(model, optimizer, scaler, epoch, config, best_metric), run_dir / "best.pt")
            save_json({"best_metric": best_metric, "epoch": epoch, "metrics": val_metrics}, run_dir / "best.json")


if __name__ == "__main__":
    main()
