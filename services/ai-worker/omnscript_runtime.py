from __future__ import annotations

import ast
import multiprocessing as mp
import queue
from dataclasses import dataclass
from typing import Any


class OmniScriptError(ValueError):
    pass


_ALLOWED_NODES = {
    ast.Module, ast.Expr, ast.Assign, ast.Name, ast.Load, ast.Store,
    ast.Constant, ast.List, ast.Tuple, ast.Dict, ast.Subscript, ast.Slice,
    ast.BinOp, ast.UnaryOp, ast.BoolOp, ast.Compare, ast.If, ast.For,
    ast.Call, ast.keyword, ast.Add, ast.Sub, ast.Mult, ast.Div, ast.Mod,
    ast.USub, ast.UAdd, ast.And, ast.Or, ast.Not, ast.Eq, ast.NotEq,
    ast.Lt, ast.LtE, ast.Gt, ast.GtE, ast.In, ast.NotIn, ast.IfExp,
    ast.Break, ast.Continue, ast.Pass,
}

_ALLOWED_CALLS = {
    "notes", "stems", "set_note_gain", "set_note_pitch", "mute_note",
    "move_spectral_region", "request_separation", "request_transcription",
    "len", "min", "max", "round", "abs", "range", "enumerate",
}


@dataclass(frozen=True)
class ValidationResult:
    valid: bool
    errors: list[str]


def validate_script(source: str) -> ValidationResult:
    errors: list[str] = []
    try:
        tree = ast.parse(source, mode="exec")
    except SyntaxError as exc:
        return ValidationResult(False, [f"line {exc.lineno}: {exc.msg}"])

    for node in ast.walk(tree):
        if type(node) not in _ALLOWED_NODES:
            errors.append(f"line {getattr(node, 'lineno', '?')}: {type(node).__name__} is not allowed")
        if isinstance(node, ast.Name) and node.id.startswith("__"):
            errors.append(f"line {node.lineno}: private names are not allowed")
        if isinstance(node, ast.Call):
            if not isinstance(node.func, ast.Name) or node.func.id not in _ALLOWED_CALLS:
                errors.append(f"line {node.lineno}: function call is not permitted")
    return ValidationResult(not errors, errors)


def _execute_child(source: str, context: dict[str, Any], output: Any) -> None:
    validation = validate_script(source)
    if not validation.valid:
        output.put({"ok": False, "errors": validation.errors})
        return

    commands: list[dict[str, Any]] = []
    project_notes = list(context.get("notes", []))
    project_stems = list(context.get("stems", []))

    def notes() -> list[dict[str, Any]]:
        return [dict(item) for item in project_notes]

    def stems() -> list[dict[str, Any]]:
        return [dict(item) for item in project_stems]

    def emit(command: str, **params: Any) -> None:
        commands.append({"command": command, "params": params})

    env = {
        "notes": notes,
        "stems": stems,
        "set_note_gain": lambda note_id, gain_db: emit("set_note_gain", noteId=str(note_id), gainDb=float(gain_db)),
        "set_note_pitch": lambda note_id, semitones: emit("set_note_pitch", noteId=str(note_id), semitones=float(semitones)),
        "mute_note": lambda note_id: emit("mute_note", noteId=str(note_id)),
        "move_spectral_region": lambda source_stem, destination_stem, start, end, low_hz, high_hz: emit(
            "move_spectral_region", sourceStem=str(source_stem), destinationStem=str(destination_stem),
            start=float(start), end=float(end), lowHz=float(low_hz), highHz=float(high_hz)),
        "request_separation": lambda quality="balanced": emit("request_separation", quality=str(quality)),
        "request_transcription": lambda stem_id: emit("request_transcription", stemId=str(stem_id)),
        "len": len, "min": min, "max": max, "round": round, "abs": abs,
        "range": range, "enumerate": enumerate,
    }
    try:
        exec(compile(source, "<omnscript>", "exec"), {"__builtins__": {}}, env)
        output.put({"ok": True, "transaction": commands})
    except Exception as exc:
        output.put({"ok": False, "errors": [str(exc)]})


def execute_script(source: str, context: dict[str, Any], timeout_seconds: float = 2.0) -> dict[str, Any]:
    process_context = mp.get_context("spawn")
    result_queue = process_context.Queue(maxsize=1)
    process = process_context.Process(target=_execute_child, args=(source, context, result_queue), daemon=True)
    process.start()
    process.join(max(0.1, min(timeout_seconds, 10.0)))
    if process.is_alive():
        process.terminate()
        process.join(1.0)
        return {"ok": False, "errors": ["OmniScript exceeded its execution time limit"]}
    try:
        return result_queue.get_nowait()
    except queue.Empty:
        return {"ok": False, "errors": ["OmniScript exited without a result"]}
