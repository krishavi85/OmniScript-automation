# Production DAW Branch Status

Branch: `feature/production-daw-systems`

## Implemented in this branch

- Piecewise tempo maps with beat/second conversion
- Linear automation lanes
- Take registration and non-overlapping comp-region selection
- Acyclic routing graphs
- Accumulated route latency and output compensation calculations
- Plugin instance state containers and serial latency accounting
- Worker heartbeat monitoring, restart budgets, and quarantine state
- Spectrogram analysis stored as tiled NumPy arrays
- Optional grayscale PNG export for spectrogram tiles
- Validated, non-destructive spectral brush-operation files
- Harmonic soft-mask note attenuation and pitch relocation
- Integrated LUFS, sample peak, RMS, and four-times oversampled true-peak analysis
- Chunked ONNX providers for denoise, dereverb, and declipping models
- MIDI-to-audio instrument rendering through FluidSynth and SoundFonts
- NSIS/CPack package metadata
- Ed25519 update-manifest and SHA-256 artifact verification
- Linux, Windows, Python, production-DSP, and JUCE CI coverage

## Existing systems retained

- Demucs separation and multi-model fusion
- Basic Pitch transcription
- SQLite project persistence
- MIDI export
- JUCE audio import, waveform view, transport, device selection, and plugin discovery
- Restricted OmniScript AST and timed subprocess execution

## Technical limits that remain

### Polyphonic note editing

The new harmonic resynthesis endpoint performs real soft-mask editing. It cannot guarantee perfect isolation when another instrument shares the selected note's harmonics. A learned multi-source note representation and resynthesis model is still required for RipX-class isolation.

### Plugin hosting

Routing, state, latency, and crash-supervision foundations are implemented. Third-party plugin binaries are not loaded by this branch because safe hosting requires a dedicated process executable, shared-memory audio transport, editor remoting, and platform-specific hardening.

### Vocal replacement

Instrument rendering is implemented. Vocal replacement is deliberately not enabled without a consent, provenance, anti-impersonation, and provider-licensing system.

### Restoration models

The ONNX runtime is implemented, but model weights are not redistributed. A compatible manifest and model are required.

### Windows sandbox

The existing OmniScript language restrictions remain active. A complete Windows AppContainer or equivalent sandbox helper is not included in this branch.

### Autonomous editing agent

The deterministic assistant planner remains active. A provider-backed autonomous executor is not included because it requires explicit credential storage, tool authorization, audit logging, and rollback controls.

## Merge policy

Keep this pull request in draft state until all CI jobs pass. Do not merge by disabling failing tests.
