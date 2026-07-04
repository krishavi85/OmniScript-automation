from __future__ import annotations

import shutil
import subprocess
from pathlib import Path
from typing import Any


def render_instrument(midi: Path, soundfont: Path, output: Path,
                      sample_rate: int = 48000) -> dict[str, Any]:
    executable = shutil.which("fluidsynth")
    if executable is None:
        raise RuntimeError("FluidSynth is not installed")
    if not midi.is_file() or not soundfont.is_file():
        raise FileNotFoundError("MIDI or SoundFont file does not exist")
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [executable, "-ni", str(soundfont), str(midi),
               "-F", str(output), "-r", str(int(sample_rate))]
    completed = subprocess.run(command, capture_output=True, text=True, check=False)
    if completed.returncode != 0 or not output.is_file():
        raise RuntimeError(completed.stderr.strip() or "instrument rendering failed")
    return {"output": str(output), "renderer": "fluidsynth", "sampleRate": int(sample_rate)}
