from __future__ import annotations

import importlib.util
import shutil
import sys
from pathlib import Path
from typing import Any, Callable

from engine_commands import build_command


class EngineRuntimeError(RuntimeError):
    pass


def resolve_command(command: list[str]) -> list[str]:
    if not command:
        raise EngineRuntimeError("empty engine command")
    executable = shutil.which(command[0])
    if executable:
        return [executable, *command[1:]]
    module_fallbacks = {"demucs": "demucs", "spleeter": "spleeter"}
    module_name = module_fallbacks.get(command[0])
    if module_name and importlib.util.find_spec(module_name) is not None:
        return [sys.executable, "-m", module_name, *command[1:]]
    raise EngineRuntimeError(f"engine executable is unavailable: {command[0]}")


def discover_outputs(output_dir: Path) -> dict[str, Any]:
    extensions = {".wav", ".flac", ".mp3", ".ogg", ".m4a", ".wma", ".aiff", ".aif", ".caf"}
    files = sorted(path for path in output_dir.rglob("*") if path.is_file() and path.suffix.lower() in extensions)
    stems: dict[str, str] = {}
    for path in files:
        stems.setdefault(path.stem.lower(), str(path))
    return {"files": [str(path) for path in files], "stems": stems}


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
