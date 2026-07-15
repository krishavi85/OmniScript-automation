from __future__ import annotations

import hmac
import os
import re
from typing import Any


class ExecutorPolicyError(ValueError):
    pass


ALLOWED_METHODS = frozenset({
    "mode.plan",
    "mode.run",
    "pipeline.validate",
    "pipeline.run",
    "transcription.run",
    "restoration.run",
    "restoration.onnx",
    "mastering.run",
    "mastering.analyze",
    "spectral.tiles",
    "spectral.mask",
    "note.resynthesize",
    "note.resynthesize.neural",
    "instrument.render",
    "voice.transform.authorized",
})
_CREDENTIAL = re.compile(r"^OMNISTEM_[A-Z0-9_]{1,96}$")


def authorize(token: str) -> None:
    expected = os.environ.get("OMNISTEM_EXECUTOR_TOKEN", "")
    if not expected:
        raise ExecutorPolicyError("Autonomous execution is disabled")
    if not token or not hmac.compare_digest(token, expected):
        raise ExecutorPolicyError("Executor authorization failed")


def validate_credentials(value: Any) -> list[str]:
    if value is None:
        return []
    if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
        raise ExecutorPolicyError("requiredCredentials must be an array")
    validated: list[str] = []
    for name in value:
        if not _CREDENTIAL.fullmatch(name):
            raise ExecutorPolicyError(f"Credential reference is not allowed: {name}")
        if not os.environ.get(name):
            raise ExecutorPolicyError(f"Credential reference is not configured: {name}")
        validated.append(name)
    return validated


def validate_actions(value: Any) -> list[dict[str, Any]]:
    if not isinstance(value, list) or not value:
        raise ExecutorPolicyError("actions must be a non-empty array")
    if len(value) > 16:
        raise ExecutorPolicyError("No more than 16 actions may run in one request")
    actions: list[dict[str, Any]] = []
    for index, item in enumerate(value):
        if not isinstance(item, dict):
            raise ExecutorPolicyError(f"Action {index + 1} must be an object")
        method = str(item.get("method", "")).strip()
        params = item.get("params", {})
        if method not in ALLOWED_METHODS:
            raise ExecutorPolicyError(f"Executor method is not allowed: {method}")
        if not isinstance(params, dict):
            raise ExecutorPolicyError(f"Action {index + 1} params must be an object")
        actions.append({"method": method, "params": params})
    return actions
