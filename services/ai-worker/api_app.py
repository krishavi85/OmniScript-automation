from __future__ import annotations

from typing import Any

import main as worker_entry

try:
    from fastapi import FastAPI
except ImportError as exc:  # pragma: no cover
    raise RuntimeError("Install FastAPI and Uvicorn to use the local API") from exc


app = FastAPI(
    title="OmniStem Studio Local API",
    version="0.4.0",
    description="Local API for the canonical OmniStem Studio worker.",
)


@app.get("/health")
def health() -> dict[str, Any]:
    return worker_entry.worker.handle({
        "id": "api-health",
        "method": "health.check",
        "params": {},
    })["result"]


@app.get("/methods")
def methods() -> dict[str, Any]:
    return {"methods": sorted(worker_entry.worker.METHODS)}


@app.get("/engines")
def engines() -> dict[str, Any]:
    return worker_entry.worker.handle({
        "id": "api-engines",
        "method": "engine.list",
        "params": {},
    })["result"]
