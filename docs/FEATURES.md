# OmniStem Studio Feature Map

## Stem separation

The worker exposes Demucs-family model discovery, separation planning, cancellable background execution, model selection, quality modes, and output manifests. Ensemble fusion, phase alignment, artifact scoring, and specialist models are represented as pipeline stages and are the next DSP/model implementation layer.

## Spectral editing

Spectral corrections are stored as non-destructive mask operations. The built-in worker can render a time/frequency gain mask through overlap-add STFT processing. The JUCE shell contains a dedicated spectral canvas ready for tiled spectrogram rendering and correction-brush interaction.

## Note-level audio editing and MIDI

The core defines stable note objects with pitch curves, gain, pan, formants, confidence, envelopes, and reversible commands. The native MIDI writer exports note objects to a standards-compatible MIDI file. The Basic Pitch adapter submits audio-to-MIDI jobs.

## Vocal and instrument replacement

Replacement is modeled as a processing node and AI plan that transfers timing, pitch bends, velocity, articulation, and expression to a licensed target instrument, plugin, or voice model. Actual synthesis remains provider/plugin dependent.

## Restoration and mastering

The worker includes executable local DSP pipelines for DC removal, adaptive gating, saturation, and peak normalization, plus extensible processing graph definitions for denoise, declick, dereverb, EQ, dynamics, width, and true-peak limiting.

## Plugin hosting

The JUCE application initializes audio plugin formats and the known-plugin registry. Subsequent UI work will add scanning, blacklist management, plugin windows, routing, delay compensation, state serialization, VST3 hosting, and CLAP through a dedicated adapter.

## Embedded OmniScript

OmniScript is a restricted Python-like transaction language. AST validation blocks imports, attributes, private names, and unapproved calls. Execution occurs in a separate process with a time limit and produces reversible commands rather than directly mutating project files. It is a constrained automation runtime, not a security boundary for arbitrary hostile native code.

## AI-assisted editing

`assistant.plan` converts a natural-language editing request into a non-destructive action plan. A future local or remote LLM adapter can replace the deterministic planner while preserving the same validated command schema and confirmation gate.

## Audio engine and Windows integration

The optional JUCE target provides a native GUI, audio importing and decoding, transport playback, ASIO/WASAPI configuration, timeline, spectral editor, note editor, and plugin-format management. The core target remains independently buildable for tests and headless workflows.

## Persistence and jobs

SQLite persistence stores projects, stems, note objects, and spectral masks. C++ and Python job managers provide queues, progress, cancellation, state reporting, and failure isolation.
