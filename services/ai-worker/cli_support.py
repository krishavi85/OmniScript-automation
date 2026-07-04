from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from request_once import execute_request


class CliError(RuntimeError):
    pass


def invoke(method: str, params: dict[str, Any]) -> dict[str, Any]:
    response = execute_request({"id": "cli", "method": method, "params": params})
    if "error" in response:
        error = response["error"]
        raise CliError(str(error.get("message", error)))
    job = response.get("job")
    if isinstance(job, dict):
        if job.get("state") != "completed":
            error = job.get("error") or {"message": job.get("message", job.get("state"))}
            raise CliError(str(error.get("message", error)))
        return job.get("result") or {}
    result = response.get("result")
    return result if isinstance(result, dict) else {"result": result}


def load_json(path: Path) -> dict[str, Any]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise CliError(f"could not read JSON file {path}: {exc}") from exc
    if not isinstance(payload, dict):
        raise CliError(f"JSON file must contain an object: {path}")
    return payload


def parse_csv(value: str) -> list[str]:
    return [part.strip() for part in value.split(",") if part.strip()]


def emit(payload: Any, compact: bool = False) -> None:
    print(json.dumps(payload, indent=None if compact else 2, sort_keys=not compact, default=str))
