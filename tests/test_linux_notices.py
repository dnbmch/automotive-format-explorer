"""Linux packaging failure controls; no changes to the package database."""
import importlib.util
import json
import pathlib
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("notices", ROOT / "scripts/linux_notices.py")
notices = importlib.util.module_from_spec(spec)
spec.loader.exec_module(notices)


class NoticesTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="notices with spaces ")
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.app = self.root / "AppDir"
        self.qt = self.root / "Qt prefix"
        self.material = self.root / "licenses"
        for p in (self.app / "usr/qml/Example", self.qt / "qml/Example", self.qt / "sbom", self.material):
            p.mkdir(parents=True)
        self.source = self.qt / "qml/Example/libexample.so"
        cpp = self.root / "example.cpp"
        cpp.write_text('extern "C" int answer() { return 42; }\n')
        subprocess.run(["g++", "-shared", "-fPIC", "-Wl,--build-id", "-Wl,-rpath,/original/qt/path",
                        str(cpp), "-o", str(self.source)], check=True)
        self.payload = self.app / "usr/qml/Example/libexample.so"
        shutil.copyfile(self.source, self.payload)
        self.sbom = self.qt / "sbom/example.spdx.json"
        self.sbom.write_text(json.dumps({
            "name": "example", "files": [{"fileName": "./qml/Example/libexample.so",
                "licenseConcluded": "MIT", "copyrightText": "Example copyright",
                "checksums": [{"algorithm": "SHA1", "checksumValue": notices.digest(self.source, "sha1")}]}],
            "packages": [{"name": "Example", "licenseConcluded": "MIT"}],
            "hasExtractedLicensingInfos": []}))
        (self.material / "MIT.txt").write_text("Example fixture license text\n")
        self.system = patch.object(notices, "system_libraries", return_value=[])
        self.system.start()
        self.addCleanup(self.system.stop)

    def generate(self):
        notices.generate(self.app, self.qt, self.material)

    def test_qml_and_symlink_are_attributed_deterministically(self):
        (self.payload.parent / "libalias.so").symlink_to(self.payload.name)
        self.generate()
        doc = self.app / notices.DOC
        before = {p.relative_to(doc): p.read_bytes() for p in doc.rglob("*") if p.is_file()}
        self.generate()
        self.assertEqual(before, {p.relative_to(doc): p.read_bytes() for p in doc.rglob("*") if p.is_file()})
        text = (doc / "THIRD_PARTY_NOTICES.txt").read_text()
        self.assertIn("usr/qml/Example/libexample.so", text)
        self.assertIn("usr/qml/Example/libalias.so", text)
        self.assertEqual(len(list((doc / "licenses").glob("*.txt"))), 1)
        notices.verify(self.app)

    def test_unknown_payload_fails(self):
        self.source.unlink()
        with self.assertRaisesRegex(ValueError, "libexample.so.*origin"):
            self.generate()

    def test_rpath_modified_after_copy(self):
        original = b"/original/qt/path"
        replacement = b"/deployed/qt/path"
        self.assertEqual(len(original), len(replacement))
        data = self.payload.read_bytes()
        self.assertIn(original, data)
        self.payload.write_bytes(data.replace(original, replacement))
        self.assertNotEqual(notices.digest(self.source), notices.digest(self.payload))
        self.assertIn("/deployed/qt/path", notices.run("readelf", "-d", str(self.payload)))
        self.generate()
        notices.verify(self.app)

    def test_debian_plugin_and_missing_copyright(self):
        system = self.root / "system/libexample.so"
        system.parent.mkdir()
        self.source.rename(system)
        plugin = self.app / "usr/plugins/platforms/libexample.so"
        plugin.parent.mkdir(parents=True)
        self.payload.rename(plugin)
        copyright_file = self.root / "copyright"
        copyright_file.write_text("Fixture copyright. See /usr/share/common-licenses/GPL-3.\n")
        with patch.object(notices, "system_libraries", return_value=[system]), \
                patch.object(notices, "debian_package", return_value=("fixture:amd64", "1.2", copyright_file)):
            self.generate()
            notice = (self.app / notices.DOC / "THIRD_PARTY_NOTICES.txt").read_text()
            self.assertIn("Debian package fixture:amd64 1.2", notice)
            self.assertIn("usr/plugins/platforms/libexample.so", notice)
            self.assertTrue((self.app / notices.DOC / "licenses/GPL-3").is_file())
            notices.verify(self.app)
            copyright_file.unlink()
            with self.assertRaisesRegex(ValueError, "libexample.so.*copyright"):
                self.generate()

    def test_unrelated_same_name_fails(self):
        cpp = self.root / "different.cpp"
        cpp.write_text('extern "C" int answer() { return 99; }\n')
        subprocess.run(["g++", "-shared", "-fPIC", "-Wl,--build-id", str(cpp),
                        "-o", str(self.payload)], check=True)
        with self.assertRaisesRegex(ValueError, "libexample.so.*origin"):
            self.generate()

    def test_missing_license_fails_with_payload(self):
        (self.material / "MIT.txt").unlink()
        with self.assertRaisesRegex(ValueError, "libexample.so.*MIT"):
            self.generate()

    def test_missing_sbom_record_fails(self):
        self.sbom.write_text('{"files": [], "packages": []}')
        with self.assertRaisesRegex(ValueError, "libexample.so.*SBOM"):
            self.generate()

    def test_sbom_checksum_mismatch_fails(self):
        self.sbom.write_text(self.sbom.read_text().replace(notices.digest(self.source, "sha1"), "0" * 40))
        with self.assertRaisesRegex(ValueError, "libexample.so.*checksum"):
            self.generate()

    def test_added_payload_after_notices_fails_verification(self):
        self.generate()
        shutil.copyfile(self.payload, self.payload.parent / "extra.so")
        with self.assertRaisesRegex(ValueError, "coverage"):
            notices.verify(self.app)


if __name__ == "__main__":
    unittest.main()
