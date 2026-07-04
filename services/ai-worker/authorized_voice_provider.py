from __future__ import annotations

import hashlib
import json
import shutil
import subprocess
from pathlib import Path
from typing import Any

from voice_consent import verify_consent


def file_hash(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def run_authorized_provider(source: Path, output: Path, provider_manifest: Path,
                            consent_file: Path, public_key_b64: str,
                            voice_id: str, model_file: Path) -> dict[str, Any]:
    provider = json.loads(provider_manifest.read_text(encoding="utf-8"))
    command_template = provider.get("command")
    if not isinstance(command_template, list) or not command_template:
        raise ValueError("provider manifest requires a command array")
    model_digest = file_hash(model_file)
    consent = verify_consent(consent_file, public_key_b64, voice_id, model_digest)
    executable = shutil.which(str(command_template[0]))
    if executable is None:
        raise RuntimeError("approved voice provider executable is unavailable")

    output.parent.mkdir(parents=True, exist_ok=True)
    replacements = {
        "{source}": str(source),
        "{output}": str(output),
        "{model}": str(model_file),
        "{voice_id}": voice_id,
    }
    command = [executable]
    for value in command_template[1:]:
        item = str(value)
        for key, replacement in replacements.items():
            item = item.replace(key, replacement)
        command.append(item)
    completed = subprocess.run(command, capture_output=True, text=True, check=False)
    if completed.returncode != 0 or not output.is_file():
        raise RuntimeError(completed.stderr.strip() or "authorized voice provider failed")

    provenance = {
        "version": 1,
        "operation": "authorized-voice-transformation",
        "voiceId": voice_id,
        "voiceOwner": str(consent["voiceOwner"]),
        "purpose": str(consent["purpose"]),
        "consentSha256": str(consent["consentSha256"]),
        "sourceSha256": file_hash(source),
        "outputSha256": file_hash(output),
        "modelSha256": model_digest,
        "provider": str(provider.get("name", provider_manifest.stem)),
    }
    sidecar = output.with_suffix(output.suffix + ".provenance.json")
    sidecar.write_text(json.dumps(provenance, indent=2), encoding="utf-8")
    provenance["provenanceFile"] = str(sidecar)
    return provenance
