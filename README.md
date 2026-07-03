# OmniStem Studio

OmniStem Studio is a Windows-first, clean-room AI digital audio workstation foundation for stem separation, spectral editing, note objects, audio-to-MIDI, restoration, mastering, plugin workflows, OmniScript automation, and AI-assisted editing.

> This repository contains working native, DSP, persistence, MIDI, scripting, and model-adapter code. It is not yet a finished commercial replacement for RipX. See the [implementation status](docs/IMPLEMENTATION_STATUS.md) for the exact verified, partial, and missing capabilities.

## Verified implementation

- C++20 core with stems, pitch curves, note objects, spectral masks, processing graphs, reversible note-gain commands, MIDI export, SQLite project persistence, and cancellable background jobs
- Versioned SQLite persistence that round-trips project-local IDs, stem roles, gain envelopes, spectral masks, processor chains, and processor parameters
- Optional JUCE desktop target with native window, audio decoding, waveform display, transport playback, audio-device selection, WASAPI, optional ASIO compilation, editor workspaces, and plugin scanning/management
- Python JSON-lines worker with runtime detection, Demucs execution, Basic Pitch execution, real multi-model stem fusion, built-in spectral-mask DSP, basic restoration/mastering, jobs, cancellation, and deterministic edit planning
- Restricted OmniScript validator and spawn-isolated, time-limited subprocess runtime that emits reversible command transactions
- Native, Python, optional-DSP, Windows, and JUCE build/test jobs in GitHub Actions

## Important capability boundaries

- Note objects and MIDI export are real; polyphonic note-level audio resynthesis is not implemented.
- Spectral-mask rendering is real; the desktop spectrogram tile renderer and correction brush are not complete.
- Plugin scanning and management are real; plugins are not yet inserted into a latency-compensated project signal graph.
- Replacement planning is implemented; actual vocal or instrument replacement audio is not implemented.
- Restoration and mastering execute real DSP, but they are validation-grade rather than a complete neural restoration/mastering suite.
- The AI assistant is currently a deterministic planner, not an autonomous LLM agent.

## Repository layout

```text
apps/desktop/                 Native CLI and optional JUCE DAW shell
packages/core/                Project model, jobs, persistence, MIDI, edit graph
services/ai-worker/           AI/DSP worker and OmniScript runtime
services/ai-worker/tests/     Protocol, scripting, jobs, DSP, and ensemble tests
scripts/examples/             OmniScript examples
docs/                         Architecture, feature, and implementation status
.github/workflows/            Linux, Windows, Python, DSP, and JUCE CI
```

## Build and test the native core

```powershell
cmake -S . -B build -DOMNISTEM_BUILD_JUCE_APP=OFF
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\apps\desktop\Release\OmniStemCLI.exe
```

On single-configuration generators, omit `-C Release`; the CLI is normally `build/apps/desktop/OmniStemCLI`.

## Build the JUCE Windows application

Clone JUCE separately, then point CMake at it:

```powershell
git clone https://github.com/juce-framework/JUCE.git external/JUCE
cmake -S . -B build-juce `
  -DOMNISTEM_BUILD_JUCE_APP=ON `
  -DOMNISTEM_ENABLE_ASIO=OFF `
  -DOMNISTEM_JUCE_SOURCE_DIR="$PWD/external/JUCE"
cmake --build build-juce --config Release --target OmniStemStudio
```

WASAPI is enabled by default in the JUCE target. ASIO is opt-in with `-DOMNISTEM_ENABLE_ASIO=ON` and requires a compatible SDK and an appropriate licensing review before distribution.

## Run and test the AI worker

The base protocol and OmniScript runtime require Python 3.11+:

```powershell
python -m compileall services/ai-worker
python -m unittest discover -s services/ai-worker/tests -v
python services/ai-worker/main.py
```

Install optional local DSP/model integrations:

```powershell
py -3.11 -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r services/ai-worker/requirements-ai.txt
```

PyTorch and Torchaudio should be installed using the package source appropriate for the machine's CPU or CUDA configuration.

### Protocol examples

```json
{"id":"1","method":"health.check","params":{}}
{"id":"2","method":"model.list","params":{}}
{"id":"3","method":"separation.run","params":{"source":"C:/audio/song.wav","quality":"ensemble","models":["htdemucs","htdemucs_ft"]}}
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

OmniScript produces a validated transaction. The desktop application must apply the transaction through its undo system after user approval.

## Engineering boundaries

- The real-time audio callback must never run Python, model inference, disk I/O, plugin scanning, allocation-heavy analysis, or blocking locks.
- Source audio is immutable; edits are commands, masks, note transformations, automation, or processor-graph changes.
- Voice or instrument replacement requires consent, provenance, licensing, and anti-impersonation controls.
- OmniScript is deliberately narrow and still requires an OS-level capability sandbox before untrusted public scripts can be accepted.
- This project must not copy RipX source code, protected UI assets, private formats, or proprietary algorithms.

See [Architecture](docs/ARCHITECTURE.md), [Feature Map](docs/FEATURES.md), and [Implementation Status](docs/IMPLEMENTATION_STATUS.md).

## License

No open-source license has been selected. All rights are reserved until the repository owner adds one.
