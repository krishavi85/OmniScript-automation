from __future__ import annotations

from pathlib import Path
from typing import Any

from engine_registry import ENGINES


class EngineCommandError(ValueError):
    pass


def _parse_stems(params: dict[str, Any]) -> tuple[str, ...]:
    raw = params.get("stems") or ["vocals", "instrumental"]
    if not isinstance(raw, list):
        raise EngineCommandError("stems must be an array")
    stems = tuple(str(value).strip().lower() for value in raw if str(value).strip())
    if not stems:
        raise EngineCommandError("at least one stem is required")
    return stems


def build_command(params: dict[str, Any]) -> dict[str, Any]:
    engine_id = str(params.get("engine", "demucs")).strip().lower()
    if engine_id not in ENGINES:
        raise EngineCommandError(f"unsupported engine: {engine_id}")
    spec = ENGINES[engine_id]

    source = Path(str(params.get("source", ""))).expanduser().resolve()
    if not source.is_file():
        raise EngineCommandError(f"source file does not exist: {source}")
    if source.suffix.lower() not in spec.input_extensions:
        raise EngineCommandError(f"{spec.display_name} does not support {source.suffix or 'this input type'}")

    output = Path(params.get("outputDir") or source.parent / f"{source.stem}-{engine_id}").expanduser().resolve()
    overwrite = bool(params.get("overwrite", False))
    if output.exists() and any(output.iterdir()) and not overwrite:
        raise EngineCommandError("output directory is not empty; enable overwrite explicitly")

    output_format = str(params.get("outputFormat", "wav")).strip().lower().lstrip(".")
    if output_format not in spec.output_formats:
        raise EngineCommandError(f"{spec.display_name} does not support output format {output_format}")

    stems = _parse_stems(params)
    if len(stems) not in spec.supported_stem_counts:
        raise EngineCommandError(f"{spec.display_name} does not support {len(stems)} requested stems")

    model = str(params.get("model", "")).strip()
    device = str(params.get("device", "auto")).strip().lower()
    command: list[str]

    if engine_id == "audio-separator":
        if device != "auto":
            raise EngineCommandError("Audio Separator uses automatic device selection")
        command = [
            "audio-separator", str(source), "--output_dir", str(output),
            "--output_format", output_format.upper(),
        ]
        if model:
            command += ["--model_filename", model]
        if len(stems) == 1:
            command += ["--single_stem", stems[0].replace("_", " ").title()]

    elif engine_id == "demucs":
        command = ["demucs", "--out", str(output)]
        if model:
            command += ["--name", model]
        if device != "auto":
            if device not in {"cpu", "cuda", "mps"}:
                raise EngineCommandError("Demucs device must be auto, cpu, cuda, or mps")
            command += ["--device", device]
        requested = set(stems)
        if "instrumental" in requested and len(requested) == 2:
            target = next(iter(requested - {"instrumental"}))
            if target not in {"vocals", "drums", "bass", "other", "guitar", "piano"}:
                raise EngineCommandError(f"Demucs cannot create a two-stem split for {target}")
            command += ["--two-stems", target]
        if output_format == "flac":
            command.append("--flac")
        elif output_format == "mp3":
            command.append("--mp3")
        command.append(str(source))

    elif engine_id == "spleeter":
        preset = model or f"spleeter:{len(stems)}stems"
        command = [
            "spleeter", "separate", "-p", preset, "-o", str(output),
            "-c", output_format, str(source),
        ]

    else:
        if device not in {"auto", "cpu", "cuda"}:
            raise EngineCommandError("Open-Unmix device must be auto, cpu, or cuda")
        requested = set(stems)
        native_targets = {"vocals", "drums", "bass", "other"}
        if requested != {"vocals", "instrumental"} and requested - native_targets:
            raise EngineCommandError("Open-Unmix supports native targets or vocals plus instrumental")
        command = ["umx", str(source), "--outdir", str(output), "--ext", f".{output_format}"]
        if model:
            command += ["--model", model]
        if device == "cpu":
            command.append("--no-cuda")
        if requested == {"vocals", "instrumental"}:
            command += ["--targets", "vocals", "--residual", "instrumental"]
        else:
            command += ["--targets", *stems]

    return {
        "engine": engine_id,
        "source": str(source),
        "outputDir": str(output),
        "outputFormat": output_format,
        "stems": list(stems),
        "model": model or None,
        "device": device,
        "command": command,
    }
