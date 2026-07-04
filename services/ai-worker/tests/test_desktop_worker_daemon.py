from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

WORKER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(WORKER_DIR))

from desktop_worker_daemon import process_one


class DesktopWorkerDaemonTests(unittest.TestCase):
    def test_health_request_round_trip(self) -> None:
        root = Path(tempfile.mkdtemp(prefix="omnistem-daemon-test-"))
        inbox = root / "inbox"
        outbox = root / "outbox"
        inbox.mkdir()
        outbox.mkdir()
        request = {
            "id": "health-test",
            "method": "health.check",
            "params": {},
        }
        (inbox / "health-test.json").write_text(json.dumps(request), encoding="utf-8")

        self.assertTrue(process_one(inbox, outbox))
        response_path = outbox / "health-test.json"
        self.assertTrue(response_path.is_file())
        response = json.loads(response_path.read_text(encoding="utf-8"))
        self.assertEqual(response["id"], "health-test")
        self.assertEqual(response["result"]["status"], "ok")

    def test_empty_queue_returns_false(self) -> None:
        root = Path(tempfile.mkdtemp(prefix="omnistem-daemon-empty-"))
        inbox = root / "inbox"
        outbox = root / "outbox"
        inbox.mkdir()
        outbox.mkdir()
        self.assertFalse(process_one(inbox, outbox))


if __name__ == "__main__":
    unittest.main()
