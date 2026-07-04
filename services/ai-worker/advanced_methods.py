from __future__ import annotations

from pathlib import Path
from typing import Any

import worker
from benchmarking import BenchmarkError, analyze_file, benchmark_set
from engine_runtime import EngineRuntimeError, execute as execute_engine
from mode_planner import plan_mode
from mode_types import ModeError
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


def _consensus_scores(run_results: dict[str, dict[str, Any]], fused_stems: dict[str, str]) -> dict[str, Any]:
    import numpy as np

    scores: dict[str, list[dict[str, Any]]] = {}
    for stem, fused_path in fused_stems.items():
        fused_audio, fused_rate = worker._load_audio(Path(fused_path))
        stem_scores: list[dict[str, Any]] = []
        for run_id, result in run_results.items():
            candidate_path = (result.get("stems") or {}).get(stem)
            if not candidate_path:
                continue
            candidate_audio, candidate_rate = worker._load_audio(Path(candidate_path))
            if candidate_rate != fused_rate or candidate_audio.shape[1] != fused_audio.shape[1]:
                continue
            length = min(candidate_audio.shape[0], fused_audio.shape[0])
            candidate = candidate_audio[:length].reshape(-1).astype(np.float64)
            fused = fused_audio[:length].reshape(-1).astype(np.float64)
            error = candidate - fused
            rmse = float(np.sqrt(np.mean(error * error)))
            candidate_centered = candidate - float(np.mean(candidate))
            fused_centered = fused - float(np.mean(fused))
            denominator = float(np.sqrt(np.sum(candidate_centered ** 2) * np.sum(fused_centered ** 2)))
            correlation = float(np.sum(candidate_centered * fused_centered)) / denominator if denominator > 0.0 else 0.0
            stem_scores.append({
                "runId": run_id,
                "correlation": max(-1.0, min(1.0, correlation)),
                "rmse": rmse,
            })
        scores[stem] = sorted(stem_scores, key=lambda item: (-item["correlation"], item["rmse"]))
    return scores


def mode_run(params: dict[str, Any]) -> dict[str, Any]:
    raw_source = str(params.get("source", "")).strip()
    if not raw_source:
        raise worker.RpcError("INVALID_ARGUMENT", "params.source is required")
    source = Path(raw_source).expanduser().resolve()
    if not source.is_file():
        raise worker.RpcError("FILE_NOT_FOUND", str(source))
    installed = params.get("installedEngines")
    if installed is not None and not isinstance(installed, list):
        raise worker.RpcError("INVALID_ARGUMENT", "params.installedEngines must be an array")
    try:
        plan = plan_mode(params, installed_engines=installed)
    except ModeError as exc:
        raise worker.RpcError("MODE_ERROR", str(exc)) from exc

    output_root = Path(params.get("outputDir") or source.parent / f"{source.stem}-{plan['mode']}").resolve()

    def operation(job: worker.Job) -> dict[str, Any]:
        output_root.mkdir(parents=True, exist_ok=True)
        run_results: dict[str, dict[str, Any]] = {}
        fusion_result: dict[str, Any] | None = None
        benchmark_result: dict[str, Any] | None = None
        total_steps = max(1, len(plan["steps"]))

        for index, step in enumerate(plan["steps"]):
            if job.cancel.is_set():
                raise worker.RpcError("CANCELLED", "Operation cancelled")
            job.update(progress=index / total_steps, message=f"Running {step['id']}")

            if step["kind"] == "separate":
                step_source = source
                source_from = step.get("sourceFrom")
                if source_from:
                    previous = run_results.get(str(source_from))
                    stem_name = str(step.get("sourceStem") or plan["stems"][0])
                    previous_path = (previous or {}).get("stems", {}).get(stem_name)
                    if not previous_path:
                        raise worker.RpcError("CASCADE_SOURCE_MISSING", f"{source_from} did not produce stem {stem_name}")
                    step_source = Path(previous_path)

                engine_params = {
                    "source": str(step_source),
                    "outputDir": str(output_root / step["id"]),
                    "engine": step["engine"],
                    "model": step.get("model") or "",
                    "stems": step["stems"],
                    "device": step["device"],
                    "outputFormat": step["outputFormat"],
                    "overwrite": bool(params.get("overwrite", False)),
                }

                def run_process(command: list[str]) -> None:
                    worker._run_process(job, command)

                try:
                    run_results[step["id"]] = execute_engine(engine_params, run_process)
                except (ValueError, EngineRuntimeError) as exc:
                    raise worker.RpcError("ENGINE_ERROR", f"{step['id']}: {exc}") from exc

            elif step["kind"] == "fuse":
                inputs = [run_results[input_id] for input_id in step["inputs"]]
                path_maps = [
                    {stem: Path(path) for stem, path in (result.get("stems") or {}).items()}
                    for result in inputs
                ]
                weights = params.get("weights")
                if weights is not None:
                    if not isinstance(weights, list) or len(weights) != len(path_maps):
                        raise worker.RpcError("INVALID_ARGUMENT", "weights must match the engine result count")
                    # Existing fusion is equal-weight; weighted fusion remains explicit future work.
                    if len({float(value) for value in weights}) != 1:
                        raise worker.RpcError("UNSUPPORTED_WEIGHTING", "current production fusion supports equal weights only")
                stems, missing = worker._fuse_stems(job, path_maps, output_root, step["stems"])
                fusion_result = {"stems": stems, "missingStems": missing, "strategy": "polarity-aligned-mean"}

            elif step["kind"] == "benchmark":
                if fusion_result is None:
                    raise worker.RpcError("BENCHMARK_REFERENCE_MISSING", "God Mode benchmark requires fused stems")
                benchmark_result = {"consensus": _consensus_scores(run_results, fusion_result["stems"])}

        return {
            "mode": plan["mode"],
            "source": str(source),
            "outputDir": str(output_root),
            "plan": plan,
            "runs": run_results,
            "fusion": fusion_result,
            "benchmark": benchmark_result,
        }

    return {"jobId": worker.JOBS.submit(f"mode-{plan['mode']}", operation).id, "plan": plan}


worker.METHODS.update({
    "pipeline.validate": pipeline_validate,
    "pipeline.run": pipeline_run,
    "benchmark.analyze": benchmark_analyze,
    "benchmark.compare": benchmark_compare,
    "mode.run": mode_run,
})
