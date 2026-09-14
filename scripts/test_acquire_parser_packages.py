import hashlib
import json
from pathlib import Path
import tarfile
import tempfile
import unittest

from acquire_parser_packages import PARSERS, acquire


class PackageAcquisitionTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.prefix = self.root / "installed"
        self.lock = self.root / "lock.json"
        self.packages = []
        for name in sorted(PARSERS):
            source = self.root / name
            for relative in [f"share/{name}/build-info.json", f"lib/cmake/{name}/{name}Config.cmake"]:
                path = source / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(name)
            archive = self.root / f"{name}.tar.gz"
            with tarfile.open(archive, "w:gz") as tar:
                tar.add(source, arcname=".")
            self.packages.append({"name": name, "url": archive.as_uri(),
                                  "sha256": hashlib.sha256(archive.read_bytes()).hexdigest()})

    def run_acquisition(self):
        self.lock.write_text(json.dumps({"test-platform": self.packages}))
        return acquire(self.lock, "test-platform", self.prefix)

    def test_complete_prefix_preserves_producer_files(self):
        self.run_acquisition()
        for name in PARSERS:
            self.assertEqual((self.prefix / f"share/{name}/build-info.json").read_text(), name)
        self.assertEqual((self.prefix / "parser-package-lock.json").read_bytes(), self.lock.read_bytes())

    def test_bad_hash_does_not_publish_partial_prefix(self):
        self.packages[-1]["sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "SHA256 mismatch"):
            self.run_acquisition()
        self.assertFalse(self.prefix.exists())

    def test_existing_prefix_is_not_overwritten(self):
        self.run_acquisition()
        with self.assertRaisesRegex(ValueError, "destination already exists"):
            self.run_acquisition()

    def test_duplicate_parser_cannot_replace_missing_parser(self):
        self.packages[-1] = self.packages[0]
        with self.assertRaisesRegex(ValueError, "exactly the four"):
            self.run_acquisition()
        self.assertFalse(self.prefix.exists())


if __name__ == "__main__":
    unittest.main()
