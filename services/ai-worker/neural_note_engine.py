from __future__ import annotations

import json
from pathlib import Path


def load_config(path: Path) -> dict:
    config = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(config, dict):
        raise ValueError("configuration must be an object")
    chunk = int(config.get("chunkSamples", 0))
    overlap = int(config.get("overlapSamples", -1))
    if chunk <= 0 or overlap < 0 or overlap * 2 >= chunk:
        raise ValueError("invalid chunk settings")
    return config
