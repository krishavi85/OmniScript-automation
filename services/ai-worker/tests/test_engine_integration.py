from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

WORKER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(WORKER_DIR))

from engine_commands import EngineCommandError, build_command
from engine_registry import public_registry
from request_once import execute_request


class EngineIntegrationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.root = Path(tempfile.mkdtemp(prefix="omnistem-engine-test-"))
        self.source = self.root / "song.wav"
        self.source.write_bytes(b"RIFF-test-placeholder")

    def test_registry_contains_migrated_engines(self) -> None:
        identifiers = {row["id"] for row in public_registry()}
        self.assertEqual(identifiers, {"audio-separator", "demucs", "spleeter", "openunmix"})

    def test_demucs_two_stem_command(self) -> None:
        result = build_command({
            "engine": "demucs",
            "source": str(self.source),
            "outputDir": str(self.root / "demucs-out"),
            "stems": ["vocals", "instrumental"],
            "model": "htdemucs_ft",
            "device": "cpu",
            "outputFormat": "wav",
        })
        self.assertEqual(result["engine"], "demucs")
        self.assertIn("--two-stems", result["command"])
        self.assertIn("vocals", result["command"])
        self.assertIn("--device", result["command"])

    def test_spleeter_rejects_invalid_stem_count(self) -> None:
        with self.assertRaises(EngineCommandError):
            build_command({
                "engine": "spleeter",
                "source": str(self.source),
                "outputDir": str(self.root / "spleeter-out"),
                "stems": ["vocals", "drums", "other"],
            })

    def test_openunmix_rejects_unsupported_input(self) -> None:
        source = self.root / "song.mp3"
        source.write_bytes(b"test")
        with self.assertRaises(EngineCommandError):
            build_command({"engine": "openunmix", "source": str(source)})

    def test_one_request_runner_executes_health_check(self) -> None:
        response = execute_request({"id": "test", "method": "health.check", "params": {}})
        self.assertEqual(response["id"], "test")
        self.assertEqual(response["result"]["status"], "ok")
        json.dumps(response)


if __name__ == "__main__":
    unittest.main()
