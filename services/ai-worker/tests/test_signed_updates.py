from __future__ import annotations

import base64
import importlib.util
import os
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))

from sign_release import create_signed_manifest
from update_verifier import verify_artifact, verify_manifest

HAS_CRYPTOGRAPHY = importlib.util.find_spec("cryptography") is not None


@unittest.skipUnless(HAS_CRYPTOGRAPHY, "cryptography is an optional production dependency")
class SignedUpdateTests(unittest.TestCase):
    def test_manifest_signature_and_artifact_hash(self) -> None:
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
        from cryptography.hazmat.primitives.serialization import Encoding, PrivateFormat, PublicFormat, NoEncryption

        root = Path(tempfile.mkdtemp(prefix="omnistem-update-test-"))
        artifact = root / "OmniStem-Setup.exe"
        artifact.write_bytes(b"test installer payload")
        private_key = Ed25519PrivateKey.generate()
        private_raw = private_key.private_bytes(Encoding.Raw, PrivateFormat.Raw, NoEncryption())
        public_raw = private_key.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw)
        key_file = root / "signing.key"
        key_file.write_text(base64.b64encode(private_raw).decode("ascii"), encoding="utf-8")
        previous = os.environ.get("OMNISTEM_UPDATE_SIGNING_KEY_FILE")
        os.environ["OMNISTEM_UPDATE_SIGNING_KEY_FILE"] = str(key_file)
        try:
            manifest_path = root / "manifest.json"
            signature_path = root / "manifest.sig"
            create_signed_manifest(
                "1.2.3", artifact, "https://example.invalid/OmniStem-Setup.exe",
                manifest_path, signature_path,
            )
            manifest = verify_manifest(
                manifest_path, signature_path,
                base64.b64encode(public_raw).decode("ascii"),
            )
            self.assertEqual(manifest["version"], "1.2.3")
            self.assertTrue(verify_artifact(artifact, manifest["artifact"]["sha256"]))
            artifact.write_bytes(b"tampered")
            self.assertFalse(verify_artifact(artifact, manifest["artifact"]["sha256"]))
        finally:
            if previous is None:
                os.environ.pop("OMNISTEM_UPDATE_SIGNING_KEY_FILE", None)
            else:
                os.environ["OMNISTEM_UPDATE_SIGNING_KEY_FILE"] = previous


if __name__ == "__main__":
    unittest.main()
