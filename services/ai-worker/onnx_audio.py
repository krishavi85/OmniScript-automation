from __future__ import annotations

import json
from pathlib import Path
from typing import Any


def run_restoration_model(model_path: Path, manifest_path: Path,
                          source: Path, output: Path) -> dict[str, Any]:
    import numpy as np
    import onnxruntime as ort
    import soundfile as sf

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    task = str(manifest.get("task", ""))
    if task not in {"denoise", "dereverb", "declip"}:
        raise ValueError("manifest task must be denoise, dereverb, or declip")
    input_name = str(manifest.get("inputName", "input"))
    output_name = str(manifest.get("outputName", "output"))
    chunk_samples = int(manifest.get("chunkSamples", 262144))
    overlap_samples = int(manifest.get("overlapSamples", 8192))
    expected_rate = int(manifest.get("sampleRate", 0))
    layout = str(manifest.get("layout", "BCT"))
    if chunk_samples <= 0 or overlap_samples < 0 or overlap_samples * 2 >= chunk_samples:
        raise ValueError("invalid chunk and overlap settings")

    audio, sample_rate = sf.read(source, always_2d=True, dtype="float32")
    if expected_rate and sample_rate != expected_rate:
        raise ValueError(f"model expects {expected_rate} Hz, source is {sample_rate} Hz")
    providers = ["CPUExecutionProvider"]
    if "CUDAExecutionProvider" in ort.get_available_providers():
        providers.insert(0, "CUDAExecutionProvider")
    session = ort.InferenceSession(str(model_path), providers=providers)

    hop = chunk_samples - overlap_samples * 2
    padded = np.pad(audio, ((overlap_samples, chunk_samples), (0, 0)))
    result = np.zeros_like(padded)
    weight = np.zeros((padded.shape[0], 1), dtype=np.float32)
    fade = np.ones((chunk_samples, 1), dtype=np.float32)
    if overlap_samples:
        ramp = np.linspace(0.0, 1.0, overlap_samples, dtype=np.float32)[:, None]
        fade[:overlap_samples] = ramp
        fade[-overlap_samples:] = ramp[::-1]

    for offset in range(0, padded.shape[0] - chunk_samples + 1, hop):
        chunk = padded[offset:offset + chunk_samples]
        if layout == "BCT":
            model_input = np.transpose(chunk, (1, 0))[None, ...]
        elif layout == "BTC":
            model_input = chunk[None, ...]
        else:
            raise ValueError("unsupported model layout")
        prediction = session.run([output_name], {input_name: model_input})[0]
        if layout == "BCT":
            restored = np.transpose(prediction[0], (1, 0))
        else:
            restored = prediction[0]
        if restored.shape != chunk.shape:
            raise ValueError(f"model returned {restored.shape}, expected {chunk.shape}")
        result[offset:offset + chunk_samples] += restored * fade
        weight[offset:offset + chunk_samples] += fade

    result = result[overlap_samples:overlap_samples + audio.shape[0]]
    weight = weight[overlap_samples:overlap_samples + audio.shape[0]]
    result /= np.maximum(weight, 1.0e-6)
    output.parent.mkdir(parents=True, exist_ok=True)
    sf.write(output, result, sample_rate, subtype="FLOAT")
    return {
        "output": str(output),
        "task": task,
        "model": str(model_path),
        "provider": session.get_providers()[0],
        "sampleRate": int(sample_rate),
    }
