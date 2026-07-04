from __future__ import annotations

import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

WORKER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(WORKER_DIR))

import advanced_methods
import worker


def wait_for_job(job_id: str) -> dict[str, object]:
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        snapshot = worker.JOBS.get(job_id).snapshot()
        if snapshot["state"] in {"completed", "failed", "cancelled"}:
            return snapshot
        time.sleep(0.01)
    raise AssertionError("mode job did not finish")


class ModeExecutionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.root = Path(tempfile.mkdtemp(prefix="omnistem-mode-run-"))
        self.source = self.root / "song.wav"
        self.source.write_bytes(b"placeholder")

    @staticmethod
    def fake_execute(params: dict[str, object], _runner: object) -> dict[str, object]:
        output = Path(str(params["outputDir"]))
        output.mkdir(parents=True, exist_ok=True)
        vocals = output / "vocals.wav"
        instrumental = output / "instrumental.wav"
        vocals.write_bytes(b"v")
        instrumental.write_bytes(b"i")
        return {
            "stems": {"vocals": str(vocals), "instrumental": str(instrumental)},
            "files": [str(vocals), str(instrumental)],
        }

    def test_standard_mode_executes_one_engine(self) -> None:
        with patch.object(advanced_methods, "execute_engine", side_effect=self.fake_execute):
            response = advanced_methods.mode_run({
                "source": str(self.source),
                "mode": "standard",
                "engine": "demucs",
                "outputDir": str(self.root / "out"),
            })
            snapshot = wait_for_job(response["jobId"])
        self.assertEqual(snapshot["state"], "completed")
        result = snapshot["result"]
        self.assertEqual(result["mode"], "standard")
        self.assertIn("standard-1", result["runs"])

    def test_cascade_uses_previous_vocal_stem(self) -> None:
        seen_sources: list[str] = []

        def execute(params: dict[str, object], runner: object) -> dict[str, object]:
            seen_sources.append(str(params["source"]))
            return self.fake_execute(params, runner)

        with patch.object(advanced_methods, "execute_engine", side_effect=execute):
            response = advanced_methods.mode_run({
                "source": str(self.source),
                "mode": "cascade",
                "engines": ["demucs", "audio-separator"],
                "cascadeStem": "vocals",
                "outputDir": str(self.root / "cascade"),
            })
            snapshot = wait_for_job(response["jobId"])
        self.assertEqual(snapshot["state"], "completed")
        self.assertEqual(seen_sources[0], str(self.source.resolve()))
        self.assertTrue(seen_sources[1].endswith("vocals.wav"))


if __name__ == "__main__":
    unittest.main()
