from __future__ import annotations

import sys
import unittest
from pathlib import Path

WORKER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(WORKER_DIR))

from pipeline_runtime import PipelineError, resolve_references, run_pipeline, validate_pipeline


class PipelineRuntimeTests(unittest.TestCase):
    def test_validation_orders_dependencies(self) -> None:
        steps = validate_pipeline({
            "steps": [
                {"id": "second", "method": "example.two", "dependsOn": ["first"]},
                {"id": "first", "method": "example.one"},
            ]
        })
        self.assertEqual([step["id"] for step in steps], ["first", "second"])

    def test_cycle_is_rejected(self) -> None:
        with self.assertRaises(PipelineError):
            validate_pipeline({
                "steps": [
                    {"id": "a", "method": "x", "dependsOn": ["b"]},
                    {"id": "b", "method": "y", "dependsOn": ["a"]},
                ]
            })

    def test_reference_resolution(self) -> None:
        value = resolve_references(
            {"source": "${separate.stems.vocals}"},
            {"separate": {"stems": {"vocals": "vocals.wav"}}},
        )
        self.assertEqual(value["source"], "vocals.wav")

    def test_pipeline_executes_sync_steps(self) -> None:
        def dispatch(method: str, params: dict[str, object]) -> dict[str, object]:
            if method == "seed":
                return {"result": {"value": 4}}
            return {"result": {"value": int(params["value"]) * 2}}

        result = run_pipeline(
            {
                "name": "double",
                "steps": [
                    {"id": "seed", "method": "seed"},
                    {
                        "id": "double",
                        "method": "double",
                        "dependsOn": ["seed"],
                        "params": {"value": "${seed.value}"},
                    },
                ],
            },
            dispatch=dispatch,
            job_snapshot=lambda _: {},
            cancel_requested=lambda: False,
            poll_seconds=0.01,
        )
        self.assertEqual(result["terminalResult"]["value"], 8)

    def test_nested_pipeline_is_rejected(self) -> None:
        with self.assertRaises(PipelineError):
            validate_pipeline({"steps": [{"id": "nested", "method": "pipeline.run"}]})


if __name__ == "__main__":
    unittest.main()
