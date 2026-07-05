from __future__ import annotations

import hashlib
import os
import shutil
import subprocess
from pathlib import Path
from typing import Any

import worker


def _hash(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _tool(format_name: str) -> str:
    variable = "OMNISTEM_CLAP_VALIDATOR" if format_name == "clap" else "OMNISTEM_VST3_VALIDATOR"
    configured = os.environ.get(variable, "").strip()
    candidate = configured or ("clap-validator" if format_name == "clap" else "validator")
    resolved = shutil.which(candidate)
    if resolved is None:
        raise worker.RpcError("VALIDATOR_UNAVAILABLE", f"Configure {variable} with the official validator path")
    return resolved


def _format(path: Path, requested: str) -> str:
    value = requested.strip().lower()
    if not value:
        value = "clap" if path.suffix.lower() == ".clap" else "vst3"
    if value not in {"clap", "vst3"}:
        raise worker.RpcError("INVALID_ARGUMENT", "format must be clap or vst3")
    return value


def validate_plugin(params: dict[str, Any]) -> dict[str, Any]:
    plugin = worker._require_file(params, "plugin")
    format_name = _format(plugin, str(params.get("format", "")))
    timeout = max(5.0, min(float(params.get("timeoutSeconds", 180.0)), 600.0))

    def operation(job: worker.Job) -> dict[str, Any]:
        executable = _tool(format_name)
        command = [executable, "validate", str(plugin)] if format_name == "clap" else [executable, str(plugin)]
        job.update(progress=0.1, message=f"Running {format_name.upper()} validator")
        try:
            completed = subprocess.run(
                command,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=timeout,
                check=False,
            )
        except subprocess.TimeoutExpired as exc:
            raise worker.RpcError("VALIDATION_TIMEOUT", f"Plugin validation exceeded {timeout:g} seconds") from exc
        output = (completed.stdout + "\n" + completed.stderr).strip()
        report = {
            "plugin": str(plugin),
            "format": format_name,
            "sha256": _hash(plugin),
            "validator": executable,
            "exitCode": completed.returncode,
            "compatible": completed.returncode == 0,
            "output": output[-50000:],
        }
        if completed.returncode != 0:
            job.update(message="Validator reported compatibility failures")
        return report

    return {"jobId": worker.JOBS.submit(f"{format_name}-compatibility", operation).id}


worker.METHODS["plugin.compatibility.validate"] = validate_plugin
