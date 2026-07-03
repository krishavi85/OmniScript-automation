# OmniStem Studio Architecture

## 1. Desktop process

The native desktop process owns the user interface, transport, real-time audio graph, project state, undo history, plugin hosting, and communication with the AI worker. The audio callback must never block on disk, network, Python, model inference, allocation, or locks.

## 2. AI worker

The Python worker runs outside the audio process. It receives versioned requests over a local IPC channel and returns progress events, artifacts, confidence maps, note events, and structured errors.

Initial methods:

- `health.check`
- `separation.plan`
- `separation.run`
- `transcription.plan`
- `transcription.run`
- `spectral.repair`
- `model.list`

## 3. Ensemble separation

The model router selects one or more backends according to source type, requested stems, hardware, duration, and quality mode. Production stages are decoding, loudness normalization, segmentation, inference, overlap-add reconstruction, phase alignment, model fusion, artifact scoring, targeted repair, and lossless export.

## 4. Non-destructive project model

Original source audio is immutable. Every edit is a reversible command referencing time ranges, spectral masks, note-event IDs, or automation lanes.

A note event contains:

- stable UUID
- stem ID
- source time range
- pitch curve
- gain envelope
- pan envelope
- formant and timbre controls
- confidence values
- spectral-mask references
- provenance and model version

## 5. Spectral correction

Correction strokes do not destructively paint audio. Each stroke creates a mask operation with source and destination stem IDs, frequency/time bounds, strength, feathering, and provenance. Rendering applies the mask graph to cached analysis tiles.

## 6. OmniScript

OmniScript begins as sandboxed Python-compatible automation with an application-specific API. Scripts operate through commands and transactions so every mutation is undoable. File, network, microphone, model, plugin, and process access require explicit permissions.

Example API namespaces:

- `project`
- `selection`
- `notes`
- `stems`
- `spectral`
- `midi`
- `render`
- `ai`
- `ui`

## 7. Windows deployment

Target Windows 10/11 x64 initially, with ASIO and WASAPI support. Package the desktop executable, worker runtime, signed model manifests, and optional model packs separately. GPU execution should support CUDA first and DirectML as a fallback.

## 8. Delivery milestones

### Milestone A — foundation

Native shell, waveform playback, project database, worker IPC, jobs, logs, crash recovery, and automated tests.

### Milestone B — separation editor

Model manager, four/six-stem separation, stem mixer, spectrogram tiles, correction brush, exports, and comparison listening.

### Milestone C — note objects

Multipitch transcription, note graph, piano-roll editor, pitch/time/gain edits, MIDI export, confidence overlays, and reversible rendering.

### Milestone D — OmniScript

Sandbox, editor, autocomplete, transaction API, permissions, debugger, examples, and package manifests.

### Milestone E — professional DAW integration

Recording, automation, VST3/CLAP hosting, advanced routing, tempo map, plugin delay compensation, freeze, and external control surfaces.
