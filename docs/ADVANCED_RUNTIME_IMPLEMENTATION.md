# Advanced Runtime Implementation

Branch: `feature/advanced-runtime-models`

## Implemented

### Learned note-conditioned resynthesis

`services/ai-worker/neural_note_engine.py` loads a compatible ONNX model, validates its manifest, processes long audio in overlapping chunks, sends a selected-note condition vector to the model, reconstructs the output with overlap weighting, and applies optional mixture-consistency correction.

The engine supports MIDI pitch, note start/end, gain, and pitch-shift conditions. Model quality depends on the supplied trained model. The repository does not describe imperfect model weights as perfect isolation.

### Authorized voice transformation

`services/ai-worker/voice_consent.py` verifies Ed25519-signed consent documents, validity periods, voice identifiers, and the exact model SHA-256 digest.

`services/ai-worker/authorized_voice_provider.py` invokes only a configured external provider after consent verification and writes a provenance sidecar containing source, output, model, consent, owner, purpose, and provider hashes.

### Verified model bundles

`tools/model_bundle.py` builds offline model bundles from a lock file. Every model requires:

- an approved license identifier
- a supported task
- an exact SHA-256 digest
- a fixed filename
- a source location

Partial downloads are rejected and invalid checksums are deleted. The resulting bundle includes a machine-readable manifest.

### Signed update artifacts

`tools/sign_release.py` reads the Ed25519 signing key from a secret file referenced by `OMNISTEM_UPDATE_SIGNING_KEY_FILE`; it does not accept key material on the command line.

`tools/update_verifier.py` verifies the manifest signature and installer SHA-256 digest. CPack now installs the application binaries and produces deterministic package names.

### Worker API

The worker exposes:

- `note.resynthesize.neural`
- `voice.transform.authorized`
- PNG-backed spectrogram tiles
- existing ONNX restoration and deterministic DSP methods

### Protocol contracts

- `protocols/plugin-host-protocol.json`
- `protocols/omnscript-sandbox-policy.json`

These freeze the transport, state, editor-window, heartbeat, AppContainer, resource, and fail-closed requirements for the native hosts.

## Not represented as finished

The following require privileged native process-launch code that the repository write safety controls rejected in this session:

- the Windows AppContainer launcher executable
- the out-of-process VST3/CLAP host executable
- shared-memory real-time plugin audio transport
- native plugin editor-window remoting
- asynchronous multichannel disk writer connected to the JUCE callback
- the updater process that applies and rolls back an installer
- the credentialed autonomous action executor

The protocol and security foundations for these systems are present, but this document does not label them complete.

## Model weights

No third-party model weight is silently redistributed. A release model bundle must be generated from a reviewed lock file with exact checksums and compatible licenses. This avoids shipping unknown, changed, or commercially restricted artifacts.

## CI status

Native Linux, native Windows, and production-audio jobs pass on the advanced branch. The lightweight Python job currently fails because the signed-update test requires the optional `cryptography` dependency. Attempts to add the dependency gate were rejected by repository write safety controls. The PR remains draft until this is resolved.
