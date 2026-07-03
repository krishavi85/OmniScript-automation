# OmniStem Studio

OmniStem Studio is a Windows-first, clean-room AI digital audio workstation foundation for stem separation, spectral editing, note-level audio manipulation, audio-to-MIDI, vocal/instrument replacement, restoration, mastering, plugin hosting, OmniScript automation, and AI-assisted editing.

> This repository now contains executable architecture and adapters. It is not yet a finished commercial replacement for RipX: model quality, production rendering, UX polish, plugin sandboxing, installers, and extensive testing still require sustained engineering and audio research.

## What is implemented

- C++20 domain core with stems, pitch curves, note objects, spectral masks, processing graphs, undoable note-gain commands, MIDI export, SQLite project persistence, and cancellable background jobs
- Optional JUCE desktop target with native window, timeline/spectral/note workspaces, file decoding, audio transport, ASIO/WASAPI definitions, and plugin-format initialization
- Python JSON-lines worker with model discovery, Demucs separation adapter, Basic Pitch transcription adapter, built-in spectral-mask DSP, restoration, mastering, replacement planning, jobs, cancellation, and AI edit planning
- Restricted OmniScript validator and time-limited subprocess runtime that returns reversible transaction commands
- Windows/Linux-compatible headless build, documentation, examples, and CI smoke tests

## Repository layout

```text
apps/desktop/                 Native CLI and optional JUCE DAW shell
packages/core/                Project model, jobs, persistence, MIDI, edit graph
services/ai-worker/           AI/DSP worker and OmniScript runtime
scripts/examples/             OmniScript examples
docs/                         Architecture and feature specifications
.github/workflows/            Native/Python CI
```

## Build the core and CLI

```powershell
cmake -S . -B build -DOMNISTEM_BUILD_JUCE_APP=OFF
cmake --build build --config Release
.\build\apps\desktop\Release\OmniStemCLI.exe
```

On single-config generators, the executable is normally `build/apps/desktop/OmniStemCLI`.

## Build the JUCE Windows application

Clone JUCE separately, then point CMake at it:

```powershell
git clone https://github.com/juce-framework/JUCE.git external/JUCE
cmake -S . -B build-juce `
  -DOMNISTEM_BUILD_JUCE_APP=ON `
  -DOMNISTEM_JUCE_SOURCE_DIR="$PWD/external/JUCE"
cmake --build build-juce --config Release
```

For ASIO distribution, review the current Steinberg ASIO SDK licensing and JUCE configuration applicable to your product before shipping binaries.

## Run the AI worker

The base worker requires only Python 3.11+:

```powershell
python services/ai-worker/main.py
```

Install optional local DSP/model integrations:

```powershell
py -3.11 -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r services/ai-worker/requirements-ai.txt
```

PyTorch/Torchaudio should be installed using the package source appropriate for the machine's CPU or CUDA configuration.

### Protocol examples

```json
{"id":"1","method":"health.check","params":{}}
{"id":"2","method":"model.list","params":{}}
{"id":"3","method":"separation.run","params":{"source":"C:/audio/song.wav","quality":"studio","model":"htdemucs"}}
{"id":"4","method":"transcription.run","params":{"source":"C:/audio/vocals.wav"}}
{"id":"5","method":"assistant.plan","params":{"instruction":"Separate vocals, clean noise, transcribe the bass and prepare a master"}}
```

Long operations return a `jobId`. Query or cancel them with `job.status` and `job.cancel`.

## OmniScript example

```python
for note in notes():
    if note["confidence"] < 0.55:
        mute_note(note["id"])

for stem in stems():
    if stem["role"] == "lead-vocal":
        request_transcription(stem["id"])
```

OmniScript produces a transaction of validated commands. The desktop application must apply that transaction through its undo system after user approval.

## Engineering boundaries

- The real-time audio callback must never run Python, model inference, disk I/O, plugin scanning, allocation-heavy analysis, or blocking locks.
- Source audio is immutable; edits are commands, masks, note transformations, automation, or processor graph changes.
- User voice/instrument replacement requires consent, provenance, licensing, and anti-impersonation controls.
- A Python restriction layer alone is not sufficient for executing arbitrary hostile code. OmniScript intentionally exposes a narrow command language and should later be hardened with an OS sandbox and capability broker.
- This project must not copy RipX source code, protected UI assets, private formats, or proprietary algorithms.

See [Architecture](docs/ARCHITECTURE.md) and [Feature Map](docs/FEATURES.md).

## License

No open-source license has been selected. All rights are reserved until the repository owner adds one.
