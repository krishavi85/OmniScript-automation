from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path

WORKER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(WORKER_DIR))

from mastering_pro import analyze_loudness
from spectral_tools import generate_tiles, harmonic_note_resynthesis, save_brush_operations

BASIC_DSP = importlib.util.find_spec("numpy") is not None and importlib.util.find_spec("soundfile") is not None
LOUDNESS = BASIC_DSP and importlib.util.find_spec("pyloudnorm") is not None and importlib.util.find_spec("scipy") is not None


@unittest.skipUnless(BASIC_DSP, "production audio tests require numpy and soundfile")
class ProductionAudioTests(unittest.TestCase):
    def setUp(self) -> None:
        import numpy as np
        import soundfile as sf

        self.root = Path(tempfile.mkdtemp(prefix="omnistem-production-test-"))
        self.rate = 16000
        timeline = np.arange(self.rate, dtype=np.float32) / self.rate
        self.audio = (0.2 * np.sin(2 * np.pi * 440 * timeline) +
                      0.1 * np.sin(2 * np.pi * 880 * timeline)).astype(np.float32)
        self.source = self.root / "polyphonic.wav"
        sf.write(self.source, self.audio, self.rate)

    def test_spectrogram_tiles_and_manifest(self) -> None:
        result = generate_tiles(self.source, self.root / "tiles", n_fft=1024, hop=256, tile_columns=8)
        self.assertGreater(len(result["tiles"]), 1)
        manifest = json.loads(Path(result["manifest"]).read_text(encoding="utf-8"))
        self.assertEqual(manifest["storage"], "uint16-npy")
        self.assertTrue(all(Path(tile["path"]).is_file() for tile in result["tiles"]))

    def test_brush_operations_are_validated_and_saved(self) -> None:
        result = save_brush_operations(self.root / "brush.json", [{
            "startSeconds": 0.1, "endSeconds": 0.4,
            "lowHz": 400, "highHz": 500, "gainDb": -12,
            "feather": 0.2,
        }])
        self.assertEqual(result["operationCount"], 1)
        self.assertTrue(Path(result["path"]).is_file())

    def test_harmonic_note_resynthesis_writes_audio(self) -> None:
        output = self.root / "edited.wav"
        result = harmonic_note_resynthesis(
            self.source, output, start_seconds=0.1, end_seconds=0.8,
            fundamental_hz=440.0, gain_db=-30.0, pitch_shift_semitones=0.0,
            n_fft=1024, hop=256,
        )
        self.assertEqual(result["method"], "soft-harmonic-mask-resynthesis")
        self.assertTrue(output.is_file())

    @unittest.skipUnless(LOUDNESS, "loudness analysis requires scipy and pyloudnorm")
    def test_lufs_and_true_peak_analysis(self) -> None:
        analysis = analyze_loudness(self.source)
        self.assertIn("integratedLufs", analysis)
        self.assertIn("truePeakDbtp", analysis)
        self.assertEqual(analysis["oversampling"], 4)


if __name__ == "__main__":
    unittest.main()
