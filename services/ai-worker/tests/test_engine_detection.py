from __future__ import annotations

import sys
import unittest
from pathlib import Path
from unittest.mock import patch

WORKER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(WORKER_DIR))

import main as worker_entry
from engine_runtime import engine_status, installed_engine_ids


class EngineDetectionTests(unittest.TestCase):
    def test_executable_only_engine_is_not_detected_from_module(self) -> None:
        with (
            patch("engine_runtime.shutil.which", return_value=None),
            patch("engine_runtime.importlib.util.find_spec", return_value=object()),
        ):
            status = engine_status("audio-separator")
        self.assertFalse(status["installed"])
        self.assertIsNone(status["module"])

    def test_demucs_module_fallback_is_detected(self) -> None:
        with (
            patch("engine_runtime.shutil.which", return_value=None),
            patch("engine_runtime.importlib.util.find_spec", return_value=object()),
        ):
            status = engine_status("demucs")
        self.assertTrue(status["installed"])
        self.assertEqual(status["module"], "demucs")

    def test_installed_ids_use_runtime_status(self) -> None:
        def fake_status(engine_id: str) -> dict[str, object]:
            return {"id": engine_id, "installed": engine_id == "openunmix"}

        with patch("engine_runtime.engine_status", side_effect=fake_status):
            self.assertEqual(installed_engine_ids(), ["openunmix"])

    def test_auto_plan_fails_when_no_engine_is_installed(self) -> None:
        with patch.object(worker_entry, "installed_engine_ids", return_value=[]):
            with self.assertRaises(worker_entry.worker.RpcError) as context:
                worker_entry.intelligent_mode_plan({"mode": "auto", "stems": ["vocals", "instrumental"]})
        self.assertEqual(context.exception.code, "BACKEND_UNAVAILABLE")

    def test_engine_list_includes_runtime_status(self) -> None:
        with patch.object(worker_entry, "engine_status", return_value={
            "id": "demucs",
            "installed": True,
            "executable": "demucs",
            "module": None,
        }):
            result = worker_entry.engine_list({})
        self.assertTrue(result["engines"])
        self.assertTrue(all("installed" in row for row in result["engines"]))


if __name__ == "__main__":
    unittest.main()
