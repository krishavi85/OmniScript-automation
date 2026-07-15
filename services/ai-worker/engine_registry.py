from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class EngineSpec:
    engine_id: str
    display_name: str
    input_extensions: frozenset[str]
    output_formats: frozenset[str]
    supported_stem_counts: frozenset[int]


COMMON_INPUTS = frozenset({
    ".wav", ".flac", ".aiff", ".aif", ".mp3", ".aac", ".m4a",
    ".ogg", ".opus", ".wma", ".alac", ".caf",
})

ENGINES: dict[str, EngineSpec] = {
    "audio-separator": EngineSpec(
        "audio-separator", "Audio Separator", COMMON_INPUTS,
        frozenset({"wav", "flac", "mp3", "ogg", "opus", "m4a", "aac", "wma", "aiff", "aif", "caf"}),
        frozenset({1, 2, 4, 5, 6}),
    ),
    "demucs": EngineSpec(
        "demucs", "Demucs", COMMON_INPUTS,
        frozenset({"wav", "flac", "mp3"}), frozenset({2, 4, 6}),
    ),
    "spleeter": EngineSpec(
        "spleeter", "Spleeter", COMMON_INPUTS,
        frozenset({"wav", "flac", "mp3", "ogg", "m4a", "wma"}), frozenset({2, 4, 5}),
    ),
    "openunmix": EngineSpec(
        "openunmix", "Open-Unmix", frozenset({".wav", ".flac", ".ogg"}),
        frozenset({"wav", "flac", "ogg"}), frozenset({1, 2, 4}),
    ),
}


def public_registry() -> list[dict[str, object]]:
    return [
        {
            "id": spec.engine_id,
            "name": spec.display_name,
            "inputExtensions": sorted(spec.input_extensions),
            "outputFormats": sorted(spec.output_formats),
            "supportedStemCounts": sorted(spec.supported_stem_counts),
        }
        for spec in ENGINES.values()
    ]
