from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


def export_png_tiles(manifest: dict[str, Any]) -> dict[str, Any]:
    import numpy as np
    from PIL import Image

    for tile in manifest.get("tiles", []):
        source = Path(tile["path"])
        values = np.load(source, allow_pickle=False)
        image = np.round(values.astype(np.float32) / 65535.0 * 255.0).astype(np.uint8)
        image = np.flipud(image)
        destination = source.with_suffix(".png")
        Image.fromarray(image, mode="L").save(destination, optimize=True)
        tile["pngPath"] = str(destination)
    manifest["imageStorage"] = "8-bit-grayscale-png"
    return manifest


def convert_manifest(manifest_path: Path) -> dict[str, Any]:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    converted = export_png_tiles(manifest)
    manifest_path.write_text(json.dumps(converted, indent=2), encoding="utf-8")
    return converted


def main() -> int:
    parser = argparse.ArgumentParser(description="Export OmniStem spectrogram tiles as PNG images")
    parser.add_argument("manifest", type=Path)
    args = parser.parse_args()
    result = convert_manifest(args.manifest.resolve())
    print(json.dumps({"manifest": str(args.manifest), "tileCount": len(result.get("tiles", []))}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
