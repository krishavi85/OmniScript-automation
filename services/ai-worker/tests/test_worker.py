from __future__ import annotations

import sys
import time
import unittest
from pathlib import Path

WORKER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(WORKER_DIR))

import worker
from omnscript_runtime import execute_script, validate_script


class WorkerTests(unittest.TestCase):
    def test_health_lists_real_protocol(self) -> None:
        result = worker.health_check({})
        self.assertEqual(result["protocolVersion"], 3)
        self.assertIn("separation.run", result["capabilities"])

    def test_ensemble_plan_matches_implementation(self) -> None:
        plan = worker.separation_plan({"quality": "ensemble"})
        self.assertEqual(plan["models"], ["htdemucs", "htdemucs_ft"])
        self.assertIn("polarity-alignment", plan["stages"])
        self.assertNotIn("artifact-repair", plan["stages"])

    def test_transcription_plan_is_truthful(self) -> None:
        outputs = worker.transcription_plan({})["outputs"]
        self.assertIn("note-events-csv", outputs)
        self.assertIn("model-output-npz", outputs)

    def test_invalid_spectral_region_is_rejected_before_job(self) -> None:
        with self.assertRaises(worker.RpcError) as context:
            worker.spectral_mask({"source": __file__, "startSeconds": 2, "endSeconds": 1})
        self.assertEqual(context.exception.code, "INVALID_ARGUMENT")

    def test_omniscript_blocks_imports(self) -> None:
        validation = validate_script("import os")
        self.assertFalse(validation.valid)

    def test_omniscript_emits_transaction(self) -> None:
        result = execute_script(
            'for note in notes():\n    if note["confidence"] < 0.5:\n        mute_note(note["id"])',
            {"notes": [{"id": "n1", "confidence": 0.2}], "stems": []},
        )
        self.assertTrue(result["ok"])
        self.assertEqual(result["transaction"][0]["command"], "mute_note")

    def test_cancelled_job_is_not_failed(self) -> None:
        manager = worker.JobManager(1)
        try:
            job = manager.submit("test", lambda current: self._wait_for_cancel(current))
            manager.cancel(job.id)
            deadline = time.time() + 2
            while job.snapshot()["state"] in {"queued", "running"} and time.time() < deadline:
                time.sleep(0.01)
            self.assertEqual(job.snapshot()["state"], "cancelled")
        finally:
            manager.shutdown()

    @staticmethod
    def _wait_for_cancel(job: worker.Job) -> dict[str, object]:
        deadline = time.time() + 1
        while not job.cancel.is_set() and time.time() < deadline:
            time.sleep(0.01)
        if job.cancel.is_set():
            raise worker.RpcError("CANCELLED", "cancelled")
        return {}


if __name__ == "__main__":
    unittest.main()
