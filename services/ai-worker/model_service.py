from __future__ import annotations

from typing import Any

from model_registry import list_models, update_status


def installed_models() -> dict[str, Any]:
    return {"models": list_models()}


def version_status(model_id: str, version: str) -> dict[str, Any]:
    return update_status(model_id, version)
