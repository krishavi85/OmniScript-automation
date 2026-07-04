from __future__ import annotations

import sys
import unittest
from pathlib import Path

WORKER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(WORKER_DIR))

from mode_planner import plan_mode
from mode_types import ModeError


class ModePlannerTests(unittest.TestCase):
    def test_standard_mode(self) -> None:
        plan = plan_mode({"mode": "standard", "engine": "demucs", "stems": ["voice", "drum"]})
        self.assertEqual(plan["mode"], "standard")
        self.assertEqual(plan["stems"], ["vocals", "drums"])
        self.assertEqual(plan["steps"][0]["engine"], "demucs")

    def test_auto_mode_selects_six_stem_demucs(self) -> None:
        plan = plan_mode(
            {
                "mode": "auto",
                "stems": ["vocals", "drums", "bass", "guitar", "piano", "other"],
            },
            installed_engines=["demucs"],
        )
        self.assertEqual(plan["steps"][0]["model"], "htdemucs_6s")

    def test_comparison_requires_two_engines(self) -> None:
        with self.assertRaises(ModeError):
            plan_mode({"mode": "comparison", "engines": ["demucs"]})

    def test_ensemble_adds_fusion_step(self) -> None:
        plan = plan_mode({
            "mode": "ensemble",
            "engines": ["demucs", "openunmix"],
            "models": ["htdemucs_ft", "umxhq"],
        })
        self.assertEqual(plan["steps"][-1]["kind"], "fuse")
        self.assertEqual(plan["terminalStep"], "ensemble-fusion")

    def test_cascade_links_steps(self) -> None:
        plan = plan_mode({
            "mode": "cascade",
            "engines": ["demucs", "audio-separator"],
            "cascadeStem": "vocals",
        })
        self.assertIsNone(plan["steps"][0]["sourceFrom"])
        self.assertEqual(plan["steps"][1]["sourceFrom"], "cascade-1")
        self.assertEqual(plan["steps"][1]["sourceStem"], "vocals")

    def test_god_mode_adds_fusion_and_benchmark(self) -> None:
        plan = plan_mode({
            "mode": "god",
            "engines": ["demucs", "openunmix", "audio-separator"],
            "models": ["htdemucs_ft", "umxhq", "UVR-MDX-NET-Inst_HQ_3.onnx"],
        })
        self.assertEqual(plan["steps"][-2]["kind"], "fuse")
        self.assertEqual(plan["steps"][-1]["kind"], "benchmark")


if __name__ == "__main__":
    unittest.main()
