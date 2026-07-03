from __future__ import annotations

import importlib.util
import json
import math
import os
import shutil
import subprocess
import sys
import threading
import uuid
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable

from omnscript_runtime import execute_script, validate_script


class RpcError(Exception):
    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code
        self.message = message


@dataclass
class Job:
    id: str
    kind: str
    state: str = "queued"
    progress: float = 0.0
    message: str = ""
    result: dict[str, Any] | None = None
    error: dict[str, str] | None = None
    cancel: threading.Event = field(default_factory=threading.Event)

    def snapshot(self) -> dict[str, Any]:
        return {
            "id": self.id,
            "kind": self.kind,
            "state": self.state,
            "progress": self.progress,
            "message": self.message,
            "result": self.result,
            "error": self.error,
        }


class JobManager:
    def __init__(self, workers: int = 2) -> None:
        self._executor = ThreadPoolExecutor(max_workers=max(1, workers), thread_name_prefix="omnistem-ai")
        self._jobs: dict[str, Job] = {}
        self._lock = threading.RLock()

    def submit(self, kind: str, operation: Callable[[Job], dict[str, Any]]) -> Job:
        job = Job(id=str(uuid.uuid4()), kind=kind)
        with self._lock:
            self._jobs[job.id] = job

        def run() -> None:
            job.state = "running"
            try:
                job.result = operation(job)
                job.progress = 1.0
                job.state = "cancelled" if job.cancel.is_set() else "completed"
            except RpcError as exc:
                job.state = "failed"
                job.error = {"code": exc.code, "message": exc.message}
            except Exception as exc:
                job.state = "failed"
                job.error = {"code": "INTERNAL_ERROR", "message": str(exc)}

        self._executor.submit(run)
        return job

    def get(self, job_id: str) -> Job:
        with self._lock:
            if job_id not in self._jobs:
                raise RpcError("JOB_NOT_FOUND", f"Unknown job: {job_id}")
            return self._jobs[job_id]

    def cancel(self, job_id: str) -> Job:
        job = self.get(job_id)
        job.cancel.set()
        job.message = "Cancellation requested"
        return job


JOBS = JobManager()


def _module_available(name: str) -> bool:
    return importlib.util.find_spec(name) is not None


def model_list(_: dict[str, Any]) -> dict[str, Any]:
    demucs_cli = shutil.which("demucs") is not None or _module_available("demucs")
    basic_pitch_cli = shutil.which("basic-pitch") is not None
    return {
        "runtimes": {
            "pytorch": _module_available("torch"),
            "onnxRuntime": _module_available("onnxruntime"),
            "cuda": bool(os.environ.get("CUDA_VISIBLE_DEVICES")) or shutil.which("nvidia-smi") is not None,
        },
        "adapters": [
            {"id": "demucs", "task": "separation", "available": demucs_cli},
            {"id": "basic-pitch", "task": "audio-to-midi", "available": basic_pitch_cli},
            {"id": "numpy-dsp", "task": "restoration/mastering/spectral", "available": _module_available("numpy") and _module_available("soundfile")},
        ],
    }


def health_check(_: dict[str, Any]) -> dict[str, Any]:
    return {
        "status": "ok",
        "service": "omnistem-ai-worker",
        "protocolVersion": 2,
        "capabilities": sorted(METHODS),
        "models": model_list({}),
    }


def _require_file(params: dict[str, Any], key: str = "source") -> Path:
    raw = str(params.get(key, "")).strip()
    if not raw:
        raise RpcError("INVALID_ARGUMENT", f"params.{key} is required")
    path = Path(raw).expanduser().resolve()
    if not path.is_file():
        raise RpcError("FILE_NOT_FOUND", str(path))
    return path


def separation_plan(params: dict[str, Any]) -> dict[str, Any]:
    quality = str(params.get("quality", "balanced"))
    if quality not in {"fast", "balanced", "studio", "ensemble"}:
        raise RpcError("INVALID_ARGUMENT", f"Unsupported quality mode: {quality}")
    stems = params.get("stems") or ["vocals", "drums", "bass", "other"]
    return {
        "quality": quality,
        "requestedStems": stems,
        "backend": "demucs",
        "stages": ["decode", "segment", "separate", "phase-align", "ensemble-fuse", "artifact-repair", "export"],
    }


def _run_process(job: Job, command: list[str], cwd: Path | None = None) -> None:
    process = subprocess.Popen(command, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, encoding="utf-8", errors="replace")
    assert process.stdout is not None
    for line in process.stdout:
        if job.cancel.is_set():
            process.terminate()
            raise RpcError("CANCELLED", "Operation cancelled")
        job.message = line.strip()[-500:]
    code = process.wait()
    if code != 0:
        raise RpcError("PROCESS_FAILED", f"Command exited with code {code}: {' '.join(command)}")


def separation_run(params: dict[str, Any]) -> dict[str, Any]:
    source = _require_file(params)
    output = Path(params.get("outputDir") or source.parent / f"{source.stem}-stems").resolve()
    model = str(params.get("model", "htdemucs"))
    plan = separation_plan(params)

    def operation(job: Job) -> dict[str, Any]:
        output.mkdir(parents=True, exist_ok=True)
        job.progress = 0.05
        if shutil.which("demucs"):
            command = ["demucs", "--out", str(output), "-n", model, str(source)]
        elif _module_available("demucs"):
            command = [sys.executable, "-m", "demucs", "--out", str(output), "-n", model, str(source)]
        else:
            raise RpcError("BACKEND_UNAVAILABLE", "Install the optional Demucs backend")
        _run_process(job, command)
        job.progress = 0.95
        return {"outputDir": str(output), "model": model, "plan": plan}

    job = JOBS.submit("separation", operation)
    return {"jobId": job.id, "plan": plan}


def transcription_plan(params: dict[str, Any]) -> dict[str, Any]:
    return {
        "backend": str(params.get("backend", "basic-pitch")),
        "outputs": ["midi", "note-events", "pitch-curves", "confidence-map"],
        "includePitchBends": bool(params.get("includePitchBends", True)),
    }


def transcription_run(params: dict[str, Any]) -> dict[str, Any]:
    source = _require_file(params)
    output = Path(params.get("outputDir") or source.parent / f"{source.stem}-transcription").resolve()
    plan = transcription_plan(params)

    def operation(job: Job) -> dict[str, Any]:
        executable = shutil.which("basic-pitch")
        if not executable:
            raise RpcError("BACKEND_UNAVAILABLE", "Install Basic Pitch so the basic-pitch command is available")
        output.mkdir(parents=True, exist_ok=True)
        _run_process(job, [executable, str(output), str(source)])
        midi_files = [str(path) for path in output.rglob("*.mid")]
        return {"outputDir": str(output), "midiFiles": midi_files, "plan": plan}

    job = JOBS.submit("transcription", operation)
    return {"jobId": job.id, "plan": plan}


def _load_audio(source: Path) -> tuple[Any, int]:
    if not (_module_available("numpy") and _module_available("soundfile")):
        raise RpcError("BACKEND_UNAVAILABLE", "Install numpy and soundfile for built-in DSP")
    import numpy as np
    import soundfile as sf
    audio, sample_rate = sf.read(source, always_2d=True, dtype="float32")
    if audio.size == 0:
        raise RpcError("INVALID_AUDIO", "Audio file contains no samples")
    return np.asarray(audio, dtype=np.float32), int(sample_rate)


def _write_audio(path: Path, audio: Any, sample_rate: int) -> None:
    import soundfile as sf
    path.parent.mkdir(parents=True, exist_ok=True)
    sf.write(path, audio, sample_rate, subtype="FLOAT")


def restoration_run(params: dict[str, Any]) -> dict[str, Any]:
    source = _require_file(params)
    output = Path(params.get("output") or source.with_name(source.stem + "-restored.wav")).resolve()
    strength = max(0.0, min(float(params.get("strength", 0.35)), 1.0))

    def operation(job: Job) -> dict[str, Any]:
        import numpy as np
        audio, sample_rate = _load_audio(source)
        job.progress = 0.2
        audio = audio - np.mean(audio, axis=0, keepdims=True)
        noise_floor = np.quantile(np.abs(audio), 0.15, axis=0, keepdims=True)
        threshold = noise_floor * (1.5 + 4.0 * strength)
        gain = np.clip((np.abs(audio) - threshold) / (threshold + 1e-7), 0.0, 1.0)
        audio = audio * ((1.0 - strength * 0.7) + gain * strength * 0.7)
        job.progress = 0.7
        peak = float(np.max(np.abs(audio)))
        if peak > 1.0:
            audio /= peak
        _write_audio(output, audio, sample_rate)
        return {"output": str(output), "processors": ["dc-removal", "adaptive-gate", "peak-protection"]}

    job = JOBS.submit("restoration", operation)
    return {"jobId": job.id}


def mastering_run(params: dict[str, Any]) -> dict[str, Any]:
    source = _require_file(params)
    output = Path(params.get("output") or source.with_name(source.stem + "-mastered.wav")).resolve()
    target_peak_db = float(params.get("targetPeakDb", -1.0))
    drive = max(0.0, min(float(params.get("drive", 0.15)), 1.0))

    def operation(job: Job) -> dict[str, Any]:
        import numpy as np
        audio, sample_rate = _load_audio(source)
        job.progress = 0.25
        audio = np.tanh(audio * (1.0 + drive * 3.0)) / math.tanh(1.0 + drive * 3.0)
        peak = float(np.max(np.abs(audio))) + 1e-12
        target = 10.0 ** (target_peak_db / 20.0)
        audio = audio * min(target / peak, 8.0)
        job.progress = 0.8
        _write_audio(output, audio, sample_rate)
        return {"output": str(output), "targetPeakDb": target_peak_db, "processors": ["soft-saturation", "peak-normalization"]}

    job = JOBS.submit("mastering", operation)
    return {"jobId": job.id}


def spectral_mask(params: dict[str, Any]) -> dict[str, Any]:
    source = _require_file(params)
    output = Path(params.get("output") or source.with_name(source.stem + "-spectral.wav")).resolve()
    start = max(0.0, float(params.get("startSeconds", 0.0)))
    end = float(params.get("endSeconds", start + 1.0))
    low_hz = max(0.0, float(params.get("lowHz", 0.0)))
    high_hz = float(params.get("highHz", 20000.0))
    gain_db = float(params.get("gainDb", -12.0))

    def operation(job: Job) -> dict[str, Any]:
        import numpy as np
        audio, sample_rate = _load_audio(source)
        n_fft, hop = 2048, 512
        window = np.hanning(n_fft).astype(np.float32)
        padded = np.pad(audio, ((0, n_fft), (0, 0)))
        output_audio = np.zeros_like(padded)
        weight = np.zeros((padded.shape[0], 1), dtype=np.float32)
        total = max(1, (padded.shape[0] - n_fft) // hop)
        for index, offset in enumerate(range(0, padded.shape[0] - n_fft, hop)):
            if job.cancel.is_set():
                raise RpcError("CANCELLED", "Operation cancelled")
            frame = padded[offset:offset+n_fft] * window[:, None]
            spectrum = np.fft.rfft(frame, axis=0)
            frame_time = offset / sample_rate
            if start <= frame_time <= end:
                bins = np.fft.rfftfreq(n_fft, 1.0 / sample_rate)
                selection = (bins >= low_hz) & (bins <= high_hz)
                spectrum[selection] *= 10.0 ** (gain_db / 20.0)
            reconstructed = np.fft.irfft(spectrum, n=n_fft, axis=0).real.astype(np.float32) * window[:, None]
            output_audio[offset:offset+n_fft] += reconstructed
            weight[offset:offset+n_fft] += window[:, None] ** 2
            if index % 32 == 0:
                job.progress = min(0.95, index / total)
        output_audio = output_audio[:audio.shape[0]] / np.maximum(weight[:audio.shape[0]], 1e-6)
        _write_audio(output, output_audio, sample_rate)
        return {"output": str(output), "mask": {"startSeconds": start, "endSeconds": end, "lowHz": low_hz, "highHz": high_hz, "gainDb": gain_db}}

    job = JOBS.submit("spectral-mask", operation)
    return {"jobId": job.id}


def replacement_plan(params: dict[str, Any]) -> dict[str, Any]:
    instrument = str(params.get("instrument", "piano"))
    return {
        "instrument": instrument,
        "analysis": ["note-events", "timing", "velocity", "pitch-bend", "articulation"],
        "render": ["midi-or-plugin", "expression-transfer", "transient-alignment", "blend"],
        "requires": ["transcribed-note-events", "licensed-target-instrument-or-plugin"],
    }


def assistant_plan(params: dict[str, Any]) -> dict[str, Any]:
    instruction = str(params.get("instruction", "")).strip()
    if not instruction:
        raise RpcError("INVALID_ARGUMENT", "params.instruction is required")
    text = instruction.lower()
    actions: list[dict[str, Any]] = []
    if any(word in text for word in ("separate", "stem", "isolate")):
        actions.append({"method": "separation.run", "reason": "source separation requested"})
    if any(word in text for word in ("midi", "note", "transcrib")):
        actions.append({"method": "transcription.run", "reason": "note extraction requested"})
    if any(word in text for word in ("noise", "clean", "restore", "click", "reverb")):
        actions.append({"method": "restoration.run", "reason": "restoration requested"})
    if any(word in text for word in ("master", "loud", "release")):
        actions.append({"method": "mastering.run", "reason": "mastering requested"})
    if any(word in text for word in ("replace", "instrument", "voice")):
        actions.append({"method": "replacement.plan", "reason": "replacement requested"})
    if not actions:
        actions.append({"method": "assistant.review", "reason": "manual review required"})
    return {"instruction": instruction, "actions": actions, "nonDestructive": True, "requiresConfirmationBeforeRender": True}


def script_validate(params: dict[str, Any]) -> dict[str, Any]:
    result = validate_script(str(params.get("source", "")))
    return {"valid": result.valid, "errors": result.errors}


def script_run(params: dict[str, Any]) -> dict[str, Any]:
    source = str(params.get("source", ""))
    context = params.get("context") or {}
    if not isinstance(context, dict):
        raise RpcError("INVALID_ARGUMENT", "params.context must be an object")
    return execute_script(source, context, float(params.get("timeoutSeconds", 2.0)))


def job_status(params: dict[str, Any]) -> dict[str, Any]:
    return JOBS.get(str(params.get("jobId", ""))).snapshot()


def job_cancel(params: dict[str, Any]) -> dict[str, Any]:
    return JOBS.cancel(str(params.get("jobId", ""))).snapshot()


METHODS: dict[str, Callable[[dict[str, Any]], dict[str, Any]]] = {
    "health.check": health_check,
    "model.list": model_list,
    "job.status": job_status,
    "job.cancel": job_cancel,
    "separation.plan": separation_plan,
    "separation.run": separation_run,
    "transcription.plan": transcription_plan,
    "transcription.run": transcription_run,
    "restoration.run": restoration_run,
    "mastering.run": mastering_run,
    "spectral.mask": spectral_mask,
    "replacement.plan": replacement_plan,
    "assistant.plan": assistant_plan,
    "script.validate": script_validate,
    "script.run": script_run,
}


def handle(payload: dict[str, Any]) -> dict[str, Any]:
    request_id = payload.get("id")
    method = str(payload.get("method", ""))
    params = payload.get("params", {})
    if method not in METHODS:
        raise RpcError("METHOD_NOT_FOUND", f"Unknown method: {method}")
    if not isinstance(params, dict):
        raise RpcError("INVALID_ARGUMENT", "params must be an object")
    return {"id": request_id, "result": METHODS[method](params)}


def main() -> int:
    for raw_line in sys.stdin:
        line = raw_line.strip()
        if not line:
            continue
        request_id: Any = None
        try:
            payload = json.loads(line)
            if not isinstance(payload, dict):
                raise RpcError("INVALID_REQUEST", "request must be a JSON object")
            request_id = payload.get("id")
            response = handle(payload)
        except json.JSONDecodeError as exc:
            response = {"id": request_id, "error": {"code": "PARSE_ERROR", "message": str(exc)}}
        except RpcError as exc:
            response = {"id": request_id, "error": {"code": exc.code, "message": exc.message}}
        except Exception as exc:
            response = {"id": request_id, "error": {"code": "INTERNAL_ERROR", "message": str(exc)}}
        print(json.dumps(response, separators=(",", ":")), flush=True)
    return 0
