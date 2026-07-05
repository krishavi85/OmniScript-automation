from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from model_registry import ModelRegistryError, register


class ModelPolicyTests(unittest.TestCase):
    def test_license_acceptance_is_required(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            request = {
                "modelId": "test-model",
                "version": "1.0.0",
                "artifact": __file__,
                "sha256": "0" * 64,
                "licenseId": "example-license",
                "licenseUrl": "https://example.com/license",
                "licenseAccepted": False,
            }
            with self.assertRaises(ModelRegistryError):
                register(request, Path(directory))


if __name__ == "__main__":
    unittest.main()
