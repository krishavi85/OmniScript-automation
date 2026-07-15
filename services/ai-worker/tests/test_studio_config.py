from __future__ import annotations

import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

WORKER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(WORKER_DIR))

from studio_config import ConfigError, load_config, write_default_config


class StudioConfigTests(unittest.TestCase):
    def setUp(self) -> None:
        self.root = Path(tempfile.mkdtemp(prefix="omnistem-config-"))
        self.path = self.root / "config.toml"

    def test_write_and_load_defaults(self) -> None:
        write_default_config(self.path)
        config = load_config(self.path)
        self.assertEqual(config.default_engine, "demucs")
        self.assertEqual(config.api_host, "127.0.0.1")
        self.assertEqual(config.api_port, 8765)

    def test_environment_overrides(self) -> None:
        self.path.write_text('[omnistem]\ndefault_engine="openunmix"\n', encoding="utf-8")
        with patch.dict(os.environ, {"OMNISTEM_DEFAULT_ENGINE": "audio-separator", "OMNISTEM_API_PORT": "9000"}, clear=False):
            config = load_config(self.path)
        self.assertEqual(config.default_engine, "audio-separator")
        self.assertEqual(config.api_port, 9000)

    def test_non_loopback_host_is_rejected(self) -> None:
        self.path.write_text('[omnistem]\napi_host="0.0.0.0"\n', encoding="utf-8")
        with self.assertRaises(ConfigError):
            load_config(self.path)

    def test_existing_file_is_not_overwritten(self) -> None:
        self.path.write_text("existing", encoding="utf-8")
        with self.assertRaises(ConfigError):
            write_default_config(self.path)


if __name__ == "__main__":
    unittest.main()
