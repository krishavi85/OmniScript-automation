from __future__ import annotations

import json
import math
from pathlib import Path
from typing import Any


def _runtime() -> tuple[Any, Any]:
    try:
        import numpy as np
        import soundfile as sf
    except ImportError as exc:
        raise RuntimeError("spectral tools require numpy and soundfile") from exc
    return np, sf


def load_audio(path: Path) -> tuple[Any, int]:
    np, sf = _runtime()
    audio, sample_rate = sf.read(path, always_2d=True, dtype="float32")
    if audio.size == 0:
        raise ValueError("audio file contains no samples")
    return np.asarray(audio, dtype=np.float32), int(sample_rate)


def write_audio(path: Path, audio: Any, sample_rate: int) -> None:
    _, sf = _runtime()
    path.parent.mkdir(parents=True, exist_ok=True)
    sf.write(path, audio, sample_rate, subtype="FLOAT")


def _stft(audio: Any, n_fft: int, hop: int) -> tuple[Any, Any]:
    np, _ = _runtime()
    window = np.hanning(n_fft).astype(np.float32)
    mono = np.mean(audio, axis=1)
    padded = np.pad(mono, (n_fft // 2, n_fft // 2))
    frame_count = 1 + max(0, (len(padded) - n_fft) // hop)
    frames = np.empty((n_fft // 2 + 1, frame_count), dtype=np.complex64)
    for index in range(frame_count):
        offset = index * hop
        frames[:, index] = np.fft.rfft(padded[offset:offset + n_fft] * window)
    return frames, window


def generate_tiles(source: Path, output_dir: Path, *, n_fft: int = 2048,
                   hop: int = 512, tile_columns: int = 256,
                   min_db: float = -100.0, max_db: float = 0.0) -> dict[str, Any]:
    np, _ = _runtime()
    if n_fft < 256 or n_fft > 16384 or n_fft & (n_fft - 1):
        raise ValueError("n_fft must be a power of two between 256 and 16384")
    if hop <= 0 or hop > n_fft or tile_columns <= 0:
        raise ValueError("invalid hop or tile size")
    audio, sample_rate = load_audio(source)
    spectrum, _ = _stft(audio, n_fft, hop)
    magnitude = np.abs(spectrum)
    reference = max(float(np.max(magnitude)), 1.0e-12)
    decibels = 20.0 * np.log10(np.maximum(magnitude, 1.0e-12) / reference)
    normalized = np.clip((decibels - min_db) / (max_db - min_db), 0.0, 1.0)
    quantized = np.round(normalized * 65535.0).astype(np.uint16)

    output_dir.mkdir(parents=True, exist_ok=True)
    tiles: list[dict[str, Any]] = []
    for tile_index, start in enumerate(range(0, quantized.shape[1], tile_columns)):
        tile = quantized[:, start:start + tile_columns]
        tile_path = output_dir / f"tile-{tile_index:06d}.npy"
        np.save(tile_path, tile, allow_pickle=False)
        tiles.append({
            "index": tile_index,
            "path": str(tile_path),
            "startColumn": start,
            "columnCount": int(tile.shape[1]),
            "startSeconds": start * hop / sample_rate,
            "endSeconds": (start + tile.shape[1]) * hop / sample_rate,
        })

    manifest = {
        "version": 1,
        "source": str(source),
        "sampleRate": sample_rate,
        "channels": int(audio.shape[1]),
        "durationSeconds": audio.shape[0] / sample_rate,
        "nFft": n_fft,
        "hop": hop,
        "frequencyBins": int(quantized.shape[0]),
        "timeColumns": int(quantized.shape[1]),
        "storage": "uint16-npy",
        "minDb": min_db,
        "maxDb": max_db,
        "tiles": tiles,
    }
    manifest_path = output_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    manifest["manifest"] = str(manifest_path)
    return manifest


def save_brush_operations(output_path: Path, operations: list[dict[str, Any]]) -> dict[str, Any]:
    validated: list[dict[str, Any]] = []
    for index, operation in enumerate(operations):
        required = {"startSeconds", "endSeconds", "lowHz", "highHz", "gainDb"}
        if not required.issubset(operation):
            raise ValueError(f"brush operation {index} is missing required values")
        start = float(operation["startSeconds"])
        end = float(operation["endSeconds"])
        low = float(operation["lowHz"])
        high = float(operation["highHz"])
        gain = float(operation["gainDb"])
        feather = float(operation.get("feather", 0.15))
        if not all(math.isfinite(value) for value in (start, end, low, high, gain, feather)):
            raise ValueError(f"brush operation {index} contains non-finite values")
        if start < 0 or end <= start or low < 0 or high <= low or not 0 <= feather <= 1:
            raise ValueError(f"brush operation {index} has an invalid range")
        validated.append({
            "startSeconds": start,
            "endSeconds": end,
            "lowHz": low,
            "highHz": high,
            "gainDb": gain,
            "feather": feather,
            "sourceStemId": str(operation.get("sourceStemId", "")),
            "destinationStemId": str(operation.get("destinationStemId", "")),
        })
    output_path.parent.mkdir(parents=True, exist_ok=True)
    payload = {"version": 1, "operations": validated}
    output_path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    return {"path": str(output_path), "operationCount": len(validated)}


def harmonic_note_resynthesis(source: Path, output: Path, *, start_seconds: float,
                              end_seconds: float, fundamental_hz: float,
                              gain_db: float = -120.0, pitch_shift_semitones: float = 0.0,
                              harmonics: int = 24, bandwidth_cents: float = 45.0,
                              n_fft: int = 4096, hop: int = 512) -> dict[str, Any]:
    np, _ = _runtime()
    audio, sample_rate = load_audio(source)
    if start_seconds < 0 or end_seconds <= start_seconds:
        raise ValueError("invalid note time range")
    if fundamental_hz <= 0 or fundamental_hz >= sample_rate / 2:
        raise ValueError("fundamental frequency is outside the audio range")
    if harmonics <= 0 or harmonics > 128:
        raise ValueError("harmonics must be between 1 and 128")

    window = np.hanning(n_fft).astype(np.float32)
    padded = np.pad(audio, ((n_fft // 2, n_fft // 2), (0, 0)))
    output_audio = np.zeros_like(padded)
    weight = np.zeros((padded.shape[0], 1), dtype=np.float32)
    frequencies = np.fft.rfftfreq(n_fft, 1.0 / sample_rate)
    gain = 10.0 ** (gain_db / 20.0)
    shift_ratio = 2.0 ** (pitch_shift_semitones / 12.0)
    half_width_ratio = 2.0 ** (bandwidth_cents / 1200.0) - 1.0

    for offset in range(0, padded.shape[0] - n_fft + 1, hop):
        frame_time = max(0.0, (offset - n_fft // 2) / sample_rate)
        frame = padded[offset:offset + n_fft] * window[:, None]
        spectrum = np.fft.rfft(frame, axis=0)
        if start_seconds <= frame_time <= end_seconds:
            original = spectrum.copy()
            note_mask = np.zeros(len(frequencies), dtype=np.float32)
            for harmonic in range(1, harmonics + 1):
                center = fundamental_hz * harmonic
                if center >= sample_rate / 2:
                    break
                width = max(center * half_width_ratio, sample_rate / n_fft)
                distance = np.abs(frequencies - center) / width
                note_mask = np.maximum(note_mask, np.clip(1.0 - distance, 0.0, 1.0))
            spectrum *= (1.0 - note_mask[:, None])
            selected = original * note_mask[:, None]
            if abs(pitch_shift_semitones) < 1.0e-9:
                spectrum += selected * gain
            else:
                shifted = np.zeros_like(selected)
                source_bins = np.nonzero(note_mask > 0.0)[0]
                target_bins = np.round(source_bins * shift_ratio).astype(int)
                valid = target_bins < shifted.shape[0]
                for source_bin, target_bin in zip(source_bins[valid], target_bins[valid]):
                    shifted[target_bin] += selected[source_bin] * gain
                spectrum += shifted
        reconstructed = np.fft.irfft(spectrum, n=n_fft, axis=0).real.astype(np.float32)
        reconstructed *= window[:, None]
        output_audio[offset:offset + n_fft] += reconstructed
        weight[offset:offset + n_fft] += window[:, None] ** 2

    result = output_audio[n_fft // 2:n_fft // 2 + audio.shape[0]]
    result /= np.maximum(weight[n_fft // 2:n_fft // 2 + audio.shape[0]], 1.0e-7)
    peak = float(np.max(np.abs(result)))
    if peak > 1.0:
        result /= peak
    write_audio(output, result, sample_rate)
    return {
        "output": str(output),
        "method": "soft-harmonic-mask-resynthesis",
        "startSeconds": start_seconds,
        "endSeconds": end_seconds,
        "fundamentalHz": fundamental_hz,
        "gainDb": gain_db,
        "pitchShiftSemitones": pitch_shift_semitones,
        "limitations": "Overlapping sources sharing the same harmonics may still be affected.",
    }
