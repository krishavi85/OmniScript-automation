from __future__ import annotations

import hashlib
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Iterable


class CatalogError(ValueError):
    pass


STEM_ALIASES: dict[str, str] = {
    "voice": "vocals",
    "vocal": "vocals",
    "lead-vocal": "vocals",
    "lead_vocal": "vocals",
    "backing-vocals": "backing_vocals",
    "backing_vocal": "backing_vocals",
    "instrumental": "instrumental",
    "accompaniment": "instrumental",
    "music": "instrumental",
    "drum": "drums",
    "percussion": "drums",
    "bassline": "bass",
    "guitars": "guitar",
    "keys": "piano",
    "keyboard": "piano",
    "strings-section": "strings",
    "other": "other",
}

CANONICAL_STEMS = frozenset({
    "vocals",
    "backing_vocals",
    "instrumental",
    "drums",
    "bass",
    "guitar",
    "piano",
    "strings",
    "wind",
    "brass",
    "other",
})


@dataclass(frozen=True)
class ModelRecord:
    id: str
    name: str
    engine: str
    family: str
    architecture: str
    version: str
    stems: tuple[str, ...]
    aliases: tuple[str, ...]
    tags: tuple[str, ...]
    license: str
    source: str
    sha256: str | None = None
    deprecated: bool = False
    replacement: str | None = None
    notes: str = ""


MODELS: tuple[ModelRecord, ...] = (
    ModelRecord(
        id="htdemucs",
        name="HTDemucs",
        engine="demucs",
        family="demucs-transformer",
        architecture="HTDemucs",
        version="4",
        stems=("vocals", "drums", "bass", "other"),
        aliases=("demucs-default",),
        tags=("standard", "four-stem", "general"),
        license="Upstream Demucs model terms",
        source="https://github.com/adefossez/demucs",
    ),
    ModelRecord(
        id="htdemucs_ft",
        name="HTDemucs Fine-Tuned",
        engine="demucs",
        family="demucs-transformer",
        architecture="HTDemucs",
        version="4-ft",
        stems=("vocals", "drums", "bass", "other"),
        aliases=("demucs-studio", "htdemucs-finetuned"),
        tags=("studio", "four-stem", "fine-tuned"),
        license="Upstream Demucs model terms",
        source="https://github.com/adefossez/demucs",
    ),
    ModelRecord(
        id="htdemucs_6s",
        name="HTDemucs Six Source",
        engine="demucs",
        family="demucs-transformer",
        architecture="HTDemucs",
        version="4-6s",
        stems=("vocals", "drums", "bass", "guitar", "piano", "other"),
        aliases=("demucs-six",),
        tags=("six-stem", "guitar", "piano"),
        license="Upstream Demucs model terms",
        source="https://github.com/adefossez/demucs",
    ),
    ModelRecord(
        id="model_bs_roformer_ep_317_sdr_12.9755.ckpt",
        name="BS-RoFormer Viperx 1297",
        engine="audio-separator",
        family="bs-roformer",
        architecture="BS-RoFormer",
        version="317",
        stems=("vocals", "instrumental"),
        aliases=("bs-roformer-viperx",),
        tags=("two-stem", "vocals", "roformer"),
        license="Review model-specific metadata before distribution or commercial use",
        source="https://github.com/nomadkaraoke/python-audio-separator",
    ),
    ModelRecord(
        id="UVR-MDX-NET-Inst_HQ_3.onnx",
        name="UVR MDX-Net Instrumental HQ 3",
        engine="audio-separator",
        family="mdx-net",
        architecture="MDX-Net",
        version="3",
        stems=("vocals", "instrumental"),
        aliases=("mdx-inst-hq3",),
        tags=("two-stem", "vocals", "instrumental"),
        license="Review model-specific metadata before distribution or commercial use",
        source="https://github.com/nomadkaraoke/python-audio-separator",
    ),
    ModelRecord(
        id="spleeter:2stems",
        name="Spleeter Two Stem",
        engine="spleeter",
        family="spleeter",
        architecture="Spleeter",
        version="2",
        stems=("vocals", "instrumental"),
        aliases=("spleeter-two",),
        tags=("two-stem", "legacy"),
        license="MIT code; pretrained-model terms remain upstream",
        source="https://github.com/deezer/spleeter",
        deprecated=True,
        replacement="htdemucs_ft",
        notes="Retained for compatibility and comparison workflows.",
    ),
    ModelRecord(
        id="umxhq",
        name="Open-Unmix UMXHQ",
        engine="openunmix",
        family="open-unmix",
        architecture="Open-Unmix",
        version="1",
        stems=("vocals", "drums", "bass", "other"),
        aliases=("openunmix-hq",),
        tags=("four-stem", "reference"),
        license="MIT code; verify pretrained-weight terms upstream",
        source="https://github.com/sigsep/open-unmix-pytorch",
    ),
)


def normalize_stem(value: str) -> str:
    normalized = value.strip().lower().replace(" ", "_")
    normalized = STEM_ALIASES.get(normalized, normalized)
    if normalized not in CANONICAL_STEMS:
        raise CatalogError(f"unsupported stem taxonomy value: {value}")
    return normalized


def normalize_stems(values: Iterable[str]) -> list[str]:
    result: list[str] = []
    for value in values:
        stem = normalize_stem(value)
        if stem not in result:
            result.append(stem)
    if not result:
        raise CatalogError("at least one stem is required")
    return result


def resolve_model(identifier: str) -> ModelRecord:
    needle = identifier.strip().casefold()
    for model in MODELS:
        if model.id.casefold() == needle or any(alias.casefold() == needle for alias in model.aliases):
            return model
    raise CatalogError(f"unknown model: {identifier}")


def search_models(
    *,
    query: str | None = None,
    engine: str | None = None,
    family: str | None = None,
    tag: str | None = None,
    stem: str | None = None,
    include_deprecated: bool = True,
    sort_by: str = "name",
    descending: bool = False,
) -> list[dict[str, Any]]:
    rows = list(MODELS)
    if not include_deprecated:
        rows = [row for row in rows if not row.deprecated]
    if engine:
        rows = [row for row in rows if row.engine.casefold() == engine.casefold()]
    if family:
        rows = [row for row in rows if row.family.casefold() == family.casefold()]
    if tag:
        rows = [row for row in rows if tag.casefold() in {item.casefold() for item in row.tags}]
    if stem:
        normalized_stem = normalize_stem(stem)
        rows = [row for row in rows if normalized_stem in row.stems]
    if query:
        needle = query.casefold()
        rows = [
            row for row in rows
            if needle in row.id.casefold()
            or needle in row.name.casefold()
            or needle in row.engine.casefold()
            or needle in row.family.casefold()
            or any(needle in value.casefold() for value in row.aliases + row.tags + row.stems)
        ]
    allowed_sort = {
        "id": lambda row: row.id.casefold(),
        "name": lambda row: row.name.casefold(),
        "engine": lambda row: (row.engine.casefold(), row.name.casefold()),
        "family": lambda row: (row.family.casefold(), row.name.casefold()),
        "version": lambda row: row.version.casefold(),
    }
    if sort_by not in allowed_sort:
        raise CatalogError(f"unsupported sort field: {sort_by}")
    rows.sort(key=allowed_sort[sort_by], reverse=descending)
    return [asdict(row) for row in rows]


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_artifact(path: Path, expected_sha256: str) -> dict[str, Any]:
    if not path.is_file():
        raise CatalogError(f"artifact does not exist: {path}")
    expected = expected_sha256.strip().lower()
    if len(expected) != 64 or any(character not in "0123456789abcdef" for character in expected):
        raise CatalogError("expected SHA-256 must contain exactly 64 hexadecimal characters")
    actual = sha256_file(path)
    return {
        "path": str(path.resolve()),
        "algorithm": "sha256",
        "expected": expected,
        "actual": actual,
        "verified": actual == expected,
    }
