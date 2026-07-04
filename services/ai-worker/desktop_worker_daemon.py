from __future__ import annotations

import argparse
import json
import os
import signal
import time
from pathlib import Path

from request_once import execute_request

_STOP = False


def _stop(_signum: int, _frame: object) -> None:
    global _STOP
    _STOP = True


def process_one(inbox: Path, outbox: Path) -> bool:
    requests = sorted(path for path in inbox.glob("*.json") if path.is_file())
    if not requests:
        return False

    request_file = requests[0]
    processing_file = request_file.with_suffix(".processing")
    try:
        request_file.replace(processing_file)
    except OSError:
        return True

    request_id = processing_file.stem
    try:
        payload = json.loads(processing_file.read_text(encoding="utf-8"))
        if not isinstance(payload, dict):
            raise ValueError("request must be a JSON object")
        request_id = str(payload.get("id") or request_id)
        response = execute_request(payload)
    except Exception as exc:
        response = {
            "id": request_id,
            "error": {"code": "DESKTOP_BRIDGE_ERROR", "message": str(exc)},
        }
    finally:
        processing_file.unlink(missing_ok=True)

    response_file = outbox / f"{request_id}.json"
    temporary = response_file.with_suffix(".json.part")
    temporary.write_text(json.dumps(response, separators=(",", ":")), encoding="utf-8")
    temporary.replace(response_file)
    return True


def run(queue: Path, poll_seconds: float, once: bool) -> int:
    inbox = queue / "inbox"
    outbox = queue / "outbox"
    inbox.mkdir(parents=True, exist_ok=True)
    outbox.mkdir(parents=True, exist_ok=True)

    while not _STOP:
        processed = process_one(inbox, outbox)
        if once:
            return 0
        if not processed:
            time.sleep(max(0.05, poll_seconds))
    return 0


def main() -> int:
    default_queue = Path(os.environ.get(
        "OMNISTEM_WORKER_QUEUE",
        Path.home() / ".omnistem" / "worker-queue",
    ))
    parser = argparse.ArgumentParser(description="Run the OmniStem desktop worker daemon")
    parser.add_argument("--queue", type=Path, default=default_queue)
    parser.add_argument("--poll", type=float, default=0.1)
    parser.add_argument("--once", action="store_true")
    args = parser.parse_args()

    signal.signal(signal.SIGINT, _stop)
    signal.signal(signal.SIGTERM, _stop)
    return run(args.queue.expanduser().resolve(), args.poll, args.once)


if __name__ == "__main__":
    raise SystemExit(main())
