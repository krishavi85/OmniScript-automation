from __future__ import annotations

from typing import Any

from model_registry import register, unregister


def register_installed_model(request: dict[str, Any]) -> dict[str, Any]:
    return register(request)


def unregister_installed_model(model_id: str) -> dict[str, Any]:
    return unregister(model_id)
