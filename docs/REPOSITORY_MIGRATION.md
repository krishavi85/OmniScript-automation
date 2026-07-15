# Repository Consolidation

`krishavi85/OmniScript-automation` is the canonical OmniStem Studio product repository.

The older `krishavi85/OmniStem` repository is retained as a legacy Python orchestration MVP. New desktop, DAW, worker, model-adapter, persistence, packaging, and release work belongs in this repository.

## Migrated capabilities

The primary worker now includes capability metadata and validated command construction for:

- Audio Separator
- Demucs
- Spleeter
- Open-Unmix

The existing Demucs, Basic Pitch, restoration, mastering, spectral, note-resynthesis, instrument-rendering, and authorized voice-provider paths remain available.

A one-request worker runner keeps background jobs alive until completion. A desktop worker daemon provides the backend side of the local frontend integration boundary.

## Legacy repository policy

The legacy repository should receive only critical maintenance and migration documentation. New product features should not be added there.

## Remaining integration boundary

The native AI Tools panel still requires an accepted JUCE-side request-submission change and end-to-end verification. The backend migration is present, but the user-facing frontend activation must not be represented as complete until that native change passes CI.
