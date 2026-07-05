from __future__ import annotations

from typing import Any

import worker
from model_download import download_and_register
from model_registry import ModelRegistryError, list_models, unregister, update_status


def installed_list(_: dict[str, Any]) -> dict[str, Any]:
    return {"models": list_models()}


def install_model(params: dict[str, Any]) -> dict[str, Any]:
    def operation(job: worker.Job) -> dict[str, Any]:
        def report(written: int, total: int | None) -> None:
            if total and total > 0:
                job.update(progress=min(0.95, written / total), message=f"Downloaded {written} of {total} bytes")
            else:
                job.update(message=f"Downloaded {written} bytes")

        try:
            result = download_and_register(params, report)
        except ModelRegistryError as exc:
            raise worker.RpcError("MODEL_INSTALL_ERROR", str(exc)) from exc
        job.update(progress=0.99, message="Model verified and registered")
        return result

    return {"jobId": worker.JOBS.submit("model-install", operation).id}


def remove_model(params: dict[str, Any]) -> dict[str, Any]:
    try:
        return unregister(str(params.get("modelId", "")))
    except ModelRegistryError as exc:
        raise worker.RpcError("MODEL_REMOVE_ERROR", str(exc)) from exc


def model_update_status(params: dict[str, Any]) -> dict[str, Any]:
    try:
        return update_status(str(params.get("modelId", "")), str(params.get("version", "")))
    except ModelRegistryError as exc:
        raise worker.RpcError("MODEL_STATUS_ERROR", str(exc)) from exc


worker.METHODS.update({
    "model.installed.list": installed_list,
    "model.install": install_model,
    "model.remove": remove_model,
    "model.updateStatus": model_update_status,
})
