from __future__ import annotations

from typing import Any, Iterable

from mode_selection import auto_choice, installed_set, requested_choices
from mode_types import SUPPORTED_MODES, ModeError
from spec_catalog import CatalogError, normalize_stems, resolve_model


def _common(params: dict[str, Any]) -> dict[str, Any]:
    raw_stems = params.get("stems") or ["vocals", "instrumental"]
    if not isinstance(raw_stems, list):
        raise ModeError("stems must be an array")
    try:
        stems = normalize_stems(str(stem) for stem in raw_stems)
    except CatalogError as exc:
        raise ModeError(str(exc)) from exc

    quality = str(params.get("quality", "balanced")).strip().lower()
    if quality not in {"fast", "balanced", "studio", "maximum"}:
        raise ModeError("quality must be fast, balanced, studio, or maximum")
    output_format = str(params.get("outputFormat", "wav")).strip().lower().lstrip(".")
    if output_format not in {"wav", "flac", "mp3", "ogg", "m4a", "aiff", "caf"}:
        raise ModeError(f"unsupported output format: {output_format}")
    return {
        "stems": stems,
        "quality": quality,
        "outputFormat": output_format,
        "device": str(params.get("device", "auto")).strip().lower(),
    }


def _standard_step(params: dict[str, Any], installed: set[str], common: dict[str, Any]) -> dict[str, Any]:
    engine = str(params.get("engine", "demucs")).strip().lower()
    if installed and engine not in installed:
        raise ModeError(f"engine is unavailable: {engine}")
    model = str(params.get("model", "")).strip() or None
    if model:
        try:
            resolved = resolve_model(model)
        except CatalogError:
            resolved = None
        if resolved is not None:
            if resolved.engine != engine:
                raise ModeError(f"model {model} belongs to {resolved.engine}, not {engine}")
            model = resolved.id
    return {"id": "standard-1", "kind": "separate", "engine": engine, "model": model, **common}


def plan_mode(params: dict[str, Any], installed_engines: Iterable[str] | None = None) -> dict[str, Any]:
    mode = str(params.get("mode", "standard")).strip().lower()
    if mode not in SUPPORTED_MODES:
        raise ModeError(f"unsupported mode: {mode}")

    installed = installed_set(installed_engines)
    common = _common(params)
    steps: list[dict[str, Any]] = []

    if mode == "standard":
        steps.append(_standard_step(params, installed, common))

    elif mode == "auto":
        choice = auto_choice(common["stems"], installed, common["quality"])
        steps.append({"id": "auto-1", "kind": "separate", "engine": choice.engine, "model": choice.model, **common})

    elif mode in {"comparison", "ensemble", "god"}:
        choices = requested_choices(params, installed)
        minimum = 3 if mode == "god" else 2
        if len(choices) < minimum:
            raise ModeError(f"{mode} mode requires at least {minimum} engines")
        for index, choice in enumerate(choices):
            steps.append({
                "id": f"{mode}-{index + 1}",
                "kind": "separate",
                "engine": choice.engine,
                "model": choice.model,
                **common,
            })
        if mode in {"ensemble", "god"}:
            steps.append({
                "id": f"{mode}-fusion",
                "kind": "fuse",
                "strategy": str(params.get("fusion", "weighted-mean")),
                "inputs": [step["id"] for step in steps],
                "stems": common["stems"],
            })
        if mode == "god":
            steps.append({
                "id": "god-benchmark",
                "kind": "benchmark",
                "inputs": [step["id"] for step in steps if step["kind"] == "separate"],
                "rankBy": str(params.get("rankBy", "consensus")),
            })

    else:
        choices = requested_choices(params, installed)
        if len(choices) < 2:
            raise ModeError("cascade mode requires at least two engines")
        cascade_stem = str(params.get("cascadeStem", common["stems"][0])).strip()
        try:
            cascade_stem = normalize_stems([cascade_stem])[0]
        except CatalogError as exc:
            raise ModeError(str(exc)) from exc
        previous: str | None = None
        for index, choice in enumerate(choices):
            step_id = f"cascade-{index + 1}"
            steps.append({
                "id": step_id,
                "kind": "separate",
                "engine": choice.engine,
                "model": choice.model,
                "sourceFrom": previous,
                "sourceStem": cascade_stem if previous else None,
                **common,
            })
            previous = step_id

    return {
        "mode": mode,
        "stems": common["stems"],
        "quality": common["quality"],
        "outputFormat": common["outputFormat"],
        "steps": steps,
        "requiresInstalledEngines": sorted({step["engine"] for step in steps if step["kind"] == "separate"}),
        "terminalStep": steps[-1]["id"],
    }
