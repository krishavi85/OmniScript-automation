from __future__ import annotations

import sys
import unittest
from pathlib import Path

WORKER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(WORKER_DIR))

from engine_runtime import canonical_output_stem


class EngineOutputNameTests(unittest.TestCase):
    def test_instrumental_aliases(self) -> None:
        self.assertEqual(canonical_output_stem(Path("no_vocals.wav")), "instrumental")
        self.assertEqual(canonical_output_stem(Path("accompaniment.wav")), "instrumental")
        self.assertEqual(canonical_output_stem(Path("song_(Instrumental)_model.wav")), "instrumental")

    def test_vocal_and_instrument_names(self) -> None:
        self.assertEqual(canonical_output_stem(Path("song_(Vocals)_model.wav")), "vocals")
        self.assertEqual(canonical_output_stem(Path("drums.wav")), "drums")
        self.assertEqual(canonical_output_stem(Path("bass.wav")), "bass")
        self.assertEqual(canonical_output_stem(Path("guitars.wav")), "guitar")
        self.assertEqual(canonical_output_stem(Path("keys.wav")), "piano")


if __name__ == "__main__":
    unittest.main()
