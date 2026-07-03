from __future__ import annotations

import base64
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


def canonical_payload(consent: dict[str, Any]) -> bytes:
    value = {key: consent[key] for key in sorted(consent) if key != "signature"}
    return json.dumps(value, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def verify_consent(path: Path, public_key_b64: str, expected_voice_id: str,
                   expected_model_sha256: str) -> dict[str, Any]:
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey

    consent = json.loads(path.read_text(encoding="utf-8"))
    required = {"voiceId", "voiceOwner", "modelSha256", "purpose", "issuedAt", "expiresAt", "signature"}
    if not isinstance(consent, dict) or not required.issubset(consent):
        raise ValueError("voice consent document is incomplete")
    if str(consent["voiceId"]) != expected_voice_id:
        raise PermissionError("consent does not match the requested voice")
    if str(consent["modelSha256"]).lower() != expected_model_sha256.lower():
        raise PermissionError("consent does not match the model artifact")
    issued = datetime.fromisoformat(str(consent["issuedAt"]).replace("Z", "+00:00"))
    expires = datetime.fromisoformat(str(consent["expiresAt"]).replace("Z", "+00:00"))
    now = datetime.now(timezone.utc)
    if issued.tzinfo is None or expires.tzinfo is None or now < issued or now >= expires:
        raise PermissionError("voice consent is not currently valid")
    signature = base64.b64decode(str(consent["signature"]))
    public_key = Ed25519PublicKey.from_public_bytes(base64.b64decode(public_key_b64))
    public_key.verify(signature, canonical_payload(consent))
    consent["consentSha256"] = hashlib.sha256(canonical_payload(consent)).hexdigest()
    return consent
