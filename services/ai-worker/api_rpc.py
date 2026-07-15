from __future__ import annotations

from typing import Any

import main as worker_entry
from fastapi import APIRouter, HTTPException
from pydantic import BaseModel, Field


class RpcRequest(BaseModel):
    id: str | int | None = None
    method: str = Field(min_length=1)
    params: dict[str, Any] = Field(default_factory=dict)


router = APIRouter()


@router.post("/rpc")
def rpc(request: RpcRequest) -> dict[str, Any]:
    try:
        return worker_entry.worker.handle(request.model_dump())
    except worker_entry.worker.RpcError as exc:
        raise HTTPException(
            status_code=400,
            detail={"code": exc.code, "message": exc.message},
        ) from exc
