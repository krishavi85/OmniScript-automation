from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import urllib.request
from pathlib import Path
from typing import Any

_ALLOWED_LICENSES = {"MIT", "Apache-2.0", "BSD-2-Clause", "BSD-3-Clause", "CC-BY-4.0"}
_ALLOWED_TASKS = {"polyphonic-analysis", "polyphonic-render", "denoise", "dereverb", "declip", "vocal-conversion"}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validate_lock(lock: dict[str, Any]) -> list[dict[str, Any]]:
    models = lock.get("models")
    if not isinstance(models, list) or not models:
        raise ValueError("model lock requires a non-empty models array")
    result: list[dict[str, Any]] = []
    seen: set[str] = set()
    for index, model in enumerate(models):
        if not isinstance(model, dict):
            raise ValueError(f"model {index} must be an object")
        model_id = str(model.get("id", "")).strip()
        task = str(model.get("task", "")).strip()
        license_id = str(model.get("license", "")).strip()
        digest = str(model.get("sha256", "")).lower()
        source = str(model.get("source", "")).strip()
        filename = str(model.get("filename", "")).strip()
        if not model_id or model_id in seen:
            raise ValueError(f"model {index} has an empty or duplicate id")
        if task not in _ALLOWED_TASKS:
            raise ValueError(f"model {model_id} has unsupported task: {task}")
        if license_id not in _ALLOWED_LICENSES:
            raise ValueError(f"model {model_id} license is not approved: {license_id}")
        if len(digest) != 64 or any(character not in "0123456789abcdef" for character in digest):
            raise ValueError(f"model {model_id} requires an exact SHA-256 digest")
        if not source or not filename or Path(filename).name != filename:
            raise ValueError(f"model {model_id} requires a safe filename and source")
        seen.add(model_id)
        result.append({
            "id": model_id,
            "task": task,
            "license": license_id,
            "sha256": digest,
            "source": source,
            "filename": filename,
            "manifest": str(model.get("manifest", "")),
            "required": bool(model.get("required", True)),
        })
    return result


def fetch(source: str, destination: Path) -> None:
    if source.startswith("file://"):
        shutil.copy2(Path(source[7:]), destination)
        return
    request = urllib.request.Request(source, headers={"User-Agent": "OmniStem-ModelBundler/1"})
    with urllib.request.urlopen(request, timeout=180) as response, destination.open("wb") as output:
        shutil.copyfileobj(response, output)


def build_bundle(lock_path: Path, output_dir: Path, cache_dir: Path) -> dict[str, Any]:
    lock = json.loads(lock_path.read_text(encoding="utf-8"))
    models = validate_lock(lock)
    output_dir.mkdir(parents=True, exist_ok=True)
    cache_dir.mkdir(parents=True, exist_ok=True)
    bundled: list[dict[str, Any]] = []
    for model in models:
        cached = cache_dir / model["filename"]
        if not cached.is_file() or sha256(cached) != model["sha256"]:
            temporary = cached.with_suffix(cached.suffix + ".part")
            temporary.unlink(missing_ok=True)
            fetch(model["source"], temporary)
            if sha256(temporary) != model["sha256"]:
                temporary.unlink(missing_ok=True)
                raise ValueError(f"checksum mismatch for {model['id']}")
            temporary.replace(cached)
        destination = output_dir / model["filename"]
        shutil.copy2(cached, destination)
        bundled.append({**model, "path": str(destination)})
    manifest = {"version": 1, "models": bundled}
    (output_dir / "bundle-manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description="Build a verified OmniStem neural-model bundle")
    parser.add_argument("lock", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--cache", type=Path, default=Path.home() / ".cache" / "omnistem-models")
    args = parser.parse_args()
    result = build_bundle(args.lock.resolve(), args.output.resolve(), args.cache.resolve())
    print(json.dumps({"models": len(result["models"]), "output": str(args.output.resolve())}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
