from __future__ import annotations

import json
import os
import re
import tempfile
from pathlib import Path
from typing import Any


class ModelRegistryError(ValueError):
    pass


_SAFE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$")


def model_root() -> Path:
    configured = os.environ.get("OMNISTEM_MODEL_DIR", "").strip()
    return Path(configured).expanduser().resolve() if configured else Path.home() / ".omnistem" / "models"


def _clean(value: str, label: str) -> str:
    value = value.strip()
    if not _SAFE.fullmatch(value):
        raise ModelRegistryError(f"Invalid {label}")
    return value


def _path(root: Path) -> Path:
    return root / "registry.json"


def _read(root: Path) -> dict[str, Any]:
    path = _path(root)
    if not path.exists():
        return {"schemaVersion": 1, "models": {}}
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ModelRegistryError(f"Could not read registry: {exc}") from exc
    if not isinstance(value, dict) or not isinstance(value.get("models"), dict):
        raise ModelRegistryError("Invalid model registry")
    return value


def _write(root: Path, value: dict[str, Any]) -> None:
    root.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=root, delete=False) as handle:
        json.dump(value, handle, indent=2, sort_keys=True)
        temporary = Path(handle.name)
    temporary.replace(_path(root))


def list_models(root: Path | None = None) -> list[dict[str, Any]]:
    base = (root or model_root()).resolve()
    rows: list[dict[str, Any]] = []
    for model_id, record in _read(base)["models"].items():
        row = dict(record)
        row["modelId"] = model_id
        row["present"] = Path(str(row.get("artifact", ""))).is_file()
        rows.append(row)
    rows.sort(key=lambda row: str(row["modelId"]).casefold())
    return rows


def register(request: dict[str, Any], root: Path | None = None) -> dict[str, Any]:
    model_id = _clean(str(request.get("modelId", "")), "modelId")
    version = _clean(str(request.get("version", "")), "version")
    artifact = Path(str(request.get("artifact", ""))).expanduser().resolve()
    checksum = str(request.get("sha256", "")).strip().lower()
    license_id = str(request.get("licenseId", "")).strip()
    license_url = str(request.get("licenseUrl", "")).strip()
    if not request.get("licenseAccepted", False):
        raise ModelRegistryError("License acceptance is required")
    if not artifact.is_file():
        raise ModelRegistryError("Model artifact does not exist")
    if len(checksum) != 64 or any(char not in "0123456789abcdef" for char in checksum):
        raise ModelRegistryError("sha256 must contain 64 hexadecimal characters")
    if not license_id or not license_url:
        raise ModelRegistryError("licenseId and licenseUrl are required")

    base = (root or model_root()).resolve()
    registry = _read(base)
    record = {
        "version": version,
        "artifact": str(artifact),
        "sha256": checksum,
        "licenseId": license_id,
        "licenseUrl": license_url,
    }
    registry["models"][model_id] = record
    _write(base, registry)
    return {"modelId": model_id, **record}


def update_status(model_id: str, version: str, root: Path | None = None) -> dict[str, Any]:
    safe_id = _clean(model_id, "modelId")
    requested = _clean(version, "version")
    current = next((row for row in list_models(root) if row["modelId"] == safe_id), None)
    return {
        "modelId": safe_id,
        "installedVersion": None if current is None else current.get("version"),
        "requestedVersion": requested,
        "updateAvailable": current is None or str(current.get("version")) != requested,
    }


def unregister(model_id: str, root: Path | None = None) -> dict[str, Any]:
    safe_id = _clean(model_id, "modelId")
    base = (root or model_root()).resolve()
    registry = _read(base)
    if safe_id not in registry["models"]:
        raise ModelRegistryError(f"Model is not registered: {safe_id}")
    registry["models"].pop(safe_id)
    _write(base, registry)
    return {"modelId": safe_id, "removedFromRegistry": True}
