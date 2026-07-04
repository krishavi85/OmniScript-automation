from __future__ import annotations

import base64
import hashlib
import json
import os
from datetime import datetime, timezone
from pathlib import Path


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def create_signed_manifest(version: str, artifact: Path, url: str,
                           manifest_path: Path, signature_path: Path) -> dict:
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

    key_path = os.environ.get("OMNISTEM_UPDATE_SIGNING_KEY_FILE", "")
    if not key_path:
        raise RuntimeError("OMNISTEM_UPDATE_SIGNING_KEY_FILE is not configured")
    key_bytes = base64.b64decode(Path(key_path).read_text(encoding="utf-8").strip())
    private_key = Ed25519PrivateKey.from_private_bytes(key_bytes)
    manifest = {
        "schemaVersion": 1,
        "version": version,
        "publishedAt": datetime.now(timezone.utc).isoformat(),
        "artifact": {
            "name": artifact.name,
            "url": url,
            "size": artifact.stat().st_size,
            "sha256": file_sha256(artifact),
        },
    }
    payload = json.dumps(manifest, sort_keys=True, separators=(",", ":")).encode("utf-8")
    manifest_path.write_bytes(payload)
    signature_path.write_text(
        base64.b64encode(private_key.sign(payload)).decode("ascii"),
        encoding="utf-8",
    )
    return manifest
