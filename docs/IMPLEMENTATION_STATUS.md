# Implementation and Verification Status

This document supersedes the capability summary introduced by commit `5008c78`. Git commits are immutable; corrections are made by later commits such as this one.

## Verified by automated tests

The repository currently verifies:

- Linux and Windows C++20 configuration, compilation, regression tests, and CLI execution
- SQLite project persistence with project-local IDs, stems, notes, gain envelopes, masks, processor chains, and parameters
- Reversible edit commands, MIDI generation, and cancellable background jobs
- Tempo maps, automation interpolation, take comping, acyclic routing, route latency, and output compensation
- Plugin state containers and plugin-chain latency accounting
- Worker heartbeat, restart-budget, crash, and quarantine states
- Python worker protocol, job cancellation, OmniScript restrictions, and reversible transactions
- STFT spectral masking, spectrogram tiles, PNG export, and brush-operation validation
- Harmonic note resynthesis
- Restoration and mastering DSP
- LUFS, RMS, sample-peak, and four-times oversampled true-peak analysis
- Synthetic multi-model stem fusion including polarity correction
- Signed update-manifest verification and tamper detection when the optional cryptography dependency is installed

## Implemented model-dependent systems

These paths are executable but require compatible runtimes, model artifacts, and hardware:

### Stem separation

The worker runs Demucs models, discovers stems, reports missing stems, and supports multi-model ensemble fusion with compatibility checks and polarity alignment.

### Audio-to-MIDI

The Basic Pitch adapter requests MIDI, note-event CSV, and raw model outputs.

### Neural note resynthesis

The note-conditioned ONNX adapter processes long audio in overlapping blocks and accepts MIDI pitch, note range, gain, and pitch-shift conditions. Output quality depends on the installed trained model. Perfect isolation is not guaranteed when sources share the same harmonics.

### Neural restoration

The ONNX restoration adapter supports denoise, dereverb, and declipping manifests with CPU, DirectML, or CUDA provider selection where available. Reviewed model weights are not silently redistributed.

### Authorized voice transformation

Voice transformation is provider-isolated and requires an Ed25519-signed owner-consent document bound to the exact voice ID, model digest, purpose, and validity period. A provenance sidecar records source, output, model, consent, owner, and provider hashes.

### Instrument rendering

MIDI and SoundFont rendering is available through FluidSynth when it is installed.

## Native DAW systems

Implemented:

- Tempo and automation data models
- Take and comp-region selection
- Routing DAG and latency calculations
- Plugin state and latency containers
- Process heartbeat, restart, and quarantine supervision
- JUCE audio import, waveform display, transport, WASAPI device selection, editor tabs, and plugin scanning

The reproducible JUCE build is pinned to **JUCE 8.0.8**. JUCE 8.0.9 removed `AudioPluginFormatManager::addDefaultFormats()`, so using an unpinned `master` checkout breaks this source version.

## Packaging and model integrity

Implemented:

- CPack/NSIS package configuration and deterministic package names
- Ed25519-signed update manifests
- SHA-256 artifact verification
- Model-bundle lock files with task, license, filename, and exact SHA-256 validation
- Protocol contracts for an isolated plugin host and Windows AppContainer sandbox

## Remaining product work

The following are not represented as complete:

- An out-of-process VST3/CLAP host with shared-memory real-time audio transport
- Remote plugin editor windows and full plugin-state restoration in projects
- A complete interactive JUCE spectrogram tile and correction-brush editor
- Fully integrated asynchronous multichannel recording, punch-in/out, and crash recovery
- A native Windows AppContainer launcher for OmniScript
- A credentialed autonomous LLM executor with single-use approvals, tamper-evident audit logs, and rollback
- Bundled production model weights whose licenses and redistribution terms have been reviewed
- Commercial-quality listening benchmarks proving perfect same-harmonic note isolation

## Verification commands

### Native core

```powershell
cmake -S . -B build -DOMNISTEM_BUILD_JUCE_APP=OFF
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

### Python worker

```powershell
python -m compileall services/ai-worker
python -m unittest discover -s services/ai-worker/tests -v
```

### Production DSP

```powershell
pip install -r services/ai-worker/requirements-production.txt
python -m unittest discover -s services/ai-worker/tests -v
```

### JUCE Windows application

```powershell
git clone --branch 8.0.8 --depth 1 https://github.com/juce-framework/JUCE.git external/JUCE
cmake -S . -B build-juce `
  -DOMNISTEM_BUILD_JUCE_APP=ON `
  -DOMNISTEM_ENABLE_ASIO=OFF `
  -DOMNISTEM_JUCE_SOURCE_DIR="$PWD/external/JUCE"
cmake --build build-juce --config Release --target OmniStemStudio --parallel 2
```

Enable ASIO only after a compatible SDK is available and its licensing requirements have been reviewed.
