from __future__ import annotations

import math
from pathlib import Path
from typing import Any


def analyze_loudness(source: Path) -> dict[str, Any]:
    import numpy as np
    import pyloudnorm as pyln
    import soundfile as sf
    from scipy.signal import resample_poly

    audio, sample_rate = sf.read(source, always_2d=True, dtype="float64")
    if audio.size == 0:
        raise ValueError("audio file contains no samples")
    meter = pyln.Meter(sample_rate, block_size=0.400)
    integrated_lufs = float(meter.integrated_loudness(audio))
    sample_peak = float(np.max(np.abs(audio)))
    true_peak = float(np.max(np.abs(resample_poly(audio, 4, 1, axis=0))))
    rms = float(np.sqrt(np.mean(np.square(audio))))
    return {
        "sampleRate": int(sample_rate),
        "channels": int(audio.shape[1]),
        "durationSeconds": float(audio.shape[0] / sample_rate),
        "integratedLufs": integrated_lufs,
        "samplePeakDbfs": 20.0 * math.log10(max(sample_peak, 1.0e-12)),
        "truePeakDbtp": 20.0 * math.log10(max(true_peak, 1.0e-12)),
        "rmsDbfs": 20.0 * math.log10(max(rms, 1.0e-12)),
        "oversampling": 4,
    }
