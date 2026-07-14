from __future__ import annotations

import copy
import json
from pathlib import Path

import yaml


def load_config(path: str | Path) -> dict:
    with open(path, "r", encoding="utf-8") as handle:
        config = yaml.safe_load(handle)
    if not isinstance(config, dict):
        raise ValueError(f"Config at {path} must decode to a mapping.")
    return config


def _parse_value(raw: str):
    try:
        return json.loads(raw)
    except json.JSONDecodeError:
        return raw


def apply_overrides(config: dict, overrides: list[str] | None) -> dict:
    updated = copy.deepcopy(config)
    if not overrides:
        return updated

    for override in overrides:
        if "=" not in override:
            raise ValueError(f"Override '{override}' must be formatted as key=value.")
        dotted_key, raw_value = override.split("=", 1)
        cursor = updated
        parts = dotted_key.split(".")
        for part in parts[:-1]:
            if part not in cursor or not isinstance(cursor[part], dict):
                cursor[part] = {}
            cursor = cursor[part]
        cursor[parts[-1]] = _parse_value(raw_value)
    return updated


def dump_config(config: dict, path: str | Path) -> None:
    with open(path, "w", encoding="utf-8") as handle:
        yaml.safe_dump(config, handle, sort_keys=False)
