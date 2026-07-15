from __future__ import annotations

import json
import sys
import time
from pathlib import Path
from typing import Any

import main as worker_entry


def execute_request(payload: dict[str, Any], timeout_seconds: float = 3600.0) -> dict[str, Any]:
    response = worker_entry.worker.handle(payload)
    result = response.get("result")
    if not isinstance(result, dict) or "jobId" not in result:
        return response

    job_id = str(result["jobId"])
    deadline = time.monotonic() + max(1.0, timeout_seconds)
    while time.monotonic() < deadline:
        snapshot = worker_entry.worker.JOBS.get(job_id).snapshot()
        if snapshot["state"] in {"completed", "failed", "cancelled"}:
            response["job"] = snapshot
            return response
        time.sleep(0.1)

    worker_entry.worker.JOBS.cancel(job_id)
    return {
        "id": payload.get("id"),
        "error": {"code": "TIMEOUT", "message": "worker request exceeded its time limit"},
    }


def main() -> int:
    if len(sys.argv) != 2:
        print(json.dumps({"error": {"code": "USAGE", "message": "request file path is required"}}))
        return 2
    request_path = Path(sys.argv[1]).expanduser().resolve()
    try:
        payload = json.loads(request_path.read_text(encoding="utf-8"))
        if not isinstance(payload, dict):
            raise ValueError("request must be a JSON object")
        response = execute_request(payload)
    except worker_entry.worker.RpcError as exc:
        response = {"id": None, "error": {"code": exc.code, "message": exc.message}}
    except Exception as exc:
        response = {"id": None, "error": {"code": "INTERNAL_ERROR", "message": str(exc)}}
    print(json.dumps(response, separators=(",", ":")), flush=True)
    return 0 if "error" not in response else 1


if __name__ == "__main__":
    raise SystemExit(main())
