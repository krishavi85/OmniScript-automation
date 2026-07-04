from __future__ import annotations

from pathlib import Path
from typing import Any

import worker
from authorized_voice_provider import run_authorized_provider
from instrument_renderer import render_instrument
from mastering_pro import analyze_loudness
from neural_note_engine import render as render_neural_note
from onnx_audio import run_restoration_model
from spectral_png import export_png_tiles
from spectral_tools import generate_tiles, harmonic_note_resynthesis, save_brush_operations


def required_path(params: dict[str, Any], key: str) -> Path:
    value = str(params.get(key, "")).strip()
    if not value:
        raise worker.RpcError("INVALID_ARGUMENT", f"params.{key} is required")
    return Path(value).expanduser().resolve()


def spectral_tiles(params: dict[str, Any]) -> dict[str, Any]:
    source = required_path(params, "source")
    output = Path(params.get("outputDir") or source.parent / f"{source.stem}-spectrogram").resolve()
    if not source.is_file():
        raise worker.RpcError("FILE_NOT_FOUND", str(source))

    def operation(job: worker.Job) -> dict[str, Any]:
        job.update(progress=0.1, message="Computing spectrogram tiles")
        result = generate_tiles(
            source, output,
            n_fft=int(params.get("nFft", 2048)),
            hop=int(params.get("hop", 512)),
            tile_columns=int(params.get("tileColumns", 256)),
        )
        if bool(params.get("png", True)):
            result = export_png_tiles(result)
        return result

    return {"jobId": worker.JOBS.submit("spectrogram-tiles", operation).id}


def spectral_brush_save(params: dict[str, Any]) -> dict[str, Any]:
    operations = params.get("operations")
    if not isinstance(operations, list):
        raise worker.RpcError("INVALID_ARGUMENT", "params.operations must be an array")
    return save_brush_operations(required_path(params, "output"), operations)


def note_resynthesize(params: dict[str, Any]) -> dict[str, Any]:
    source = required_path(params, "source")
    output = required_path(params, "output")

    def operation(job: worker.Job) -> dict[str, Any]:
        job.update(progress=0.1, message="Resynthesizing selected note")
        return harmonic_note_resynthesis(
            source, output,
            start_seconds=float(params["startSeconds"]),
            end_seconds=float(params["endSeconds"]),
            fundamental_hz=float(params["fundamentalHz"]),
            gain_db=float(params.get("gainDb", -120.0)),
            pitch_shift_semitones=float(params.get("pitchShiftSemitones", 0.0)),
        )

    return {"jobId": worker.JOBS.submit("note-resynthesis", operation).id}


def note_resynthesize_neural(params: dict[str, Any]) -> dict[str, Any]:
    source = required_path(params, "source")
    output = required_path(params, "output")
    model = required_path(params, "model")
    config = required_path(params, "config")
    note = params.get("note")
    if not isinstance(note, dict):
        raise worker.RpcError("INVALID_ARGUMENT", "params.note must be an object")

    def operation(job: worker.Job) -> dict[str, Any]:
        job.update(progress=0.1, message="Running note-conditioned neural resynthesis")
        return render_neural_note(source, output, model, config, note)

    return {"jobId": worker.JOBS.submit("neural-note-resynthesis", operation).id}


def mastering_analyze(params: dict[str, Any]) -> dict[str, Any]:
    return analyze_loudness(required_path(params, "source"))


def restoration_onnx(params: dict[str, Any]) -> dict[str, Any]:
    model = required_path(params, "model")
    manifest = required_path(params, "manifest")
    source = required_path(params, "source")
    output = required_path(params, "output")

    def operation(job: worker.Job) -> dict[str, Any]:
        job.update(progress=0.1, message="Running restoration model")
        return run_restoration_model(model, manifest, source, output)

    return {"jobId": worker.JOBS.submit("onnx-restoration", operation).id}


def instrument_render(params: dict[str, Any]) -> dict[str, Any]:
    midi = required_path(params, "midi")
    soundfont = required_path(params, "soundfont")
    output = required_path(params, "output")

    def operation(job: worker.Job) -> dict[str, Any]:
        return render_instrument(midi, soundfont, output, int(params.get("sampleRate", 48000)))

    return {"jobId": worker.JOBS.submit("instrument-render", operation).id}


def voice_transform_authorized(params: dict[str, Any]) -> dict[str, Any]:
    source = required_path(params, "source")
    output = required_path(params, "output")
    provider = required_path(params, "providerManifest")
    consent = required_path(params, "consent")
    model = required_path(params, "model")
    public_key = str(params.get("consentPublicKey", "")).strip()
    voice_id = str(params.get("voiceId", "")).strip()
    if not public_key or not voice_id:
        raise worker.RpcError("INVALID_ARGUMENT", "consentPublicKey and voiceId are required")

    def operation(job: worker.Job) -> dict[str, Any]:
        job.update(progress=0.1, message="Verifying voice-owner consent")
        return run_authorized_provider(source, output, provider, consent, public_key, voice_id, model)

    return {"jobId": worker.JOBS.submit("authorized-voice-transform", operation).id}


worker.METHODS.update({
    "spectral.tiles": spectral_tiles,
    "spectral.brush.save": spectral_brush_save,
    "note.resynthesize": note_resynthesize,
    "note.resynthesize.neural": note_resynthesize_neural,
    "mastering.analyze": mastering_analyze,
    "restoration.onnx": restoration_onnx,
    "instrument.render": instrument_render,
    "voice.transform.authorized": voice_transform_authorized,
})

if __name__ == "__main__":
    raise SystemExit(worker.main())
