from __future__ import annotations

import argparse
from pathlib import Path

import torch

from training.common import build_dataloaders, build_model, format_metrics, resolve_device, run_validation
from utils import load_config, save_json
from utils.config import apply_overrides


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Validate the TSDF compression baseline.")
    parser.add_argument("--config", required=True, help="Path to a YAML config file.")
    parser.add_argument("--checkpoint", required=True, help="Checkpoint produced by training.")
    parser.add_argument(
        "--set",
        dest="overrides",
        action="append",
        default=[],
        help="Override config values, for example: --set data.limit=2",
    )
    parser.add_argument("--output", default=None, help="Optional JSON output path.")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    config = apply_overrides(load_config(args.config), args.overrides)
    device = resolve_device(config.get("training", {}).get("device"))
    _, _, _, val_loader = build_dataloaders(config)

    checkpoint = torch.load(args.checkpoint, map_location=device)
    model = build_model(config, device)
    model.load_state_dict(checkpoint["model"])

    metrics = run_validation(model, val_loader, config, device)
    print(format_metrics(metrics))
    if args.output:
        save_json(metrics, Path(args.output))


if __name__ == "__main__":
    main()
