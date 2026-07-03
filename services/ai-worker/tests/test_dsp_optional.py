from __future__ import annotations

import importlib.util
import sys
import tempfile
import time
import unittest
from pathlib import Path

WORKER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(WORKER_DIR))

import worker

DSP_AVAILABLE = importlib.util.find_spec("numpy") is not None and importlib.util.find_spec("soundfile") is not None


@unittest.skipUnless(DSP_AVAILABLE, "numpy and soundfile are optional dependencies")
class DspTests(unittest.TestCase):
    def setUp(self) -> None:
        import numpy as np
        import soundfile as sf

        self.root = Path(tempfile.mkdtemp(prefix="omnistem-dsp-test-"))
        self.sample_rate = 16000
        time_axis = np.arange(self.sample_rate, dtype=np.float32) / self.sample_rate
        audio = (0.2 * np.sin(2 * np.pi * 440 * time_axis) + 0.01).astype(np.float32)
        self.source = self.root / "tone.wav"
        sf.write(self.source, audio, self.sample_rate)

    @staticmethod
    def wait_for_job(job_id: str) -> dict[str, object]:
        job = worker.JOBS.get(job_id)
        deadline = time.time() + 10
        while job.snapshot()["state"] in {"queued", "running"} and time.time() < deadline:
            time.sleep(0.02)
        return job.snapshot()

    def test_restoration_mastering_and_spectral_render(self) -> None:
        operations = [
            (worker.restoration_run, {"source": str(self.source), "output": str(self.root / "restored.wav")}),
            (worker.mastering_run, {"source": str(self.source), "output": str(self.root / "mastered.wav")}),
            (worker.spectral_mask, {
                "source": str(self.source), "output": str(self.root / "spectral.wav"),
                "startSeconds": 0.1, "endSeconds": 0.8,
                "lowHz": 400, "highHz": 500, "gainDb": -18,
            }),
        ]
        for operation, params in operations:
            snapshot = self.wait_for_job(operation(params)["jobId"])
            self.assertEqual(snapshot["state"], "completed", snapshot)
            self.assertTrue(Path(snapshot["result"]["output"]).is_file())

    def test_ensemble_fusion_corrects_inverted_polarity(self) -> None:
        import numpy as np
        import soundfile as sf

        time_axis = np.arange(self.sample_rate, dtype=np.float32) / self.sample_rate
        reference = np.sin(2 * np.pi * 220 * time_axis).astype(np.float32)[:, None]
        first = self.root / "model-a" / "vocals.wav"
        second = self.root / "model-b" / "vocals.wav"
        first.parent.mkdir()
        second.parent.mkdir()
        sf.write(first, reference, self.sample_rate)
        sf.write(second, -reference, self.sample_rate)

        outputs, missing = worker._fuse_stems(
            worker.Job("ensemble-test", "ensemble"),
            [{"vocals": first}, {"vocals": second}],
            self.root / "fused",
            ["vocals"],
        )
        fused, _ = sf.read(outputs["vocals"], always_2d=True, dtype="float32")
        correlation = float(np.corrcoef(reference[:, 0], fused[:, 0])[0, 1])
        self.assertEqual(missing, [])
        self.assertGreater(correlation, 0.99)


if __name__ == "__main__":
    unittest.main()
