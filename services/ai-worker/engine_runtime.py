from __future__ import annotations

import importlib.util
import re
import shutil
import sys
from pathlib import Path
from typing import Any, Callable

from engine_commands import build_command
from engine_registry import ENGINES


class EngineRuntimeError(RuntimeError):
    pass


ENGINE_EXECUTABLES = {
    "audio-separator": "audio-separator",
    "demucs": "demucs",
    "spleeter": "spleeter",
    "openunmix": "umx",
}
ENGINE_MODULE_FALLBACKS: dict[str, str | None] = {
    "audio-separator": None,
    "demucs": "demucs",
    "spleeter": "spleeter",
    "openunmix": None,
}


def _module_available(module_name: str | None) -> bool:
    return bool(module_name and importlib.util.find_spec(module_name) is not None)


def engine_status(engine_id: str) -> dict[str, Any]:
    if engine_id not in ENGINES:
        raise EngineRuntimeError(f"unknown engine: {engine_id}")
    executable_name = ENGINE_EXECUTABLES[engine_id]
    executable = shutil.which(executable_name)
    module_name = ENGINE_MODULE_FALLBACKS[engine_id]
    module_available = _module_available(module_name)
    return {
        "id": engine_id,
        "installed": bool(executable or module_available),
        "executable": executable,
        "module": module_name if module_available else None,
    }


def installed_engine_ids() -> list[str]:
    return [engine_id for engine_id in ENGINES if engine_status(engine_id)["installed"]]


def resolve_command(command: list[str]) -> list[str]:
    if not command:
        raise EngineRuntimeError("empty engine command")
    executable = shutil.which(command[0])
    if executable:
        return [executable, *command[1:]]
    command_to_engine = {value: key for key, value in ENGINE_EXECUTABLES.items()}
    engine_id = command_to_engine.get(command[0])
    module_name = ENGINE_MODULE_FALLBACKS.get(engine_id or "")
    if _module_available(module_name):
        return [sys.executable, "-m", str(module_name), *command[1:]]
    raise EngineRuntimeError(f"engine executable is unavailable: {command[0]}")


def canonical_output_stem(path: Path) -> str:
    text = " ".join([path.stem, path.parent.name]).lower()
    normalized = re.sub(r"[^a-z0-9]+", "_", text).strip("_")
    tokens = set(part for part in normalized.split("_") if part)

    if "backing" in tokens and ({"vocal", "vocals"} & tokens):
        return "backing_vocals"
    if (
        "instrumental" in tokens
        or "accompaniment" in tokens
        or "karaoke" in tokens
        or "novocals" in tokens
        or "no_vocals" in normalized
        or "without_vocals" in normalized
    ):
        return "instrumental"
    if {"vocal", "vocals", "voice"} & tokens:
        return "vocals"
    if {"drum", "drums", "percussion"} & tokens:
        return "drums"
    if {"bass", "bassline"} & tokens:
        return "bass"
    if {"guitar", "guitars"} & tokens:
        return "guitar"
    if {"piano", "keys", "keyboard"} & tokens:
        return "piano"
    if {"string", "strings"} & tokens:
        return "strings"
    if {"wind", "winds"} & tokens:
        return "wind"
    if {"brass", "horn", "horns"} & tokens:
        return "brass"
    if "other" in tokens:
        return "other"
    return path.stem.lower()


def _stem_priority(path: Path, canonical: str) -> tuple[int, int, str]:
    exact = path.stem.lower() == canonical
    return (0 if exact else 1, len(path.parts), str(path).casefold())


def discover_outputs(output_dir: Path) -> dict[str, Any]:
    extensions = {".wav", ".flac", ".mp3", ".ogg", ".m4a", ".wma", ".aiff", ".aif", ".caf"}
    files = sorted(path for path in output_dir.rglob("*") if path.is_file() and path.suffix.lower() in extensions)
    candidates: dict[str, list[Path]] = {}
    for path in files:
        canonical = canonical_output_stem(path)
        candidates.setdefault(canonical, []).append(path)

    stems: dict[str, str] = {}
    collisions: dict[str, list[str]] = {}
    for canonical, paths in candidates.items():
        ordered = sorted(paths, key=lambda path: _stem_priority(path, canonical))
        stems[canonical] = str(ordered[0])
        if len(ordered) > 1:
            collisions[canonical] = [str(path) for path in ordered]

    return {
        "files": [str(path) for path in files],
        "stems": stems,
        "rawStems": {path.stem.lower(): str(path) for path in files},
        "collisions": collisions,
    }


def execute(
    params: dict[str, Any],
    run_process: Callable[[list[str]], None],
) -> dict[str, Any]:
    plan = build_command(params)
    output_dir = Path(plan["outputDir"])
    output_dir.mkdir(parents=True, exist_ok=True)
    command = resolve_command(list(plan["command"]))
    run_process(command)
    outputs = discover_outputs(output_dir)
    if not outputs["files"]:
        raise EngineRuntimeError("engine completed without producing an audio file")
    return {"plan": plan, **outputs}
