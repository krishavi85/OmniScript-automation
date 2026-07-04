from __future__ import annotations

import base64
import hashlib
import json
from pathlib import Path
from typing import Any


def verify_manifest(manifest_path: Path, signature_path: Path,
                    public_key_b64: str) -> dict[str, Any]:
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey

    payload = manifest_path.read_bytes()
    signature = base64.b64decode(signature_path.read_text(encoding="utf-8").strip())
    public_key = Ed25519PublicKey.from_public_bytes(base64.b64decode(public_key_b64))
    public_key.verify(signature, payload)
    manifest = json.loads(payload.decode("utf-8"))
    if not isinstance(manifest, dict) or "version" not in manifest or "artifact" not in manifest:
        raise ValueError("invalid update manifest")
    return manifest


def verify_artifact(path: Path, expected_sha256: str) -> bool:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().lower() == expected_sha256.lower()
