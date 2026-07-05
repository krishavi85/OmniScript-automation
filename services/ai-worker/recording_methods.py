from __future__ import annotations

from pathlib import Path
from typing import Any

import worker
from recording_service import MANAGER, RecordingError


def _call(operation: Any) -> dict[str, Any]:
    try:
        return operation()
    except RecordingError as exc:
        raise worker.RpcError("RECORDING_ERROR", str(exc)) from exc


def devices(_: dict[str, Any]) -> dict[str, Any]:
    return _call(lambda: {"devices": MANAGER.devices()})


def start(params: dict[str, Any]) -> dict[str, Any]:
    return _call(lambda: MANAGER.start(params))


def stop(_: dict[str, Any]) -> dict[str, Any]:
    return _call(MANAGER.stop)


def status(_: dict[str, Any]) -> dict[str, Any]:
    return MANAGER.status()


def recover(params: dict[str, Any]) -> dict[str, Any]:
    value = str(params.get("directory", "")).strip()
    if not value:
        raise worker.RpcError("INVALID_ARGUMENT", "directory is required")
    return _call(lambda: MANAGER.recover(Path(value)))


worker.METHODS.update({
    "recording.devices": devices,
    "recording.start": start,
    "recording.stop": stop,
    "recording.status": status,
    "recording.recover": recover,
})
