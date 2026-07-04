from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

WORKER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(WORKER_DIR))

from studio_cli import build_parser, dispatch


class StudioCliTests(unittest.TestCase):
    def setUp(self) -> None:
        self.root = Path(tempfile.mkdtemp(prefix="omnistem-cli-"))
        self.source = self.root / "song.wav"
        self.source.write_bytes(b"placeholder")
        self.parser = build_parser()

    def test_versioned_environment_command(self) -> None:
        args = self.parser.parse_args(["env"])
        result = dispatch(args)
        self.assertEqual(result["omnistem"], "0.4.0")
        self.assertIn("python", result)

    def test_models_command_uses_catalog(self) -> None:
        args = self.parser.parse_args(["models", "--engine", "demucs", "--current-only"])
        result = dispatch(args)
        self.assertTrue(result["models"])
        self.assertTrue(all(row["engine"] == "demucs" for row in result["models"]))

    def test_separate_dry_run_returns_standard_plan(self) -> None:
        args = self.parser.parse_args([
            "separate",
            str(self.source),
            "--engine",
            "demucs",
            "--dry-run",
        ])
        result = dispatch(args)
        self.assertEqual(result["mode"], "standard")
        self.assertEqual(result["steps"][0]["engine"], "demucs")

    def test_compare_dry_run_returns_comparison_plan(self) -> None:
        args = self.parser.parse_args([
            "compare",
            str(self.source),
            "--engines",
            "demucs,openunmix",
            "--models",
            "htdemucs_ft,umxhq",
            "--dry-run",
        ])
        result = dispatch(args)
        self.assertEqual(result["mode"], "comparison")
        self.assertEqual(len(result["steps"]), 2)

    def test_pipeline_dry_run_validates_definition(self) -> None:
        definition = self.root / "pipeline.json"
        definition.write_text(
            '{"name":"test","steps":[{"id":"health","method":"health.check"}]}',
            encoding="utf-8",
        )
        args = self.parser.parse_args(["pipeline", str(definition), "--dry-run"])
        result = dispatch(args)
        self.assertTrue(result["valid"])
        self.assertEqual(result["orderedSteps"], ["health"])


if __name__ == "__main__":
    unittest.main()
