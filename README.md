# OmniStem Studio

> **Canonical product repository:** all new OmniStem Studio desktop, DAW, worker, model-adapter, persistence, packaging, and release development belongs here. The separate `krishavi85/OmniStem` repository is a legacy Python orchestration MVP. See [Repository Consolidation](docs/REPOSITORY_MIGRATION.md).

OmniStem Studio is a Windows-first, clean-room AI digital audio workstation foundation for stem separation, spectral editing, note objects, audio-to-MIDI, restoration, mastering, plugin workflows, OmniScript automation, and AI-assisted editing.

> This is not yet a finished commercial RipX replacement. The exact document-to-code status is maintained in [OmniStem.docx Compliance](docs/OMNISTEM_DOCX_COMPLIANCE.md).

## Verified implementation

- C++20 project, stem, note, pitch, spectral-mask, processing, MIDI, routing, latency, job, and SQLite persistence systems
- JUCE Windows shell with audio import, waveform display, transport playback, audio-device selection, WASAPI, optional ASIO compilation, workspaces, and plugin scanning
- Python worker with background jobs, cancellation, Demucs, Basic Pitch, spectral DSP, restoration, mastering, ONNX adapters, consent controls, and update verification
- Unified adapters for Audio Separator, Demucs, Spleeter, and Open-Unmix
- Versioned model catalog with aliases, families, tags, versions, deprecation warnings, filters, sorting, and exact SHA-256 artifact verification
- Normalized stem taxonomy and alias resolution
- Standard, Comparison, Ensemble, Cascade, Auto, and God Mode planning and execution
- Polarity-aligned multi-engine fusion and God Mode consensus metrics
- Validated pipelines with dependency ordering, cycle detection, result references, cancellation, and worker execution
- WAV benchmark analysis with peak, RMS, clipping, RMSE, MAE, SNR, correlation, and ranking
- Canonical Python CLI with environment, doctor, engines, models, inspect, separate, batch, compare, ensemble, cascade, auto, God Mode, pipelines, benchmarks, configuration, checksum verification, JSON, dry run, and local serving
- Loopback-only FastAPI service with discovery, generic RPC, job status, and cancellation
- TOML configuration with validation and environment overrides
- Native, worker, production-audio, local-API, Windows, and JUCE CI jobs

## Important boundaries

- The native JUCE AI Tools panel is not yet connected to the worker. The backend modes are real, but users cannot run them from the current JUCE interface.
- The complete Dashboard, Batch, God Mode, Comparison Lab, Ensemble Builder, Pipeline Builder, Models, Engines, Audio Inspector, Benchmarks, History, Settings, and Logs workspaces remain incomplete.
- Perfect isolation of overlapping notes sharing identical harmonics is not guaranteed.
- Production neural quality depends on compatible reviewed model weights; the repository does not invent or redistribute unknown model hashes.
- Full out-of-process plugin execution, editor remoting, complete plugin-state restoration, live delay compensation, multichannel recording, AppContainer sandboxing, and credentialed autonomous editing remain incomplete.
- A signed installer/update deployment lifecycle is not yet proven end to end.

## Repository layout

```text
apps/desktop/                 Native CLI and optional JUCE DAW shell
packages/core/                Project model, jobs, persistence, MIDI, routing, edit graph
services/ai-worker/           Worker, CLI, API, modes, pipelines, catalog, DSP, and OmniScript
services/ai-worker/tests/     Protocol, CLI, API, mode, pipeline, DSP, security, and integration tests
tools/                        Model and signed-update tooling
protocols/                    Native host and sandbox contracts
docs/                         Architecture, migration, compliance, and implementation status
.github/workflows/            Linux, Windows, Python, API, DSP, and JUCE CI
```

## Build and test the native core

```powershell
cmake -S . -B build -DOMNISTEM_BUILD_JUCE_APP=OFF
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\apps\desktop\Release\OmniStemCLI.exe
```

On single-configuration generators, omit `-C Release`.

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

WASAPI is enabled by default. ASIO requires a compatible SDK and licensing review.

## Worker setup

```powershell
py -3.11 -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install --upgrade pip
pip install -r services/ai-worker/requirements-production.txt
python -m compileall services/ai-worker
python -m unittest discover -s services/ai-worker/tests -v
```

Install Demucs, Basic Pitch, Audio Separator, Spleeter, or Open-Unmix separately when those engines are required. PyTorch and Torchaudio should match the machine's CPU or CUDA configuration.

## Canonical CLI

```powershell
python services/ai-worker/studio_cli.py --version
python services/ai-worker/studio_cli.py doctor
python services/ai-worker/studio_cli.py models --engine demucs --current-only
python services/ai-worker/studio_cli.py separate C:\audio\song.wav --engine demucs --model htdemucs_ft --dry-run
python services/ai-worker/studio_cli.py compare C:\audio\song.wav --engines demucs,openunmix --models htdemucs_ft,umxhq --dry-run
python services/ai-worker/studio_cli.py pipeline pipeline.json --dry-run
```

Use `--json` before the subcommand for compact machine-readable output.

## Local API

```powershell
pip install -r services/ai-worker/requirements-api.txt
python services/ai-worker/studio_cli.py config init
python services/ai-worker/studio_cli.py serve
```

The built-in configuration accepts loopback addresses only. Primary routes are:

- `GET /health`
- `GET /methods`
- `GET /engines`
- `GET /models`
- `POST /rpc`
- `GET /jobs/{job_id}`
- `POST /jobs/{job_id}/cancel`

## Worker protocol examples

```json
{"id":"1","method":"catalog.models","params":{"engine":"demucs","includeDeprecated":false}}
{"id":"2","method":"mode.plan","params":{"mode":"god","engines":["demucs","openunmix","audio-separator"],"models":["htdemucs_ft","umxhq","UVR-MDX-NET-Inst_HQ_3.onnx"],"stems":["vocals","instrumental"]}}
{"id":"3","method":"mode.run","params":{"source":"C:/audio/song.wav","mode":"standard","engine":"demucs","model":"htdemucs_ft","stems":["vocals","instrumental"]}}
{"id":"4","method":"pipeline.validate","params":{"pipeline":{"steps":[{"id":"health","method":"health.check"}]}}}
```

Long operations return a `jobId`. Use `job.status` and `job.cancel`, or use `request_once.py` to wait for a terminal result.

## Engineering boundaries

- The real-time audio callback must never run Python, model inference, disk I/O, plugin scanning, allocation-heavy analysis, or blocking locks.
- Source audio is immutable; edits are commands, masks, note transformations, automation, or processor-graph changes.
- Voice or instrument replacement requires consent, provenance, licensing, and anti-impersonation controls.
- Model bundles require reviewed licenses and exact checksums.
- This project must not copy RipX source code, protected UI assets, private formats, or proprietary algorithms.

See [Architecture](docs/ARCHITECTURE.md), [Implementation Status](docs/IMPLEMENTATION_STATUS.md), [Repository Consolidation](docs/REPOSITORY_MIGRATION.md), and [OmniStem.docx Compliance](docs/OMNISTEM_DOCX_COMPLIANCE.md).

## License

No open-source license has been selected. All rights are reserved until the repository owner adds one.
