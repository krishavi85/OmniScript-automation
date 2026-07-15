from __future__ import annotations

import hashlib
import tempfile
import unittest
from pathlib import Path

from model_registry import ModelRegistryError, list_models, register


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

    def test_matching_artifact_is_registered_as_verified(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            artifact = Path(directory) / "model.bin"
            artifact.write_bytes(b"verified model bytes")
            digest = hashlib.sha256(artifact.read_bytes()).hexdigest()
            result = register(
                {
                    "modelId": "verified-model",
                    "version": "1.0.0",
                    "artifact": str(artifact),
                    "sha256": digest,
                    "licenseId": "example-license",
                    "licenseUrl": "https://example.com/license",
                    "licenseAccepted": True,
                },
                Path(directory) / "registry",
            )
            self.assertTrue(result["verified"])
            self.assertTrue(list_models(Path(directory) / "registry")[0]["verified"])

    def test_checksum_mismatch_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            artifact = Path(directory) / "model.bin"
            artifact.write_bytes(b"unexpected bytes")
            with self.assertRaises(ModelRegistryError):
                register(
                    {
                        "modelId": "invalid-model",
                        "version": "1.0.0",
                        "artifact": str(artifact),
                        "sha256": "0" * 64,
                        "licenseId": "example-license",
                        "licenseUrl": "https://example.com/license",
                        "licenseAccepted": True,
                    },
                    Path(directory) / "registry",
                )


if __name__ == "__main__":
    unittest.main()
