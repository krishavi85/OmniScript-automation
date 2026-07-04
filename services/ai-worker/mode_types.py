from __future__ import annotations

from dataclasses import dataclass


class ModeError(ValueError):
    pass


SUPPORTED_MODES = frozenset({
    "standard",
    "comparison",
    "ensemble",
    "cascade",
    "auto",
    "god",
})

DEFAULT_ENGINE_ORDER = (
    "audio-separator",
    "demucs",
    "openunmix",
    "spleeter",
)


@dataclass(frozen=True)
class EngineChoice:
    engine: str
    model: str | None = None
