from __future__ import annotations

import json
import math
from pathlib import Path
from typing import Any


def load_config(path: Path) -> dict[str, Any]:
    config = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(config, dict):
        raise ValueError("configuration must be an object")
    chunk = int(config.get("chunkSamples", 0))
    overlap = int(config.get("overlapSamples", -1))
    if chunk <= 0 or overlap < 0 or overlap * 2 >= chunk:
        raise ValueError("invalid chunk settings")
    required = {"sampleRate", "audioInput", "conditionInput", "audioOutput"}
    if not required.issubset(config):
        raise ValueError("configuration is missing model input or output names")
    return config


def _condition(np: Any, note: dict[str, Any], dimension: int) -> Any:
    pitch = float(note.get("midiPitch", -1))
    start = float(note.get("startSeconds", -1))
    end = float(note.get("endSeconds", -1))
    gain = float(note.get("gainDb", 0))
    shift = float(note.get("pitchShiftSemitones", 0))
    values = (pitch, start, end, gain, shift)
    if not all(math.isfinite(value) for value in values):
        raise ValueError("note parameters must be finite")
    if pitch < 0 or pitch > 127 or start < 0 or end <= start:
        raise ValueError("note parameters are outside the supported range")
    vector = [pitch / 127.0, start, end, gain / 60.0, shift / 24.0]
    if dimension < len(vector):
        raise ValueError("condition dimension is too small")
    vector.extend([0.0] * (dimension - len(vector)))
    return np.asarray(vector, dtype=np.float32)[None, :]


def render(source: Path, output: Path, model: Path, config_path: Path,
           note: dict[str, Any]) -> dict[str, Any]:
    import numpy as np
    import onnxruntime as ort
    import soundfile as sf

    config = load_config(config_path)
    audio, sample_rate = sf.read(source, always_2d=True, dtype="float32")
    if audio.size == 0:
        raise ValueError("source contains no samples")
    if sample_rate != int(config["sampleRate"]):
        raise ValueError("source sample rate does not match the model")

    available = ort.get_available_providers()
    providers = [name for name in ("CUDAExecutionProvider", "DmlExecutionProvider", "CPUExecutionProvider")
                 if name in available]
    session = ort.InferenceSession(str(model), providers=providers or ["CPUExecutionProvider"])
    condition = _condition(np, note, int(config.get("conditionDimension", 5)))
    chunk = int(config["chunkSamples"])
    overlap = int(config["overlapSamples"])
    hop = chunk - 2 * overlap

    padded = np.pad(audio, ((overlap, chunk), (0, 0)))
    rendered = np.zeros_like(padded)
    weights = np.zeros((padded.shape[0], 1), dtype=np.float32)
    window = np.ones((chunk, 1), dtype=np.float32)
    if overlap:
        ramp = np.linspace(0.0, 1.0, overlap, dtype=np.float32)[:, None]
        window[:overlap] = ramp
        window[-overlap:] = ramp[::-1]

    for offset in range(0, padded.shape[0] - chunk + 1, hop):
        block = padded[offset:offset + chunk]
        model_audio = np.transpose(block, (1, 0))[None, ...]
        prediction = session.run(
            [str(config["audioOutput"])],
            {
                str(config["audioInput"]): model_audio,
                str(config["conditionInput"]): condition,
            },
        )[0]
        if prediction.ndim != 3:
            raise ValueError("model output must be [batch, channels, samples]")
        block_out = np.transpose(prediction[0], (1, 0)).astype(np.float32)
        if block_out.shape != block.shape:
            raise ValueError("model output shape does not match the input block")
        rendered[offset:offset + chunk] += block_out * window
        weights[offset:offset + chunk] += window

    result = rendered[overlap:overlap + audio.shape[0]]
    result /= np.maximum(weights[overlap:overlap + audio.shape[0]], 1.0e-6)
    consistency = min(max(float(config.get("mixtureConsistency", 0.0)), 0.0), 1.0)
    result += (audio - result) * consistency
    peak = float(np.max(np.abs(result)))
    if peak > 1.0:
        result /= peak
    output.parent.mkdir(parents=True, exist_ok=True)
    sf.write(output, result, sample_rate, subtype="FLOAT")
    return {
        "output": str(output),
        "model": str(model),
        "provider": session.get_providers()[0],
        "method": "note-conditioned-neural-resynthesis",
        "note": note,
        "mixtureConsistency": consistency,
    }
