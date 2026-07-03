from __future__ import annotations

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
