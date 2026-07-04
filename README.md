# OmniStem Studio

> **Canonical product repository:** all new OmniStem Studio desktop, DAW, worker, model-adapter, persistence, packaging, and release development belongs here. The separate `krishavi85/OmniStem` repository is now a legacy Python orchestration MVP. See [Repository Consolidation](docs/REPOSITORY_MIGRATION.md).

OmniStem Studio is a Windows-first, clean-room AI digital audio workstation foundation for stem separation, spectral editing, note objects, audio-to-MIDI, restoration, mastering, plugin workflows, OmniScript automation, and AI-assisted editing.

> This repository contains working native, DSP, persistence, MIDI, scripting, model-adapter, and worker-integration code. It is not yet a finished commercial replacement for RipX. See the [implementation status](docs/IMPLEMENTATION_STATUS.md) for the exact verified, partial, and missing capabilities.

## Verified implementation

- C++20 core with stems, pitch curves, note objects, spectral masks, processing graphs, reversible note-gain commands, MIDI export, SQLite project persistence, and cancellable background jobs
- Versioned SQLite persistence that round-trips project-local IDs, stem roles, gain envelopes, spectral masks, processor chains, and processor parameters
- Optional JUCE desktop target with native window, audio decoding, waveform display, transport playback, audio-device selection, WASAPI, optional ASIO compilation, editor workspaces, and plugin scanning/management
- Python worker with runtime detection, background jobs, cancellation, Demucs, Basic Pitch, spectral DSP, restoration, mastering, neural-model adapters, consent controls, and signed-update tooling
- Unified capability metadata and validated command construction for Audio Separator, Demucs, Spleeter, and Open-Unmix
- One-request execution and desktop worker-daemon entry points for native frontend integration
- Restricted OmniScript validator and time-limited transaction runtime
- Native, Python, optional-DSP, Windows, and JUCE build/test jobs in GitHub Actions

## Important capability boundaries

- Note objects, MIDI export, harmonic resynthesis, and model adapters are real; perfect same-harmonic source isolation depends on compatible trained weights and is not guaranteed.
- Spectrogram tiles and correction operations are real; the complete interactive JUCE correction editor remains product work.
- Plugin scanning, state models, routing, latency calculations, and crash supervision are real; full out-of-process plugin execution and editor remoting remain incomplete.
- Authorized voice transformation requires a configured provider, exact model artifact, and valid signed owner consent.
- The AI assistant remains a deterministic planner rather than a credentialed autonomous editing agent.
- The native AI Tools request panel still requires final JUCE-side activation and end-to-end verification.

## Repository layout

```text
apps/desktop/                 Native CLI and optional JUCE DAW shell
packages/core/                Project model, jobs, persistence, MIDI, edit graph
services/ai-worker/           AI/DSP worker, adapters, daemon, and OmniScript runtime
services/ai-worker/tests/     Protocol, integration, scripting, jobs, DSP, and ensemble tests
tools/                        Model and signed-update tooling
protocols/                    Native host and sandbox contracts
docs/                         Architecture, feature, migration, and implementation status
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

The source is verified against JUCE 8.0.8:

```powershell
git clone --branch 8.0.8 --depth 1 https://github.com/juce-framework/JUCE.git external/JUCE
cmake -S . -B build-juce `
  -DOMNISTEM_BUILD_JUCE_APP=ON `
  -DOMNISTEM_ENABLE_ASIO=OFF `
  -DOMNISTEM_JUCE_SOURCE_DIR="$PWD/external/JUCE"
cmake --build build-juce --config Release --target OmniStemStudio --parallel 2
```

WASAPI is enabled by default. ASIO remains opt-in and requires a compatible SDK and licensing review before distribution.

## Run and test the worker

The base protocol and OmniScript runtime require Python 3.11+:

```powershell
python -m compileall services/ai-worker
python -m unittest discover -s services/ai-worker/tests -v
python services/ai-worker/main.py
```

Install production DSP/model dependencies:

```powershell
py -3.11 -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r services/ai-worker/requirements-production.txt
```

PyTorch and Torchaudio should be installed from the package source appropriate for the machine's CPU or CUDA configuration.

## Unified engine adapters

The primary worker now defines capabilities and command validation for:

- Audio Separator
- Demucs
- Spleeter
- Open-Unmix

Example worker requests:

```json
{"id":"1","method":"engine.list","params":{}}
{"id":"2","method":"engine.separate","params":{"source":"C:/audio/song.wav","engine":"demucs","model":"htdemucs_ft","stems":["vocals","instrumental"],"outputFormat":"wav"}}
{"id":"3","method":"transcription.run","params":{"source":"C:/audio/vocals.wav"}}
{"id":"4","method":"spectral.tiles","params":{"source":"C:/audio/song.wav","png":true}}
```

Long operations return a `jobId`. The JSON-lines worker supports `job.status` and `job.cancel`. The one-request runner waits for a submitted job to reach a terminal state before returning its final response.

## Desktop worker integration

The desktop integration backend consists of:

- `services/ai-worker/request_once.py`
- `services/ai-worker/desktop_worker_daemon.py`
- `apps/desktop/src/WorkerBridge.h`
- `apps/desktop/src/WorkerQueueBridge.h`

The user-facing JUCE AI Tools panel remains gated until the native request-submission code is accepted and verified by CI. Do not represent this panel as complete yet.

## Engineering boundaries

- The real-time audio callback must never run Python, model inference, disk I/O, plugin scanning, allocation-heavy analysis, or blocking locks.
- Source audio is immutable; edits are commands, masks, note transformations, automation, or processor-graph changes.
- Voice or instrument replacement requires consent, provenance, licensing, and anti-impersonation controls.
- Model bundles require reviewed licenses and exact checksums.
- This project must not copy RipX source code, protected UI assets, private formats, or proprietary algorithms.

See [Architecture](docs/ARCHITECTURE.md), [Feature Map](docs/FEATURES.md), [Implementation Status](docs/IMPLEMENTATION_STATUS.md), and [Repository Consolidation](docs/REPOSITORY_MIGRATION.md).

## License

No open-source license has been selected. All rights are reserved until the repository owner adds one.
