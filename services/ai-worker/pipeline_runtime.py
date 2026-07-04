from __future__ import annotations

import re
import time
from copy import deepcopy
from typing import Any, Callable


class PipelineError(ValueError):
    pass


REFERENCE = re.compile(r"^\$\{([A-Za-z0-9_-]+)(?:\.([A-Za-z0-9_.-]+))?\}$")


def validate_pipeline(definition: dict[str, Any]) -> list[dict[str, Any]]:
    raw_steps = definition.get("steps")
    if not isinstance(raw_steps, list) or not raw_steps:
        raise PipelineError("pipeline.steps must be a non-empty array")
    if len(raw_steps) > 64:
        raise PipelineError("pipeline may contain no more than 64 steps")

    steps: list[dict[str, Any]] = []
    identifiers: set[str] = set()
    for index, raw in enumerate(raw_steps):
        if not isinstance(raw, dict):
            raise PipelineError(f"step {index + 1} must be an object")
        step_id = str(raw.get("id", "")).strip()
        method = str(raw.get("method", "")).strip()
        params = raw.get("params", {})
        depends_on = raw.get("dependsOn", [])
        if not step_id or not re.fullmatch(r"[A-Za-z][A-Za-z0-9_-]{0,63}", step_id):
            raise PipelineError(f"step {index + 1} has an invalid id")
        if step_id in identifiers:
            raise PipelineError(f"duplicate step id: {step_id}")
        if not method or method == "pipeline.run":
            raise PipelineError(f"step {step_id} has an invalid method")
        if not isinstance(params, dict):
            raise PipelineError(f"step {step_id} params must be an object")
        if not isinstance(depends_on, list) or not all(isinstance(value, str) for value in depends_on):
            raise PipelineError(f"step {step_id} dependsOn must be an array of step ids")
        identifiers.add(step_id)
        steps.append({
            "id": step_id,
            "method": method,
            "params": deepcopy(params),
            "dependsOn": list(depends_on),
        })

    known: set[str] = set()
    for step in steps:
        for dependency in step["dependsOn"]:
            if dependency not in identifiers:
                raise PipelineError(f"step {step['id']} depends on unknown step {dependency}")
            if dependency == step["id"]:
                raise PipelineError(f"step {step['id']} cannot depend on itself")
        known.add(step["id"])

    ordered: list[dict[str, Any]] = []
    remaining = {step["id"]: step for step in steps}
    completed: set[str] = set()
    while remaining:
        ready = [step for step in remaining.values() if set(step["dependsOn"]).issubset(completed)]
        if not ready:
            raise PipelineError("pipeline dependencies contain a cycle")
        for step in ready:
            ordered.append(step)
            completed.add(step["id"])
            del remaining[step["id"]]
    return ordered


def _lookup(result: Any, path: str | None) -> Any:
    value = result
    if not path:
        return value
    for part in path.split("."):
        if isinstance(value, dict) and part in value:
            value = value[part]
        elif isinstance(value, list) and part.isdigit() and int(part) < len(value):
            value = value[int(part)]
        else:
            raise PipelineError(f"result path does not exist: {path}")
    return value


def resolve_references(value: Any, results: dict[str, Any]) -> Any:
    if isinstance(value, str):
        match = REFERENCE.fullmatch(value)
        if not match:
            return value
        step_id, path = match.groups()
        if step_id not in results:
            raise PipelineError(f"result is not available for step {step_id}")
        return deepcopy(_lookup(results[step_id], path))
    if isinstance(value, list):
        return [resolve_references(item, results) for item in value]
    if isinstance(value, dict):
        return {key: resolve_references(item, results) for key, item in value.items()}
    return value


def run_pipeline(
    definition: dict[str, Any],
    dispatch: Callable[[str, dict[str, Any]], dict[str, Any]],
    job_snapshot: Callable[[str], dict[str, Any]],
    cancel_requested: Callable[[], bool],
    poll_seconds: float = 0.1,
) -> dict[str, Any]:
    steps = validate_pipeline(definition)
    results: dict[str, Any] = {}
    execution: list[dict[str, Any]] = []

    for step in steps:
        if cancel_requested():
            raise PipelineError("pipeline cancelled")
        params = resolve_references(step["params"], results)
        response = dispatch(step["method"], params)
        if "error" in response:
            error = response["error"]
            raise PipelineError(f"step {step['id']} failed: {error.get('message', error)}")

        result = response.get("result", response)
        if isinstance(result, dict) and "jobId" in result:
            job_id = str(result["jobId"])
            while True:
                if cancel_requested():
                    raise PipelineError("pipeline cancelled")
                snapshot = job_snapshot(job_id)
                state = snapshot.get("state")
                if state == "completed":
                    result = snapshot.get("result") or {}
                    break
                if state in {"failed", "cancelled"}:
                    error = snapshot.get("error") or {"message": snapshot.get("message", state)}
                    raise PipelineError(f"step {step['id']} failed: {error.get('message', error)}")
                time.sleep(max(0.01, poll_seconds))

        results[step["id"]] = result
        execution.append({"id": step["id"], "method": step["method"], "status": "completed"})

    return {
        "name": str(definition.get("name", "pipeline")),
        "steps": execution,
        "results": results,
        "terminalResult": results[steps[-1]["id"]],
    }
