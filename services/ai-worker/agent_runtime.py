from __future__ import annotations

import json
import os
import urllib.request
from dataclasses import dataclass
from typing import Any


@dataclass(frozen=True)
class AgentConfig:
    endpoint: str
    model: str
    api_key_env: str = ""
    provider: str = "openai-compatible"
    timeout_seconds: float = 90.0
