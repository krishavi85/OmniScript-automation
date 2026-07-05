from __future__ import annotations

import uuid
from typing import Any

import worker
from executor_policy import (
    ExecutorPolicyError,
    authorize,
    validate_actions,
    validate_credentials,
)


def executor_run(params: dict[str, Any]) -> dict[str, Any]:
    try:
        authorize(str(params.get("authorizationToken", "")))
        actions = validate_actions(params.get("actions"))
        credential_names = validate_credentials(params.get("requiredCredentials"))
    except ExecutorPolicyError as exc:
        raise worker.RpcError("EXECUTOR_POLICY_ERROR", str(exc)) from exc

    request_id = str(params.get("requestId") or uuid.uuid4())

    def operation(job: worker.Job) -> dict[str, Any]:
        results: list[dict[str, Any]] = []
        for index, action in enumerate(actions):
            if job.cancel.is_set():
                raise worker.RpcError("CANCELLED", "Executor cancelled")
            method = str(action["method"])
            job.update(progress=index / len(actions), message=f"Running {method}")
            response = worker.handle({
                "id": f"executor:{request_id}:{index + 1}",
                "method": method,
                "params": action["params"],
            })
            result = response.get("result", {})
            if isinstance(result, dict) and result.get("jobId"):
                child_id = str(result["jobId"])
                while True:
                    if job.cancel.is_set():
                        worker.JOBS.cancel(child_id)
                        raise worker.RpcError("CANCELLED", "Executor cancelled")
                    snapshot = worker.JOBS.get(child_id).snapshot()
                    state = snapshot["state"]
                    if state == "completed":
                        result = snapshot.get("result") or {}
                        break
                    if state in {"failed", "cancelled"}:
                        raise worker.RpcError("CHILD_ACTION_FAILED", f"{method}: {snapshot.get('message', state)}")
                    job.cancel.wait(0.1)
            results.append({"method": method, "result": result})
        return {
            "requestId": request_id,
            "actions": results,
            "credentialReferences": credential_names,
            "credentialsRedacted": True,
        }

    job = worker.JOBS.submit("authorized-executor", operation)
    return {"jobId": job.id, "requestId": request_id}


worker.METHODS["executor.run"] = executor_run
