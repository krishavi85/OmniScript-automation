from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path

WORKER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(WORKER_DIR))

API_AVAILABLE = importlib.util.find_spec("fastapi") is not None and importlib.util.find_spec("httpx") is not None


@unittest.skipUnless(API_AVAILABLE, "FastAPI test dependencies are optional")
class ApiAppTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        from fastapi.testclient import TestClient
        from api_app import app

        cls.client = TestClient(app)

    def test_health_and_methods(self) -> None:
        health = self.client.get("/health")
        self.assertEqual(health.status_code, 200)
        self.assertEqual(health.json()["status"], "ok")
        methods = self.client.get("/methods")
        self.assertEqual(methods.status_code, 200)
        self.assertIn("mode.run", methods.json()["methods"])
        self.assertIn("pipeline.run", methods.json()["methods"])

    def test_models_endpoint(self) -> None:
        response = self.client.get("/models", params={"engine": "demucs", "include_deprecated": False})
        self.assertEqual(response.status_code, 200)
        rows = response.json()["models"]
        self.assertTrue(rows)
        self.assertTrue(all(row["engine"] == "demucs" for row in rows))

    def test_rpc_endpoint(self) -> None:
        response = self.client.post("/rpc", json={
            "id": "api-test",
            "method": "taxonomy.normalize",
            "params": {"stems": ["voice", "drum"]},
        })
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.json()["result"]["stems"], ["vocals", "drums"])

    def test_unknown_job_returns_404(self) -> None:
        response = self.client.get("/jobs/not-a-job")
        self.assertEqual(response.status_code, 404)


if __name__ == "__main__":
    unittest.main()
