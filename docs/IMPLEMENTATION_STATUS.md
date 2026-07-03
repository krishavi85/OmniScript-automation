# Implementation and Verification Status

This document separates working code from adapters that require optional models and from product features that remain incomplete. It should be updated whenever a subsystem changes.

## Verified locally

The following checks were run against the checked-in source structure and passed:

- CMake configuration and C++20 compilation without the JUCE target
- Native regression tests through CTest
- SQLite save/load round-trip for projects, project-local IDs, stem roles, notes, gain envelopes, spectral masks, processor chains, and processor parameters
- Reversible note-gain command apply/undo
- Standard MIDI file creation and header validation
- Native background job completion and progress reporting
- Python bytecode compilation
- AI-worker protocol and validation tests
- OmniScript import blocking and transaction generation
- Restoration rendering on generated audio using NumPy and SoundFile
- Mastering rendering on generated audio using NumPy and SoundFile
- Spectral-mask STFT rendering on generated audio
- Synthetic two-model ensemble fusion, including inverted-polarity correction

## Implemented, but dependent on optional components

### Demucs separation

The worker launches installed Demucs models, discovers rendered stems, reports missing requested stems, and supports a real multi-model ensemble path. Ensemble mode runs multiple models, checks sample-rate/channel compatibility, corrects whole-file polarity inversions, and averages sample-aligned results.

A full music separation job still requires installed Demucs model weights and suitable hardware. The repository does not redistribute model weights.

### Basic Pitch transcription

The worker supports Basic Pitch through its command-line interface and a programmatic fallback. It requests MIDI, note-event CSV, and raw model-output NPZ files.

Actual inference requires the optional Basic Pitch runtime and its compatible model dependencies.

### JUCE Windows application

The JUCE target contains a native window, audio import, waveform thumbnail, transport playback, audio-device selector, WASAPI support, optional ASIO compilation, editor workspaces, and plugin scanning/management.

The JUCE application is covered by a Windows build job in GitHub Actions. It is not covered by an automated audio-device playback test because CI runners do not provide a representative professional audio interface.

## Partial implementations

### Spectral editor UI

The worker performs actual time-frequency mask rendering. The desktop spectral tab is currently a workspace shell; it does not yet draw cached FFT tiles or provide a completed correction brush.

### Note-level audio editing

The native model, undoable edits, pitch curves, confidence data, persistence, and MIDI export are implemented. High-quality reconstruction of one edited note inside overlapping polyphonic audio is not implemented.

### Plugin hosting

Plugin discovery and management are implemented through JUCE. Plugins are not yet instantiated in a project signal graph, shown in dedicated editor windows, latency compensated, sandboxed, or persisted as complete plugin states.

### Vocal and instrument replacement

Replacement planning and processing-graph nodes exist. Actual neural voice conversion, sample-library rendering, or plugin-driven instrument replacement is not implemented. This remains `plan-only` in the worker response.

### Restoration and mastering

The included DSP is executable and useful for validation, but it is deliberately basic: DC removal, adaptive gating, peak protection, soft saturation, and peak normalization. Neural denoise, dereverb, declipping, true-peak analysis, LUFS targeting, and professional mastering decisions remain future work.

### AI-assisted editing

The current assistant is a deterministic instruction classifier that produces a validated non-destructive action plan. It is not yet an LLM agent and does not autonomously execute destructive edits.

### OmniScript security

OmniScript validates a restricted AST, blocks imports and attribute access, runs in a spawn-isolated subprocess, has a time limit, and emits command transactions. It is not an OS-grade sandbox for arbitrary hostile code. A production release still needs a capability broker, restricted token, process resource limits, and operating-system sandboxing.

## Not yet implemented

- Complete multitrack mixer and bus-routing graph
- Plugin processing in the live audio graph
- Plugin delay compensation and crash isolation
- GPU-rendered spectrogram tiles and interactive mask brush
- Polyphonic note-isolation resynthesis
- Production voice/instrument replacement
- Recording, comping, automation lanes, tempo mapping, and freeze/bounce workflows
- Signed Windows installer, updater, and model-pack manager
- Crash recovery, autosave, telemetry consent, and project repair tools
- Full accessibility and localization pass
- Commercial-quality listening tests and benchmark corpus

## Verification commands

### Native core

```powershell
cmake -S . -B build -DOMNISTEM_BUILD_JUCE_APP=OFF
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

On Linux or another single-configuration generator, omit `-C Release`.

### Python worker

```powershell
python -m compileall services/ai-worker
python -m unittest discover -s services/ai-worker/tests -v
```

### Optional DSP tests

```powershell
pip install "numpy>=1.26,<3" "soundfile>=0.12,<1"
python -m unittest discover -s services/ai-worker/tests -v
```

### JUCE application

```powershell
git clone https://github.com/juce-framework/JUCE.git external/JUCE
cmake -S . -B build-juce `
  -DOMNISTEM_BUILD_JUCE_APP=ON `
  -DOMNISTEM_ENABLE_ASIO=OFF `
  -DOMNISTEM_JUCE_SOURCE_DIR="$PWD/external/JUCE"
cmake --build build-juce --config Release --target OmniStemStudio
```

Enable ASIO only after a compatible SDK is available and its licensing requirements have been reviewed.
