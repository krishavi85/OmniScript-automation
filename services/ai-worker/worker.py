from __future__ import annotations

import atexit
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
    lock: threading.RLock = field(default_factory=threading.RLock, repr=False)

    def update(self, *, progress: float | None = None, message: str | None = None) -> None:
        with self.lock:
            if progress is not None:
                self.progress = max(0.0, min(float(progress), 1.0))
            if message is not None:
                self.message = message

    def snapshot(self) -> dict[str, Any]:
        with self.lock:
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
        self._closed = False

    def submit(self, kind: str, operation: Callable[[Job], dict[str, Any]]) -> Job:
        if not callable(operation):
            raise RpcError("INVALID_ARGUMENT", "Job operation must be callable")
        with self._lock:
            if self._closed:
                raise RpcError("WORKER_SHUTDOWN", "The job manager is shutting down")
            job = Job(id=str(uuid.uuid4()), kind=kind)
            self._jobs[job.id] = job

        def run() -> None:
            with job.lock:
                if job.cancel.is_set():
                    job.state = "cancelled"
                    job.message = "Cancelled before execution"
                    return
                job.state = "running"
            try:
                result = operation(job)
                with job.lock:
                    job.result = result
                    job.progress = 1.0
                    job.state = "cancelled" if job.cancel.is_set() else "completed"
            except RpcError as exc:
                with job.lock:
                    job.state = "cancelled" if exc.code == "CANCELLED" else "failed"
                    job.error = None if exc.code == "CANCELLED" else {"code": exc.code, "message": exc.message}
                    job.message = exc.message
            except Exception as exc:
                with job.lock:
                    job.state = "failed"
                    job.error = {"code": "INTERNAL_ERROR", "message": str(exc)}
                    job.message = str(exc)

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
        job.update(message="Cancellation requested")
        return job

    def shutdown(self) -> None:
        with self._lock:
            if self._closed:
                return
            self._closed = True
            jobs = list(self._jobs.values())
        for job in jobs:
            job.cancel.set()
        self._executor.shutdown(wait=False, cancel_futures=True)


JOBS = JobManager()
atexit.register(JOBS.shutdown)


def _module_available(name: str) -> bool:
    return importlib.util.find_spec(name) is not None


def model_list(_: dict[str, Any]) -> dict[str, Any]:
    return {
        "runtimes": {
            "pytorch": _module_available("torch"),
            "onnxRuntime": _module_available("onnxruntime"),
            "cuda": bool(os.environ.get("CUDA_VISIBLE_DEVICES")) or shutil.which("nvidia-smi") is not None,
        },
        "adapters": [
            {"id": "demucs", "task": "separation", "available": shutil.which("demucs") is not None or _module_available("demucs")},
            {"id": "basic-pitch", "task": "audio-to-midi", "available": shutil.which("basic-pitch") is not None or _module_available("basic_pitch")},
            {"id": "numpy-dsp", "task": "restoration/mastering/spectral/ensemble-fusion", "available": _module_available("numpy") and _module_available("soundfile")},
        ],
    }


def health_check(_: dict[str, Any]) -> dict[str, Any]:
    return {
        "status": "ok",
        "service": "omnistem-ai-worker",
        "protocolVersion": 3,
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


def _finite_float(params: dict[str, Any], key: str, default: float) -> float:
    value = float(params.get(key, default))
    if not math.isfinite(value):
        raise RpcError("INVALID_ARGUMENT", f"params.{key} must be finite")
    return value


def separation_plan(params: dict[str, Any]) -> dict[str, Any]:
    quality = str(params.get("quality", "balanced"))
    if quality not in {"fast", "balanced", "studio", "ensemble"}:
        raise RpcError("INVALID_ARGUMENT", f"Unsupported quality mode: {quality}")
    stems = params.get("stems") or ["vocals", "drums", "bass", "other"]
    if not isinstance(stems, list) or not all(isinstance(stem, str) and stem for stem in stems):
        raise RpcError("INVALID_ARGUMENT", "params.stems must be a list of non-empty strings")
    requested_models = params.get("models")
    if requested_models is None:
        models = ["htdemucs", "htdemucs_ft"] if quality == "ensemble" else [str(params.get("model", "htdemucs"))]
    else:
        if not isinstance(requested_models, list) or not requested_models or len(requested_models) > 4:
            raise RpcError("INVALID_ARGUMENT", "params.models must contain one to four model names")
        models = [str(model).strip() for model in requested_models]
        if not all(models):
            raise RpcError("INVALID_ARGUMENT", "Model names must not be empty")
    stages = ["decode", "demucs-inference", "export"]
    if quality == "ensemble":
        stages = ["decode", "multi-model-demucs-inference", "polarity-alignment", "sample-aligned-mean-fusion", "export"]
    return {"quality": quality, "requestedStems": stems, "backend": "demucs", "models": models, "stages": stages}


def _run_process(job: Job, command: list[str], cwd: Path | None = None) -> None:
    try:
        process = subprocess.Popen(
            command, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, encoding="utf-8", errors="replace",
        )
    except OSError as exc:
        raise RpcError("PROCESS_START_FAILED", str(exc)) from exc
    assert process.stdout is not None
    try:
        for line in process.stdout:
            if job.cancel.is_set():
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=3)
                raise RpcError("CANCELLED", "Operation cancelled")
            job.update(message=line.strip()[-500:])
        code = process.wait()
    finally:
        process.stdout.close()
    if code != 0:
        raise RpcError("PROCESS_FAILED", f"Command exited with code {code}: {' '.join(command)}")


def _demucs_command(model: str, output: Path, source: Path) -> list[str]:
    if shutil.which("demucs"):
        return ["demucs", "--out", str(output), "-n", model, str(source)]
    if _module_available("demucs"):
        return [sys.executable, "-m", "demucs", "--out", str(output), "-n", model, str(source)]
    raise RpcError("BACKEND_UNAVAILABLE", "Install the optional Demucs backend")


def _discover_stems(root: Path, source_stem: str) -> dict[str, Path]:
    candidates: dict[str, Path] = {}
    for path in root.rglob("*.wav"):
        if source_stem in path.parts or path.parent.name == source_stem:
            candidates[path.stem.lower()] = path
    if not candidates:
        for path in root.rglob("*.wav"):
            candidates[path.stem.lower()] = path
    return candidates


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


def _fuse_stems(job: Job, model_outputs: list[dict[str, Path]], output: Path,
                requested_stems: list[str]) -> tuple[dict[str, str], list[str]]:
    import numpy as np
    common_stems = set(model_outputs[0])
    for model_output in model_outputs[1:]:
        common_stems.intersection_update(model_output)
    wanted = [stem.lower() for stem in requested_stems]
    selected = sorted(stem for stem in common_stems if stem in wanted) or sorted(common_stems)
    if not selected:
        raise RpcError("NO_STEMS_FOUND", "Demucs completed but no common WAV stems were found")
    outputs: dict[str, str] = {}
    for stem_index, stem in enumerate(selected):
        if job.cancel.is_set():
            raise RpcError("CANCELLED", "Operation cancelled")
        recordings: list[Any] = []
        sample_rate: int | None = None
        channels: int | None = None
        for model_output in model_outputs:
            audio, current_rate = _load_audio(model_output[stem])
            if sample_rate is None:
                sample_rate = current_rate
                channels = audio.shape[1]
            if current_rate != sample_rate or audio.shape[1] != channels:
                raise RpcError("INCOMPATIBLE_MODEL_OUTPUT", f"Model outputs disagree for stem {stem}")
            recordings.append(audio)
        length = min(recording.shape[0] for recording in recordings)
        reference = recordings[0][:length]
        aligned = [reference]
        for recording in recordings[1:]:
            candidate = recording[:length]
            if float(np.sum(reference * candidate)) < 0.0:
                candidate = -candidate
            aligned.append(candidate)
        fused = np.mean(np.stack(aligned, axis=0), axis=0, dtype=np.float64).astype(np.float32)
        destination = output / "ensemble" / stem / f"{stem}.wav"
        _write_audio(destination, fused, int(sample_rate))
        outputs[stem] = str(destination)
        job.update(progress=0.75 + 0.2 * ((stem_index + 1) / len(selected)), message=f"Fused {stem}")
    missing = [stem for stem in wanted if stem not in outputs]
    return outputs, missing


def separation_run(params: dict[str, Any]) -> dict[str, Any]:
    source = _require_file(params)
    output = Path(params.get("outputDir") or source.parent / f"{source.stem}-stems").resolve()
    plan = separation_plan(params)

    def operation(job: Job) -> dict[str, Any]:
        output.mkdir(parents=True, exist_ok=True)
        model_outputs: list[dict[str, Path]] = []
        for index, model in enumerate(plan["models"]):
            if job.cancel.is_set():
                raise RpcError("CANCELLED", "Operation cancelled")
            run_output = output / "model-runs" / f"{index + 1}-{model}"
            job.update(progress=0.05 + 0.55 * (index / len(plan["models"])), message=f"Running {model}")
            _run_process(job, _demucs_command(model, run_output, source))
            discovered = _discover_stems(run_output, source.stem)
            if not discovered:
                raise RpcError("NO_STEMS_FOUND", f"No WAV outputs found for Demucs model {model}")
            model_outputs.append(discovered)

        if plan["quality"] == "ensemble" and len(model_outputs) > 1:
            if not (_module_available("numpy") and _module_available("soundfile")):
                raise RpcError("BACKEND_UNAVAILABLE", "Ensemble fusion requires numpy and soundfile")
            stems, missing = _fuse_stems(job, model_outputs, output, plan["requestedStems"])
            return {"outputDir": str(output), "models": plan["models"], "stems": stems, "missingStems": missing, "plan": plan}

        discovered = model_outputs[0]
        requested = [stem.lower() for stem in plan["requestedStems"]]
        stems = {name: str(path) for name, path in discovered.items() if name in requested}
        if not stems:
            stems = {name: str(path) for name, path in discovered.items()}
        missing = [stem for stem in requested if stem not in stems]
        return {"outputDir": str(output), "models": plan["models"], "stems": stems, "missingStems": missing, "plan": plan}

    job = JOBS.submit("separation", operation)
    return {"jobId": job.id, "plan": plan}


def transcription_plan(params: dict[str, Any]) -> dict[str, Any]:
    return {
        "backend": str(params.get("backend", "basic-pitch")),
        "outputs": ["midi-with-pitch-bends", "note-events-csv", "model-output-npz"],
        "inputRecommendation": "Use one isolated instrument stem for best results",
    }


def transcription_run(params: dict[str, Any]) -> dict[str, Any]:
    source = _require_file(params)
    output = Path(params.get("outputDir") or source.parent / f"{source.stem}-transcription").resolve()
    plan = transcription_plan(params)

    def operation(job: Job) -> dict[str, Any]:
        if job.cancel.is_set():
            raise RpcError("CANCELLED", "Operation cancelled")
        output.mkdir(parents=True, exist_ok=True)
        executable = shutil.which("basic-pitch")
        if executable:
            _run_process(job, [executable, str(output), str(source), "--save-note-events", "--save-model-outputs"])
        elif _module_available("basic_pitch"):
            from basic_pitch import ICASSP_2022_MODEL_PATH
            from basic_pitch.inference import predict_and_save
            predict_and_save([str(source)], str(output), True, False, True, True, ICASSP_2022_MODEL_PATH)
        else:
            raise RpcError("BACKEND_UNAVAILABLE", "Install Basic Pitch")
        if job.cancel.is_set():
            raise RpcError("CANCELLED", "Operation cancelled")
        return {
            "outputDir": str(output),
            "midiFiles": [str(path) for path in output.rglob("*.mid")],
            "noteEventFiles": [str(path) for path in output.rglob("*.csv")],
            "modelOutputFiles": [str(path) for path in output.rglob("*.npz")],
            "plan": plan,
        }

    job = JOBS.submit("transcription", operation)
    return {"jobId": job.id, "plan": plan}


def restoration_run(params: dict[str, Any]) -> dict[str, Any]:
    source = _require_file(params)
    output = Path(params.get("output") or source.with_name(source.stem + "-restored.wav")).resolve()
    strength = max(0.0, min(_finite_float(params, "strength", 0.35), 1.0))

    def operation(job: Job) -> dict[str, Any]:
        import numpy as np
        if job.cancel.is_set(): raise RpcError("CANCELLED", "Operation cancelled")
        audio, sample_rate = _load_audio(source)
        job.update(progress=0.2, message="Removing DC offset")
        audio = audio - np.mean(audio, axis=0, keepdims=True)
        noise_floor = np.quantile(np.abs(audio), 0.15, axis=0, keepdims=True)
        threshold = noise_floor * (1.5 + 4.0 * strength)
        gain = np.clip((np.abs(audio) - threshold) / (threshold + 1e-7), 0.0, 1.0)
        audio = audio * ((1.0 - strength * 0.7) + gain * strength * 0.7)
        if job.cancel.is_set(): raise RpcError("CANCELLED", "Operation cancelled")
        job.update(progress=0.75, message="Applying peak protection")
        peak = float(np.max(np.abs(audio)))
        if peak > 1.0: audio /= peak
        _write_audio(output, audio, sample_rate)
        return {"output": str(output), "processors": ["dc-removal", "adaptive-gate", "peak-protection"]}

    return {"jobId": JOBS.submit("restoration", operation).id}


def mastering_run(params: dict[str, Any]) -> dict[str, Any]:
    source = _require_file(params)
    output = Path(params.get("output") or source.with_name(source.stem + "-mastered.wav")).resolve()
    target_peak_db = _finite_float(params, "targetPeakDb", -1.0)
    drive = max(0.0, min(_finite_float(params, "drive", 0.15), 1.0))

    def operation(job: Job) -> dict[str, Any]:
        import numpy as np
        if job.cancel.is_set(): raise RpcError("CANCELLED", "Operation cancelled")
        audio, sample_rate = _load_audio(source)
        job.update(progress=0.25, message="Applying soft saturation")
        audio = np.tanh(audio * (1.0 + drive * 3.0)) / math.tanh(1.0 + drive * 3.0)
        peak = float(np.max(np.abs(audio))) + 1e-12
        target = 10.0 ** (target_peak_db / 20.0)
        audio = audio * min(target / peak, 8.0)
        if job.cancel.is_set(): raise RpcError("CANCELLED", "Operation cancelled")
        job.update(progress=0.8, message="Writing mastered audio")
        _write_audio(output, audio, sample_rate)
        return {"output": str(output), "targetPeakDb": target_peak_db, "processors": ["soft-saturation", "peak-normalization"]}

    return {"jobId": JOBS.submit("mastering", operation).id}


def spectral_mask(params: dict[str, Any]) -> dict[str, Any]:
    source = _require_file(params)
    output = Path(params.get("output") or source.with_name(source.stem + "-spectral.wav")).resolve()
    start = max(0.0, _finite_float(params, "startSeconds", 0.0))
    end = _finite_float(params, "endSeconds", start + 1.0)
    low_hz = max(0.0, _finite_float(params, "lowHz", 0.0))
    high_hz = _finite_float(params, "highHz", 20000.0)
    gain_db = _finite_float(params, "gainDb", -12.0)
    if end <= start:
        raise RpcError("INVALID_ARGUMENT", "endSeconds must be greater than startSeconds")
    if high_hz <= low_hz:
        raise RpcError("INVALID_ARGUMENT", "highHz must be greater than lowHz")

    def operation(job: Job) -> dict[str, Any]:
        import numpy as np
        audio, sample_rate = _load_audio(source)
        n_fft, hop = 2048, 512
        nyquist = sample_rate / 2.0
        effective_high_hz = min(high_hz, nyquist)
        window = np.hanning(n_fft).astype(np.float32)
        padded = np.pad(audio, ((0, n_fft), (0, 0)))
        output_audio = np.zeros_like(padded)
        weight = np.zeros((padded.shape[0], 1), dtype=np.float32)
        offsets = range(0, padded.shape[0] - n_fft + 1, hop)
        total = max(1, len(offsets))
        bins = np.fft.rfftfreq(n_fft, 1.0 / sample_rate)
        selection = (bins >= low_hz) & (bins <= effective_high_hz)
        for index, offset in enumerate(offsets):
            if job.cancel.is_set(): raise RpcError("CANCELLED", "Operation cancelled")
            frame = padded[offset:offset+n_fft] * window[:, None]
            spectrum = np.fft.rfft(frame, axis=0)
            frame_time = offset / sample_rate
            if start <= frame_time <= end:
                spectrum[selection] *= 10.0 ** (gain_db / 20.0)
            reconstructed = np.fft.irfft(spectrum, n=n_fft, axis=0).real.astype(np.float32) * window[:, None]
            output_audio[offset:offset+n_fft] += reconstructed
            weight[offset:offset+n_fft] += window[:, None] ** 2
            if index % 32 == 0: job.update(progress=min(0.95, index / total), message="Rendering spectral mask")
        output_audio = output_audio[:audio.shape[0]] / np.maximum(weight[:audio.shape[0]], 1e-6)
        _write_audio(output, output_audio, sample_rate)
        return {"output": str(output), "mask": {"startSeconds": start, "endSeconds": end, "lowHz": low_hz, "highHz": effective_high_hz, "gainDb": gain_db}}

    return {"jobId": JOBS.submit("spectral-mask", operation).id}


def replacement_plan(params: dict[str, Any]) -> dict[str, Any]:
    instrument = str(params.get("instrument", "piano"))
    return {
        "instrument": instrument,
        "analysis": ["note-events", "timing", "velocity", "pitch-bend", "articulation"],
        "render": ["midi-or-plugin", "expression-transfer", "transient-alignment", "blend"],
        "requires": ["transcribed-note-events", "licensed-target-instrument-or-plugin"],
        "status": "plan-only",
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
    return execute_script(source, context, _finite_float(params, "timeoutSeconds", 2.0))


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
