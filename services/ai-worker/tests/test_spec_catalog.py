from __future__ import annotations

import hashlib
import sys
import tempfile
import unittest
from pathlib import Path

WORKER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(WORKER_DIR))

from spec_catalog import (
    CatalogError,
    normalize_stem,
    normalize_stems,
    resolve_model,
    search_models,
    verify_artifact,
)


class SpecCatalogTests(unittest.TestCase):
    def test_stem_aliases_normalize_and_deduplicate(self) -> None:
        self.assertEqual(normalize_stem("voice"), "vocals")
        self.assertEqual(normalize_stems(["voice", "vocals", "drum"]), ["vocals", "drums"])

    def test_unknown_stem_is_rejected(self) -> None:
        with self.assertRaises(CatalogError):
            normalize_stem("lead-synth")

    def test_model_alias_resolves(self) -> None:
        model = resolve_model("demucs-studio")
        self.assertEqual(model.id, "htdemucs_ft")

    def test_search_filter_sort_and_deprecation(self) -> None:
        rows = search_models(engine="demucs", stem="vocals", sort_by="id")
        self.assertGreaterEqual(len(rows), 3)
        self.assertEqual(rows, sorted(rows, key=lambda row: row["id"].casefold()))
        current = search_models(include_deprecated=False)
        self.assertFalse(any(row["deprecated"] for row in current))

    def test_checksum_verification(self) -> None:
        root = Path(tempfile.mkdtemp(prefix="omnistem-catalog-test-"))
        artifact = root / "model.bin"
        artifact.write_bytes(b"omnistem-model")
        digest = hashlib.sha256(artifact.read_bytes()).hexdigest()
        result = verify_artifact(artifact, digest)
        self.assertTrue(result["verified"])
        self.assertEqual(result["actual"], digest)

    def test_invalid_checksum_is_rejected(self) -> None:
        root = Path(tempfile.mkdtemp(prefix="omnistem-catalog-bad-"))
        artifact = root / "model.bin"
        artifact.write_bytes(b"x")
        with self.assertRaises(CatalogError):
            verify_artifact(artifact, "not-a-checksum")


if __name__ == "__main__":
    unittest.main()
