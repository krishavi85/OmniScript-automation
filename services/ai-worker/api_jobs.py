from __future__ import annotations

from typing import Any

import main as worker_entry
import model_methods
from fastapi import APIRouter, HTTPException


router = APIRouter(prefix="/jobs")


def invoke(method: str, job_id: str) -> dict[str, Any]:
    try:
        return worker_entry.worker.handle({
            "id": "api-job",
            "method": method,
            "params": {"jobId": job_id},
        })["result"]
    except worker_entry.worker.RpcError as exc:
        raise HTTPException(
            status_code=404 if exc.code == "JOB_NOT_FOUND" else 400,
            detail={"code": exc.code, "message": exc.message},
        ) from exc


@router.get("/{job_id}")
def job_status(job_id: str) -> dict[str, Any]:
    return invoke("job.status", job_id)


@router.post("/{job_id}/cancel")
def job_cancel(job_id: str) -> dict[str, Any]:
    return invoke("job.cancel", job_id)
