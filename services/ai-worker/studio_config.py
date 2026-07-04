from __future__ import annotations

import os
import tomllib
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any


class ConfigError(ValueError):
    pass


@dataclass(frozen=True)
class StudioConfig:
    output_dir: Path
    default_engine: str = "demucs"
    default_model: str = "htdemucs_ft"
    worker_queue: Path | None = None
    api_host: str = "127.0.0.1"
    api_port: int = 8765
    max_background_jobs: int = 2

    def public_dict(self) -> dict[str, Any]:
        data = asdict(self)
        data["output_dir"] = str(self.output_dir)
        data["worker_queue"] = str(self.worker_queue) if self.worker_queue else None
        return data


def default_config_path() -> Path:
    configured = os.environ.get("OMNISTEM_CONFIG", "").strip()
    if configured:
        return Path(configured).expanduser().resolve()
    return Path.home() / ".omnistem" / "config.toml"


def _section(payload: dict[str, Any]) -> dict[str, Any]:
    value = payload.get("omnistem", payload)
    if not isinstance(value, dict):
        raise ConfigError("configuration root must be a TOML table")
    return value


def load_config(path: Path | None = None) -> StudioConfig:
    config_path = (path or default_config_path()).expanduser().resolve()
    payload: dict[str, Any] = {}
    if config_path.exists():
        try:
            with config_path.open("rb") as handle:
                payload = tomllib.load(handle)
        except (OSError, tomllib.TOMLDecodeError) as exc:
            raise ConfigError(f"could not read configuration: {exc}") from exc
    values = _section(payload)

    output_dir = Path(os.environ.get("OMNISTEM_OUTPUT_DIR", values.get("output_dir", "outputs"))).expanduser().resolve()
    default_engine = os.environ.get("OMNISTEM_DEFAULT_ENGINE", str(values.get("default_engine", "demucs"))).strip().lower()
    default_model = os.environ.get("OMNISTEM_DEFAULT_MODEL", str(values.get("default_model", "htdemucs_ft"))).strip()
    queue_value = os.environ.get("OMNISTEM_WORKER_QUEUE", str(values.get("worker_queue", ""))).strip()
    api_host = os.environ.get("OMNISTEM_API_HOST", str(values.get("api_host", "127.0.0.1"))).strip()

    try:
        api_port = int(os.environ.get("OMNISTEM_API_PORT", values.get("api_port", 8765)))
        max_jobs = int(os.environ.get("OMNISTEM_MAX_BACKGROUND_JOBS", values.get("max_background_jobs", 2)))
    except (TypeError, ValueError) as exc:
        raise ConfigError("api_port and max_background_jobs must be integers") from exc

    if not default_engine:
        raise ConfigError("default_engine must not be empty")
    if not default_model:
        raise ConfigError("default_model must not be empty")
    if api_host not in {"127.0.0.1", "localhost", "::1"}:
        raise ConfigError("the built-in API is local-only; api_host must be a loopback address")
    if not 1 <= api_port <= 65535:
        raise ConfigError("api_port must be between 1 and 65535")
    if not 1 <= max_jobs <= 32:
        raise ConfigError("max_background_jobs must be between 1 and 32")

    return StudioConfig(
        output_dir=output_dir,
        default_engine=default_engine,
        default_model=default_model,
        worker_queue=Path(queue_value).expanduser().resolve() if queue_value else None,
        api_host=api_host,
        api_port=api_port,
        max_background_jobs=max_jobs,
    )


def write_default_config(path: Path | None = None, overwrite: bool = False) -> Path:
    destination = (path or default_config_path()).expanduser().resolve()
    if destination.exists() and not overwrite:
        raise ConfigError(f"configuration already exists: {destination}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    content = """[omnistem]
output_dir = "outputs"
default_engine = "demucs"
default_model = "htdemucs_ft"
worker_queue = ""
api_host = "127.0.0.1"
api_port = 8765
max_background_jobs = 2
"""
    destination.write_text(content, encoding="utf-8")
    return destination
