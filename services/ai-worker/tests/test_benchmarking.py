from __future__ import annotations

import math
import struct
import sys
import tempfile
import unittest
import wave
from pathlib import Path

WORKER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(WORKER_DIR))

from benchmarking import BenchmarkError, analyze_file, benchmark_set, compare_files


def write_tone(path: Path, amplitude: float, frequency: float = 440.0) -> None:
    sample_rate = 8000
    samples = []
    for index in range(sample_rate // 4):
        value = amplitude * math.sin(2.0 * math.pi * frequency * index / sample_rate)
        samples.append(max(-32768, min(32767, round(value * 32767.0))))
    with wave.open(str(path), "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(sample_rate)
        handle.writeframes(struct.pack(f"<{len(samples)}h", *samples))


class BenchmarkingTests(unittest.TestCase):
    def setUp(self) -> None:
        self.root = Path(tempfile.mkdtemp(prefix="omnistem-benchmark-"))
        self.reference = self.root / "reference.wav"
        self.close = self.root / "close.wav"
        self.far = self.root / "far.wav"
        write_tone(self.reference, 0.5)
        write_tone(self.close, 0.49)
        write_tone(self.far, 0.2, 880.0)

    def test_analyze_file(self) -> None:
        metrics = analyze_file(self.reference)
        self.assertEqual(metrics["sampleRate"], 8000)
        self.assertEqual(metrics["channels"], 1)
        self.assertGreater(metrics["durationSeconds"], 0.2)
        self.assertGreater(metrics["rms"], 0.0)

    def test_compare_identical_has_high_snr(self) -> None:
        metrics = compare_files(self.reference, self.reference)
        self.assertGreater(metrics["snrDb"], 100.0)
        self.assertAlmostEqual(metrics["correlation"], 1.0, places=6)

    def test_benchmark_ranks_closer_candidate_first(self) -> None:
        result = benchmark_set([self.far, self.close], self.reference)
        self.assertEqual(result["ranking"][0], str(self.close.resolve()))

    def test_missing_candidate_is_rejected(self) -> None:
        with self.assertRaises(BenchmarkError):
            analyze_file(self.root / "missing.wav")


if __name__ == "__main__":
    unittest.main()
