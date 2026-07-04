from __future__ import annotations

from pathlib import Path
from typing import Any

import worker
from benchmarking import BenchmarkError, analyze_file, benchmark_set
from pipeline_runtime import PipelineError, run_pipeline, validate_pipeline


def pipeline_validate(params: dict[str, Any]) -> dict[str, Any]:
    definition = params.get("pipeline", params)
    if not isinstance(definition, dict):
        raise worker.RpcError("INVALID_ARGUMENT", "pipeline must be an object")
    try:
        steps = validate_pipeline(definition)
    except PipelineError as exc:
        raise worker.RpcError("PIPELINE_ERROR", str(exc)) from exc
    return {
        "valid": True,
        "name": str(definition.get("name", "pipeline")),
        "orderedSteps": [step["id"] for step in steps],
        "methods": [step["method"] for step in steps],
    }


def pipeline_run(params: dict[str, Any]) -> dict[str, Any]:
    definition = params.get("pipeline", params)
    if not isinstance(definition, dict):
        raise worker.RpcError("INVALID_ARGUMENT", "pipeline must be an object")
    try:
        validate_pipeline(definition)
    except PipelineError as exc:
        raise worker.RpcError("PIPELINE_ERROR", str(exc)) from exc

    def operation(job: worker.Job) -> dict[str, Any]:
        def dispatch(method: str, method_params: dict[str, Any]) -> dict[str, Any]:
            try:
                return worker.handle({"id": f"pipeline:{job.id}", "method": method, "params": method_params})
            except worker.RpcError as exc:
                return {"error": {"code": exc.code, "message": exc.message}}

        def snapshot(job_id: str) -> dict[str, Any]:
            return worker.JOBS.get(job_id).snapshot()

        try:
            return run_pipeline(
                definition,
                dispatch=dispatch,
                job_snapshot=snapshot,
                cancel_requested=job.cancel.is_set,
                poll_seconds=0.1,
            )
        except PipelineError as exc:
            code = "CANCELLED" if str(exc) == "pipeline cancelled" else "PIPELINE_ERROR"
            raise worker.RpcError(code, str(exc)) from exc

    return {"jobId": worker.JOBS.submit("pipeline", operation).id}


def benchmark_analyze(params: dict[str, Any]) -> dict[str, Any]:
    raw = str(params.get("source", "")).strip()
    if not raw:
        raise worker.RpcError("INVALID_ARGUMENT", "params.source is required")
    try:
        return analyze_file(Path(raw).expanduser().resolve())
    except BenchmarkError as exc:
        raise worker.RpcError("BENCHMARK_ERROR", str(exc)) from exc


def benchmark_compare(params: dict[str, Any]) -> dict[str, Any]:
    raw_candidates = params.get("candidates")
    if not isinstance(raw_candidates, list) or not raw_candidates:
        raise worker.RpcError("INVALID_ARGUMENT", "params.candidates must be a non-empty array")
    candidates = [Path(str(value)).expanduser().resolve() for value in raw_candidates]
    raw_reference = str(params.get("reference", "")).strip()
    reference = Path(raw_reference).expanduser().resolve() if raw_reference else None

    def operation(job: worker.Job) -> dict[str, Any]:
        if job.cancel.is_set():
            raise worker.RpcError("CANCELLED", "Operation cancelled")
        job.update(progress=0.1, message="Analyzing benchmark candidates")
        try:
            result = benchmark_set(candidates, reference)
        except BenchmarkError as exc:
            raise worker.RpcError("BENCHMARK_ERROR", str(exc)) from exc
        job.update(progress=0.95, message="Benchmark ranking complete")
        return result

    return {"jobId": worker.JOBS.submit("benchmark", operation).id}


worker.METHODS.update({
    "pipeline.validate": pipeline_validate,
    "pipeline.run": pipeline_run,
    "benchmark.analyze": benchmark_analyze,
    "benchmark.compare": benchmark_compare,
})
