from __future__ import annotations

import json
import sys
from dataclasses import dataclass
from typing import Any, Callable


@dataclass(frozen=True)
class RpcError(Exception):
    code: str
    message: str


def health_check(_: dict[str, Any]) -> dict[str, Any]:
    return {
        "status": "ok",
        "service": "omnistem-ai-worker",
        "protocolVersion": 1,
        "capabilities": ["health.check", "separation.plan", "transcription.plan"],
    }


def separation_plan(params: dict[str, Any]) -> dict[str, Any]:
    source = str(params.get("source", "")).strip()
    if not source:
        raise RpcError("INVALID_ARGUMENT", "params.source is required")
    quality = str(params.get("quality", "balanced"))
    if quality not in {"fast", "balanced", "studio", "ensemble"}:
        raise RpcError("INVALID_ARGUMENT", f"Unsupported quality mode: {quality}")
    return {
        "source": source,
        "quality": quality,
        "stages": [
            "decode-and-normalize",
            "model-routing",
            "source-separation",
            "phase-alignment",
            "artifact-repair",
            "loudness-matched-export",
        ],
        "status": "planned",
    }


def transcription_plan(params: dict[str, Any]) -> dict[str, Any]:
    stem_id = str(params.get("stemId", "")).strip()
    if not stem_id:
        raise RpcError("INVALID_ARGUMENT", "params.stemId is required")
    return {
        "stemId": stem_id,
        "outputs": ["note-events", "midi", "pitch-curves", "confidence-map"],
        "status": "planned",
    }


METHODS: dict[str, Callable[[dict[str, Any]], dict[str, Any]]] = {
    "health.check": health_check,
    "separation.plan": separation_plan,
    "transcription.plan": transcription_plan,
}


def handle(payload: dict[str, Any]) -> dict[str, Any]:
    request_id = payload.get("id")
    method = str(payload.get("method", ""))
    params = payload.get("params", {})
    if method not in METHODS:
        raise RpcError("METHOD_NOT_FOUND", f"Unknown method: {method}")
    if not isinstance(params, dict):
        raise RpcError("INVALID_ARGUMENT", "params must be an object")
    return {"id": request_id, "result": METHODS[method](params)}


def main() -> int:
    for raw_line in sys.stdin:
        line = raw_line.strip()
        if not line:
            continue
        request_id: Any = None
        try:
            payload = json.loads(line)
            if not isinstance(payload, dict):
                raise RpcError("INVALID_REQUEST", "request must be a JSON object")
            request_id = payload.get("id")
            response = handle(payload)
        except json.JSONDecodeError as exc:
            response = {"id": request_id, "error": {"code": "PARSE_ERROR", "message": str(exc)}}
        except RpcError as exc:
            response = {"id": request_id, "error": {"code": exc.code, "message": exc.message}}
        except Exception as exc:
            response = {"id": request_id, "error": {"code": "INTERNAL_ERROR", "message": str(exc)}}
        print(json.dumps(response, separators=(",", ":")), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
