from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


class SandboxUnavailable(RuntimeError):
    pass


def backend() -> str:
    if sys.platform.startswith("linux") and shutil.which("bwrap"):
        return "bubblewrap"
    if sys.platform == "win32" and os.environ.get("OMNISTEM_SANDBOX_HOST"):
        host = Path(os.environ["OMNISTEM_SANDBOX_HOST"])
        if host.is_file():
            return "windows-host"
    return "unavailable"


def execute(payload: dict[str, Any], timeout_seconds: float = 3.0) -> dict[str, Any]:
    selected = backend()
    if selected == "unavailable":
        raise SandboxUnavailable("no approved OS sandbox backend is configured")
    with tempfile.TemporaryDirectory(prefix="omnistem-sandbox-") as directory:
        root = Path(directory)
        request_path = root / "request.json"
        response_path = root / "response.json"
        request_path.write_text(json.dumps(payload), encoding="utf-8")
        runner = Path(__file__).with_name("sandbox_child.py").resolve()
        if selected == "bubblewrap":
            command = [
                "bwrap", "--unshare-all", "--new-session", "--die-with-parent",
                "--ro-bind", str(Path(sys.executable).parent), str(Path(sys.executable).parent),
                "--ro-bind", str(runner.parent), str(runner.parent),
                "--bind", str(root), str(root),
                "--proc", "/proc", "--dev", "/dev", "--chdir", str(root),
                sys.executable, str(runner), str(request_path), str(response_path),
            ]
        else:
            host = Path(os.environ["OMNISTEM_SANDBOX_HOST"])
            command = [str(host), "--timeout-ms", str(int(timeout_seconds * 1000)), "--",
                       sys.executable, str(runner), str(request_path), str(response_path)]
        completed = subprocess.run(command, capture_output=True, text=True,
                                   timeout=max(1.0, timeout_seconds + 2.0), check=False)
        if completed.returncode != 0:
            raise RuntimeError(completed.stderr.strip() or "sandboxed script failed")
        if not response_path.is_file():
            raise RuntimeError("sandbox did not produce a response")
        return json.loads(response_path.read_text(encoding="utf-8"))
