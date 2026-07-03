from __future__ import annotations

import base64
import hashlib
import json
import sys
import tempfile
import unittest
from datetime import datetime, timedelta, timezone
from pathlib import Path

WORKER_DIR = Path(__file__).resolve().parents[1]
ROOT_DIR = WORKER_DIR.parents[1]
sys.path.insert(0, str(WORKER_DIR))
sys.path.insert(0, str(ROOT_DIR / "tools"))

from model_bundle import build_bundle
from neural_note_engine import load_config
from voice_consent import canonical_payload, verify_consent


class AdvancedSecurityTests(unittest.TestCase):
    def setUp(self) -> None:
        self.root = Path(tempfile.mkdtemp(prefix="omnistem-security-test-"))

    def test_neural_note_manifest_validation(self) -> None:
        path = self.root / "note.json"
        path.write_text(json.dumps({
            "sampleRate": 48000,
            "audioInput": "audio",
            "conditionInput": "condition",
            "audioOutput": "output",
            "chunkSamples": 4096,
            "overlapSamples": 512,
        }), encoding="utf-8")
        config = load_config(path)
        self.assertEqual(config["chunkSamples"], 4096)

    def test_model_bundle_uses_exact_checksum(self) -> None:
        model = self.root / "test.onnx"
        model.write_bytes(b"deterministic-test-model")
        digest = hashlib.sha256(model.read_bytes()).hexdigest()
        lock = self.root / "lock.json"
        lock.write_text(json.dumps({"models": [{
            "id": "test-denoise",
            "task": "denoise",
            "license": "MIT",
            "sha256": digest,
            "source": model.as_uri(),
            "filename": "test.onnx",
        }]}), encoding="utf-8")
        result = build_bundle(lock, self.root / "bundle", self.root / "cache")
        self.assertEqual(result["models"][0]["sha256"], digest)
        self.assertTrue((self.root / "bundle" / "test.onnx").is_file())

    def test_voice_consent_signature_and_expiry(self) -> None:
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
        from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat

        private_key = Ed25519PrivateKey.generate()
        public_key = private_key.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw)
        now = datetime.now(timezone.utc)
        consent = {
            "voiceId": "artist-owned-voice",
            "voiceOwner": "Test Artist",
            "modelSha256": "a" * 64,
            "purpose": "authorized studio production",
            "issuedAt": now.isoformat(),
            "expiresAt": (now + timedelta(hours=1)).isoformat(),
        }
        consent["signature"] = base64.b64encode(
            private_key.sign(canonical_payload(consent))
        ).decode("ascii")
        path = self.root / "consent.json"
        path.write_text(json.dumps(consent), encoding="utf-8")
        verified = verify_consent(
            path, base64.b64encode(public_key).decode("ascii"),
            "artist-owned-voice", "a" * 64,
        )
        self.assertEqual(verified["voiceOwner"], "Test Artist")
        self.assertEqual(len(verified["consentSha256"]), 64)


if __name__ == "__main__":
    unittest.main()
