from __future__ import annotations

import math
import struct
import wave
from pathlib import Path
from typing import Any


class BenchmarkError(ValueError):
    pass


def _read_wav(path: Path) -> tuple[list[float], int, int]:
    if not path.is_file():
        raise BenchmarkError(f"audio file does not exist: {path}")
    if path.suffix.lower() != ".wav":
        raise BenchmarkError("built-in benchmark reader currently requires WAV input")

    with wave.open(str(path), "rb") as handle:
        channels = handle.getnchannels()
        sample_rate = handle.getframerate()
        sample_width = handle.getsampwidth()
        frames = handle.getnframes()
        raw = handle.readframes(frames)

    if sample_width not in {1, 2, 3, 4}:
        raise BenchmarkError(f"unsupported WAV sample width: {sample_width}")
    if channels < 1 or sample_rate < 1:
        raise BenchmarkError("invalid WAV metadata")

    values: list[float] = []
    if sample_width == 1:
        values = [(byte - 128) / 128.0 for byte in raw]
    elif sample_width == 2:
        count = len(raw) // 2
        values = [sample / 32768.0 for sample in struct.unpack(f"<{count}h", raw)]
    elif sample_width == 3:
        for index in range(0, len(raw), 3):
            chunk = raw[index:index + 3]
            integer = int.from_bytes(chunk, "little", signed=False)
            if integer & 0x800000:
                integer -= 1 << 24
            values.append(integer / 8388608.0)
    else:
        count = len(raw) // 4
        values = [sample / 2147483648.0 for sample in struct.unpack(f"<{count}i", raw)]
    return values, sample_rate, channels


def analyze_file(path: Path) -> dict[str, Any]:
    samples, sample_rate, channels = _read_wav(path)
    if not samples:
        raise BenchmarkError("audio file contains no samples")
    peak = max(abs(value) for value in samples)
    mean_square = sum(value * value for value in samples) / len(samples)
    rms = math.sqrt(mean_square)
    frames = len(samples) // channels
    return {
        "path": str(path.resolve()),
        "sampleRate": sample_rate,
        "channels": channels,
        "frames": frames,
        "durationSeconds": frames / sample_rate,
        "samplePeak": peak,
        "samplePeakDbfs": 20.0 * math.log10(max(peak, 1e-12)),
        "rms": rms,
        "rmsDbfs": 20.0 * math.log10(max(rms, 1e-12)),
        "clippedSamples": sum(1 for value in samples if abs(value) >= 0.999969),
    }


def compare_files(candidate: Path, reference: Path) -> dict[str, Any]:
    candidate_samples, candidate_rate, candidate_channels = _read_wav(candidate)
    reference_samples, reference_rate, reference_channels = _read_wav(reference)
    if candidate_rate != reference_rate:
        raise BenchmarkError("candidate and reference sample rates differ")
    if candidate_channels != reference_channels:
        raise BenchmarkError("candidate and reference channel counts differ")

    length = min(len(candidate_samples), len(reference_samples))
    if length == 0:
        raise BenchmarkError("candidate or reference contains no samples")
    candidate_values = candidate_samples[:length]
    reference_values = reference_samples[:length]

    errors = [candidate_value - reference_value for candidate_value, reference_value in zip(candidate_values, reference_values, strict=True)]
    signal_power = sum(value * value for value in reference_values) / length
    error_power = sum(value * value for value in errors) / length
    rmse = math.sqrt(error_power)
    mae = sum(abs(value) for value in errors) / length
    snr = 10.0 * math.log10(max(signal_power, 1e-24) / max(error_power, 1e-24))

    candidate_mean = sum(candidate_values) / length
    reference_mean = sum(reference_values) / length
    covariance = sum(
        (candidate_value - candidate_mean) * (reference_value - reference_mean)
        for candidate_value, reference_value in zip(candidate_values, reference_values, strict=True)
    )
    candidate_energy = sum((value - candidate_mean) ** 2 for value in candidate_values)
    reference_energy = sum((value - reference_mean) ** 2 for value in reference_values)
    denominator = math.sqrt(candidate_energy * reference_energy)
    correlation = covariance / denominator if denominator > 0.0 else 0.0

    return {
        "candidate": analyze_file(candidate),
        "reference": analyze_file(reference),
        "alignedSamples": length,
        "rmse": rmse,
        "mae": mae,
        "snrDb": snr,
        "correlation": max(-1.0, min(1.0, correlation)),
    }


def benchmark_set(candidates: list[Path], reference: Path | None = None) -> dict[str, Any]:
    if not candidates:
        raise BenchmarkError("at least one candidate is required")
    results: list[dict[str, Any]] = []
    for candidate in candidates:
        metrics = compare_files(candidate, reference) if reference else {"candidate": analyze_file(candidate)}
        results.append(metrics)

    if reference:
        ranked = sorted(
            results,
            key=lambda item: (-float(item["snrDb"]), float(item["rmse"])),
        )
    else:
        ranked = sorted(
            results,
            key=lambda item: (-float(item["candidate"]["rmsDbfs"]), item["candidate"]["path"]),
        )

    return {
        "reference": str(reference.resolve()) if reference else None,
        "results": results,
        "ranking": [item["candidate"]["path"] for item in ranked],
    }
