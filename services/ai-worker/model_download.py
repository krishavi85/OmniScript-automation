from __future__ import annotations

import hashlib
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any, Callable

from model_registry import ModelRegistryError, model_root, register


Progress = Callable[[int, int | None], None]


def _digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def download_and_register(
    request: dict[str, Any],
    progress: Progress | None = None,
    root: Path | None = None,
) -> dict[str, Any]:
    source_url = str(request.get("sourceUrl", "")).strip()
    parsed = urllib.parse.urlparse(source_url)
    if parsed.scheme != "https" or not parsed.netloc:
        raise ModelRegistryError("sourceUrl must be an HTTPS URL")

    model_id = str(request.get("modelId", "")).strip()
    version = str(request.get("version", "")).strip()
    expected = str(request.get("sha256", "")).strip().lower()
    if not model_id or not version:
        raise ModelRegistryError("modelId and version are required")
    if len(expected) != 64 or any(char not in "0123456789abcdef" for char in expected):
        raise ModelRegistryError("sha256 must contain 64 hexadecimal characters")

    base = (root or model_root()).resolve()
    downloads = base / ".downloads" / model_id / version
    downloads.mkdir(parents=True, exist_ok=True)
    filename = Path(urllib.parse.unquote(parsed.path)).name or "model.bin"
    partial = downloads / f"{filename}.part"
    completed = downloads / filename
    offset = partial.stat().st_size if partial.exists() else 0

    headers = {"User-Agent": "OmniStem-Studio/1.0"}
    if offset:
        headers["Range"] = f"bytes={offset}-"
    network_request = urllib.request.Request(source_url, headers=headers)

    try:
        response = urllib.request.urlopen(network_request, timeout=30)
    except OSError as exc:
        raise ModelRegistryError(f"Model download failed: {exc}") from exc

    status = getattr(response, "status", response.getcode())
    if offset and status != 206:
        offset = 0
    mode = "ab" if offset and status == 206 else "wb"
    total_header = response.headers.get("Content-Length")
    remaining = int(total_header) if total_header and total_header.isdigit() else None
    total = offset + remaining if remaining is not None else None
    written = offset

    with response, partial.open(mode) as output:
        while True:
            block = response.read(1024 * 1024)
            if not block:
                break
            output.write(block)
            written += len(block)
            if progress is not None:
                progress(written, total)

    actual = _digest(partial)
    if actual != expected:
        raise ModelRegistryError(f"Downloaded model checksum mismatch: expected {expected}, received {actual}")
    partial.replace(completed)

    registration = dict(request)
    registration["artifact"] = str(completed)
    registration.pop("sourceUrl", None)
    result = register(registration, base)
    return {**result, "sourceUrl": source_url, "downloadedBytes": written}
