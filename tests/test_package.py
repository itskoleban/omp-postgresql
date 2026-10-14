"""Adversarial checks for the distributable archive gate (no database required)."""

import importlib.util
import json
from pathlib import Path
import stat
import struct
import subprocess
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location(
    "package", Path(__file__).parents[1] / "scripts/package.py"
)
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


def pe(machine=0x14C, magic=0x10B):
    data = bytearray(100)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 60, 64)
    data[64:68] = b"PE\0\0"
    struct.pack_into("<H", data, 68, machine)
    struct.pack_into("<H", data, 88, magic)
    return bytes(data)


def payload():
    files = {
        name: b"notice"
        for name in (
            "README.md",
            "LICENSE",
            "NOTICE.md",
            "DEPENDENCIES.md",
            "docs/installation.md",
            "docs/api.md",
            "docs/security.md",
            "docs/validation.md",
            "docs/packaging.md",
            "qawno/include/omp-postgresql.inc",
            "licenses/libpq/COPYRIGHT",
            "share/omp-postgresql/LICENSE",
            "share/omp-postgresql/NOTICE.md",
        )
    }
    files.update(
        {
            f"share/omp-postgresql/licenses/{name}/{license}": b"notice"
            for name, license in package.LICENSES.items()
        }
    )
    files.update({"components/omp-postgresql.dll": pe(), "libpq.dll": pe()})
    commit = "a" * 40
    name = f"omp-postgresql-0.1.0-experimental.{commit[:12]}-windows-x86.zip"
    manifest = {
        "archive": name,
        "version": "0.1.0",
        "commit": commit,
        "platform": "windows",
        "architecture": "x86",
        "build": {
            "configuration": "Release",
            "pointer_bytes": 4,
            "sanitize": "OFF",
            "commit": commit,
            "version": "0.1.0",
            "system": "Windows",
        },
        "dependencies": {
            "components/omp-postgresql.dll": {"imports": ["libpq.dll"]},
            "libpq.dll": {"imports": ["kernel32.dll"]},
        },
        "files": {name: package.digest(data) for name, data in files.items()},
    }
    return files, manifest


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.files, self.manifest = payload()

    def test_valid_inventory(self):
        package.validate_payload(self.files, self.manifest)

    def test_checkout_allows_downloads_but_rejects_modified_or_untracked_assets(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            subprocess.run(["git", "init", "-q", temp], check=True)
            (root / "docs").mkdir()
            tracked = root / "docs/api.md"
            tracked.write_text("tracked documentation", encoding="utf8")
            subprocess.run(["git", "add", "."], cwd=root, check=True)
            subprocess.run(
                [
                    "git",
                    "-c",
                    "user.name=Package Test",
                    "-c",
                    "user.email=package-test@example.invalid",
                    "commit",
                    "-qm",
                    "fixture",
                ],
                cwd=root,
                check=True,
            )
            (root / "postgresql-17.11.tar.gz").write_bytes(b"download input")
            (root / "libpq32").mkdir()
            (root / "libpq32/libpq.dll").write_bytes(b"download input")
            package.ensure_checkout(root)
            untracked = root / "docs/credentials.md"
            untracked.write_text("must not be packaged", encoding="utf8")
            with self.assertRaises(ValueError):
                package.ensure_checkout(root)
            untracked.unlink()
            tracked.write_text("changed documentation", encoding="utf8")
            with self.assertRaises(ValueError):
                package.ensure_checkout(root)

    def test_incorrect_build_configuration(self):
        for field, value in (
            ("configuration", "Debug"),
            ("pointer_bytes", 8),
            ("sanitize", "ON"),
            ("commit", "b" * 40),
            ("version", "0.2.0"),
        ):
            with self.subTest(field=field):
                manifest = json.loads(json.dumps(self.manifest))
                manifest["build"][field] = value
                with self.assertRaises(ValueError):
                    package.validate_payload(self.files, manifest)

    def test_missing_mandatory_notice(self):
        del self.files["LICENSE"]
        with self.assertRaises(ValueError):
            package.validate_payload(self.files, self.manifest)

    def test_altered_payload(self):
        self.files["README.md"] = b"changed"
        with self.assertRaises(ValueError):
            package.validate_payload(self.files, self.manifest)

    def test_broken_documentation_link(self):
        for target in ("missing.md", "../pawn/omp-postgresql.inc", "../secret.key"):
            with self.subTest(target=target):
                files = {**self.files, "docs/api.md": f"[example]({target})".encode()}
                manifest = {
                    **self.manifest,
                    "files": {
                        **self.manifest["files"],
                        "docs/api.md": package.digest(files["docs/api.md"]),
                    },
                }
                with self.assertRaises(ValueError):
                    package.validate_payload(files, manifest)

    def test_unexpected_file_even_when_manifest_lists_it(self):
        for name in (
            ".pgpass",
            "build/CMakeCache.txt",
            "docs/server.key",
            "examples/production.conf",
        ):
            with self.subTest(name=name):
                files = {**self.files, name: b"private"}
                manifest = {
                    **self.manifest,
                    "files": {**self.manifest["files"], name: package.digest(b"private")},
                }
                with self.assertRaises(ValueError):
                    package.validate_payload(files, manifest)

    def test_wrong_pe_architecture(self):
        for data in (pe(0x8664, 0x20B), pe(magic=0x20B), pe()[:70], b"not a binary"):
            with self.subTest(data=data[:2]):
                with self.assertRaises(ValueError):
                    package.binary_architecture(data, "windows")

    def test_wrong_elf_architecture(self):
        data = bytearray(52)
        data[:6] = b"\x7fELF\x01\x01"
        struct.pack_into("<H", data, 18, 3)
        package.binary_architecture(data, "linux")
        for offset, value in ((4, 2), (5, 2), (18, 62)):
            altered = bytearray(data)
            altered[offset] = value
            with self.assertRaises(ValueError):
                package.binary_architecture(altered, "linux")

    def test_missing_transitive_runtime(self):
        self.manifest["dependencies"]["libpq.dll"]["imports"].append("libcrypto-3.dll")
        with self.assertRaises(ValueError):
            package.validate_payload(self.files, self.manifest)

    def test_missing_runtime_license(self):
        self.files["libcrypto-3.dll"] = pe()
        self.manifest["files"]["libcrypto-3.dll"] = package.digest(pe())
        self.manifest["dependencies"]["libcrypto-3.dll"] = {"imports": ["kernel32.dll"]}
        with self.assertRaises(ValueError):
            package.validate_payload(self.files, self.manifest)

    def test_unexpected_system_runtime(self):
        self.manifest["dependencies"]["libpq.dll"]["imports"].append("evil.dll")
        with self.assertRaises(ValueError):
            package.validate_payload(self.files, self.manifest)

    def write_archive(self, directory, extra=None):
        archive = Path(directory) / self.manifest["archive"]
        with zipfile.ZipFile(archive, "w") as output:
            for name, data in self.files.items():
                output.writestr(f"omp-postgresql/{name}", data)
            output.writestr("omp-postgresql/manifest.json", json.dumps(self.manifest))
            if extra:
                output.writestr(*extra)
        archive.with_suffix(".zip.sha256").write_text(
            f"{package.digest(archive.read_bytes())}  {archive.name}\n", encoding="utf8"
        )
        return archive

    def test_archive_roundtrip_and_no_overlay(self):
        with tempfile.TemporaryDirectory() as temp:
            archive = self.write_archive(temp)
            destination = Path(temp) / "installation"
            package.verify(archive, destination)
            self.assertEqual((destination / "omp-postgresql/libpq.dll").read_bytes(), pe())
            with self.assertRaises(FileExistsError):
                package.verify(archive, destination)

    def test_archive_checksum_failure(self):
        with tempfile.TemporaryDirectory() as temp:
            archive = self.write_archive(temp)
            with archive.open("ab") as output:
                output.write(b"altered")
            with self.assertRaises(ValueError):
                package.verify(archive)

    def test_unsafe_and_duplicate_paths(self):
        for name in (
            "../escaped",
            "omp-postgresql/../escaped",
            "omp-postgresql//odd",
            "omp-postgresql/C:/escaped",
            "omp-postgresql/dir\\escaped",
            "omp-postgresql/README.md",
        ):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temp:
                archive = self.write_archive(temp, (name, b"bad"))
                with self.assertRaises(ValueError):
                    package.verify(archive)

    def test_symlink_entry(self):
        entry = zipfile.ZipInfo("omp-postgresql/link")
        entry.create_system = 3
        entry.external_attr = (stat.S_IFLNK | 0o777) << 16
        with tempfile.TemporaryDirectory() as temp:
            archive = self.write_archive(temp, (entry, b"/etc/passwd"))
            with self.assertRaises(ValueError):
                package.verify(archive)


if __name__ == "__main__":
    unittest.main()
