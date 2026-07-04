from __future__ import annotations

import argparse
import platform
import sys
from pathlib import Path
from typing import Any

from cli_support import CliError, emit, invoke, load_json, parse_csv

VERSION = "0.4.0"
AUDIO_EXTENSIONS = {
    ".wav", ".flac", ".aiff", ".aif", ".mp3", ".aac", ".m4a",
    ".ogg", ".opus", ".wma", ".alac", ".caf",
}


def add_mode_options(parser: argparse.ArgumentParser, *, multiple: bool) -> None:
    parser.add_argument("source", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--stems", default="vocals,instrumental")
    parser.add_argument("--quality", choices=["fast", "balanced", "studio", "maximum"], default="balanced")
    parser.add_argument("--format", dest="output_format", default="wav")
    parser.add_argument("--device", default="auto")
    parser.add_argument("--overwrite", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    if multiple:
        parser.add_argument("--engines", required=True, help="Comma-separated engine IDs")
        parser.add_argument("--models", default="", help="Comma-separated model IDs matching engines")
    else:
        parser.add_argument("--engine", default="demucs")
        parser.add_argument("--model", default="")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="omnistem", description="OmniStem Studio command line")
    parser.add_argument("--version", action="version", version=f"omnistem {VERSION}")
    parser.add_argument("--json", action="store_true", help="Emit compact JSON")
    commands = parser.add_subparsers(dest="command", required=True)

    commands.add_parser("env", help="Show environment information")
    commands.add_parser("doctor", help="Check worker and runtime capabilities")
    commands.add_parser("engines", help="List supported separation engines")

    models = commands.add_parser("models", help="Search the model catalog")
    models.add_argument("--query", default="")
    models.add_argument("--engine", default="")
    models.add_argument("--family", default="")
    models.add_argument("--tag", default="")
    models.add_argument("--stem", default="")
    models.add_argument("--current-only", action="store_true")
    models.add_argument("--sort", dest="sort_by", default="name")
    models.add_argument("--descending", action="store_true")

    inspect = commands.add_parser("inspect", help="Analyze a WAV file")
    inspect.add_argument("source", type=Path)

    separate = commands.add_parser("separate", help="Run standard separation")
    add_mode_options(separate, multiple=False)

    auto = commands.add_parser("auto", help="Automatically select an engine and model")
    add_mode_options(auto, multiple=False)

    for name in ("compare", "ensemble", "cascade", "god"):
        mode_parser = commands.add_parser(name, help=f"Run {name} separation mode")
        add_mode_options(mode_parser, multiple=True)
        if name == "cascade":
            mode_parser.add_argument("--cascade-stem", default="vocals")

    batch = commands.add_parser("batch", help="Process a folder of audio files")
    batch.add_argument("folder", type=Path)
    batch.add_argument("--output", type=Path, default=Path("outputs"))
    batch.add_argument("--engine", default="demucs")
    batch.add_argument("--model", default="")
    batch.add_argument("--stems", default="vocals,instrumental")
    batch.add_argument("--quality", choices=["fast", "balanced", "studio", "maximum"], default="balanced")
    batch.add_argument("--format", dest="output_format", default="wav")
    batch.add_argument("--device", default="auto")
    batch.add_argument("--overwrite", action="store_true")
    batch.add_argument("--recursive", action=argparse.BooleanOptionalAction, default=True)
    batch.add_argument("--dry-run", action="store_true")

    pipeline = commands.add_parser("pipeline", help="Validate or run a JSON pipeline")
    pipeline.add_argument("definition", type=Path)
    pipeline.add_argument("--dry-run", action="store_true")

    benchmark = commands.add_parser("benchmark", help="Compare WAV candidates")
    benchmark.add_argument("candidates", nargs="+", type=Path)
    benchmark.add_argument("--reference", type=Path)

    verify = commands.add_parser("verify-model", help="Verify a model artifact SHA-256")
    verify.add_argument("artifact", type=Path)
    verify.add_argument("sha256")

    return parser


def mode_params(args: argparse.Namespace, mode: str) -> dict[str, Any]:
    params: dict[str, Any] = {
        "source": str(args.source.resolve()),
        "mode": mode,
        "stems": parse_csv(args.stems),
        "quality": args.quality,
        "outputFormat": args.output_format,
        "device": args.device,
        "overwrite": args.overwrite,
    }
    if args.output:
        params["outputDir"] = str(args.output.resolve())
    if hasattr(args, "engines"):
        params["engines"] = parse_csv(args.engines)
        models = parse_csv(args.models)
        if models:
            params["models"] = models
    else:
        params["engine"] = args.engine
        params["model"] = args.model
    if mode == "cascade":
        params["cascadeStem"] = args.cascade_stem
    return params


def run_batch(args: argparse.Namespace) -> dict[str, Any]:
    folder = args.folder.resolve()
    if not folder.is_dir():
        raise CliError(f"batch folder does not exist: {folder}")
    pattern = "**/*" if args.recursive else "*"
    files = sorted(path for path in folder.glob(pattern) if path.is_file() and path.suffix.lower() in AUDIO_EXTENSIONS)
    if not files:
        raise CliError("no supported audio files found")
    results = []
    for source in files:
        params = {
            "source": str(source),
            "outputDir": str((args.output / source.stem).resolve()),
            "mode": "standard",
            "engine": args.engine,
            "model": args.model,
            "stems": parse_csv(args.stems),
            "quality": args.quality,
            "outputFormat": args.output_format,
            "device": args.device,
            "overwrite": args.overwrite,
        }
        method = "mode.plan" if args.dry_run else "mode.run"
        try:
            results.append({"source": str(source), "ok": True, "result": invoke(method, params)})
        except CliError as exc:
            results.append({"source": str(source), "ok": False, "error": str(exc)})
    return {"files": len(files), "succeeded": sum(1 for row in results if row["ok"]), "results": results}


def dispatch(args: argparse.Namespace) -> dict[str, Any]:
    if args.command == "env":
        return {
            "omnistem": VERSION,
            "python": sys.version,
            "platform": platform.platform(),
            "machine": platform.machine(),
        }
    if args.command == "doctor":
        return invoke("health.check", {})
    if args.command == "engines":
        return invoke("engine.list", {})
    if args.command == "models":
        return invoke("catalog.models", {
            "query": args.query,
            "engine": args.engine,
            "family": args.family,
            "tag": args.tag,
            "stem": args.stem,
            "includeDeprecated": not args.current_only,
            "sortBy": args.sort_by,
            "descending": args.descending,
        })
    if args.command == "inspect":
        return invoke("benchmark.analyze", {"source": str(args.source.resolve())})
    if args.command in {"separate", "auto", "compare", "ensemble", "cascade", "god"}:
        mode = "standard" if args.command == "separate" else ("comparison" if args.command == "compare" else args.command)
        params = mode_params(args, mode)
        return invoke("mode.plan" if args.dry_run else "mode.run", params)
    if args.command == "batch":
        return run_batch(args)
    if args.command == "pipeline":
        definition = load_json(args.definition.resolve())
        return invoke("pipeline.validate" if args.dry_run else "pipeline.run", {"pipeline": definition})
    if args.command == "benchmark":
        return invoke("benchmark.compare", {
            "candidates": [str(path.resolve()) for path in args.candidates],
            "reference": str(args.reference.resolve()) if args.reference else "",
        })
    if args.command == "verify-model":
        return invoke("catalog.verify", {"artifact": str(args.artifact.resolve()), "sha256": args.sha256})
    raise CliError(f"unsupported command: {args.command}")


def main() -> int:
    args = build_parser().parse_args()
    try:
        result = dispatch(args)
    except CliError as exc:
        emit({"ok": False, "error": str(exc)}, compact=args.json)
        return 1
    emit({"ok": True, "result": result}, compact=args.json)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
