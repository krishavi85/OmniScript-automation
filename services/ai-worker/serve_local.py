from __future__ import annotations

from pathlib import Path

from studio_config import load_config


def run(config_path: Path | None = None) -> None:
    try:
        import uvicorn
    except ImportError as exc:
        raise RuntimeError("Install services/ai-worker/requirements-api.txt to use the local API") from exc

    config = load_config(config_path)
    uvicorn.run(
        "api_app:app",
        host=config.api_host,
        port=config.api_port,
        reload=False,
        access_log=True,
    )


if __name__ == "__main__":
    run()
