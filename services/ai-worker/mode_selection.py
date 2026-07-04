from __future__ import annotations

from typing import Any, Iterable

from mode_types import DEFAULT_ENGINE_ORDER, EngineChoice, ModeError
from spec_catalog import CatalogError, resolve_model


def installed_set(values: Iterable[str] | None) -> set[str]:
    return {str(value).strip().lower() for value in (values or ()) if str(value).strip()}


def auto_choice(stems: list[str], installed: set[str], quality: str) -> EngineChoice:
    candidates = installed or set(DEFAULT_ENGINE_ORDER)
    if len(stems) >= 6 and "demucs" in candidates:
        return EngineChoice("demucs", "htdemucs_6s")
    if quality in {"studio", "maximum"} and "audio-separator" in candidates:
        return EngineChoice("audio-separator", "model_bs_roformer_ep_317_sdr_12.9755.ckpt")
    if "demucs" in candidates:
        return EngineChoice("demucs", "htdemucs_ft" if quality != "fast" else "htdemucs")
    for engine in DEFAULT_ENGINE_ORDER:
        if engine in candidates:
            return EngineChoice(engine)
    raise ModeError("no separation engine is available")


def requested_choices(params: dict[str, Any], installed: set[str]) -> list[EngineChoice]:
    requested = params.get("engines")
    if requested is None:
        engines = [engine for engine in DEFAULT_ENGINE_ORDER if not installed or engine in installed]
    else:
        if not isinstance(requested, list) or not requested:
            raise ModeError("engines must be a non-empty array")
        engines = [str(engine).strip().lower() for engine in requested]
    if len(set(engines)) != len(engines):
        raise ModeError("engines must not contain duplicates")
    if installed:
        unavailable = [engine for engine in engines if engine not in installed]
        if unavailable:
            raise ModeError(f"requested engines are unavailable: {', '.join(unavailable)}")

    raw_models = params.get("models")
    if raw_models is None:
        models: list[str | None] = [None] * len(engines)
    else:
        if not isinstance(raw_models, list) or len(raw_models) != len(engines):
            raise ModeError("models must match the engines array length")
        models = [str(model).strip() or None for model in raw_models]

    choices: list[EngineChoice] = []
    for engine, model in zip(engines, models, strict=True):
        if model:
            try:
                resolved = resolve_model(model)
            except CatalogError:
                resolved = None
            if resolved is not None:
                if resolved.engine != engine:
                    raise ModeError(f"model {model} belongs to {resolved.engine}, not {engine}")
                model = resolved.id
        choices.append(EngineChoice(engine, model))
    return choices
