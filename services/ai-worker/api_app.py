from __future__ import annotations

from typing import Any

import main as worker_entry

try:
    from fastapi import FastAPI
except ImportError as exc:  # pragma: no cover
    raise RuntimeError("Install FastAPI and Uvicorn to use the local API") from exc

from api_jobs import router as jobs_router
from api_rpc import router as rpc_router


app = FastAPI(
    title="OmniStem Studio Local API",
    version="0.4.0",
    description="Local API for the canonical OmniStem Studio worker.",
)
app.include_router(rpc_router)
app.include_router(jobs_router)


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


@app.get("/models")
def models(
    query: str = "",
    engine: str = "",
    family: str = "",
    tag: str = "",
    stem: str = "",
    include_deprecated: bool = True,
    sort_by: str = "name",
    descending: bool = False,
) -> dict[str, Any]:
    return worker_entry.worker.handle({
        "id": "api-models",
        "method": "catalog.models",
        "params": {
            "query": query,
            "engine": engine,
            "family": family,
            "tag": tag,
            "stem": stem,
            "includeDeprecated": include_deprecated,
            "sortBy": sort_by,
            "descending": descending,
        },
    })["result"]
