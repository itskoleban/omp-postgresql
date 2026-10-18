"""Assemble and verify x86 packages; never publish a release."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import struct
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
LICENSES = {
    "open.mp-sdk": "LICENSE.md",
    "pawn": "license.txt",
    "glm": "copying.txt",
    "robin-hood-hashing": "LICENSE",
    "span-lite": "LICENSE.txt",
    "string-view-lite": "LICENSE.txt",
}
WINDOWS_RUNTIME = {"libpq.dll", "libssl-3.dll", "libcrypto-3.dll", "libwinpthread-1.dll"}
WINDOWS_SYSTEM = {
    "advapi32.dll",
    "bcrypt.dll",
    "crypt32.dll",
    "gdi32.dll",
    "iphlpapi.dll",
    "kernel32.dll",
    "msvcrt.dll",
    "ntdll.dll",
    "ole32.dll",
    "rpcrt4.dll",
    "secur32.dll",
    "shell32.dll",
    "user32.dll",
    "version.dll",
    "ws2_32.dll",
    "msvcp140.dll",
    "vcruntime140.dll",
    "vcruntime140_1.dll",
    "ucrtbase.dll",
}
LINUX_SYSTEM = {
    "libssl.so.3",
    "libcrypto.so.3",
    "libstdc++.so.6",
    "libgcc_s.so.1",
    "libc.so.6",
    "libm.so.6",
    "libdl.so.2",
    "libpthread.so.0",
    "libresolv.so.2",
    "ld-linux.so.2",
}


def run(argv, **kwargs):
    return subprocess.check_output(argv, text=True, encoding="utf8", **kwargs).strip()


def digest(data):
    return hashlib.sha256(data).hexdigest()


def archive_name(version, commit, platform):
    if not re.fullmatch(
        r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)(?:-rc\.[1-9][0-9]*)?", version
    ):
        raise ValueError("Invalid project version")
    # Continue verifying historical experimental artifacts without relabelling them.
    identity = version if "-rc." in version else f"{version}-experimental"
    return f"omp-postgresql-{identity}.{commit[:12]}-{platform}-x86.zip"


def record_libpq_build(libpq, source):
    """Bind the Windows source correction to the exact built DLL, before packaging."""
    patch = ROOT / "scripts/patches/libpq-windows-mutex.patch"
    subprocess.run(
        ["git", "apply", "--no-index", "--reverse", "--check", str(patch)],
        cwd=source,
        check=True,
    )
    header = (source / "src/include/pg_config.h").read_text(encoding="utf8")
    version = re.search(r'^#define PG_VERSION "([0-9.]+)"$', header, re.MULTILINE)
    if not version:
        raise ValueError("Unknown PostgreSQL source version")
    info = {
        "source_version": version[1],
        "dll_sha256": digest(libpq.read_bytes()),
        "mutex_patch_sha256": digest(patch.read_bytes()),
    }
    libpq.with_name("libpq-build-info.json").write_text(
        json.dumps(info, indent=4, sort_keys=True) + "\n", encoding="utf8", newline="\n"
    )


def binary_architecture(data, platform):
    if platform == "windows":
        if len(data) < 64 or data[:2] != b"MZ":
            raise ValueError("Expected a PE binary")
        offset = struct.unpack_from("<I", data, 60)[0]
        if offset + 26 > len(data) or data[offset : offset + 4] != b"PE\0\0":
            raise ValueError("Invalid PE header")
        machine = struct.unpack_from("<H", data, offset + 4)[0]
        magic = struct.unpack_from("<H", data, offset + 24)[0]
        if machine != 0x14C or magic != 0x10B:
            raise ValueError("Package requires x86 PE32 binaries")
    else:
        if len(data) < 52 or data[:6] != b"\x7fELF\x01\x01":
            raise ValueError("Package requires little-endian ELF32 binaries")
        if struct.unpack_from("<H", data, 18)[0] != 3:
            raise ValueError("Package requires Intel 80386 ELF binaries")


def windows_system(name):
    return name in WINDOWS_SYSTEM or bool(re.fullmatch(r"(?:api|ext)-ms-win-[a-z0-9-]+\.dll", name))


def inspect_binary(path, platform, objdump):
    binary_architecture(path.read_bytes(), platform)
    if platform == "windows":
        output = run([objdump, "-p", str(path)])
        imports = sorted(set(name.lower() for name in re.findall(r"DLL Name:\s+(\S+)", output)))
        if not imports:
            raise ValueError(f"No imports found in {path.name}")
        return {"imports": imports, "symbol_versions": []}
    output = run(["readelf", "-d", "-V", str(path)])
    imports = sorted(set(re.findall(r"\(NEEDED\).*\[(.*?)\]", output)))
    versions = sorted(
        set(re.findall(r"Name: ((?:GLIBC|GLIBCXX|CXXABI|OPENSSL|GCC)_[\w.]+)", output))
    )
    if not imports:
        raise ValueError(f"No DT_NEEDED found in {path.name}")
    resolution = run(["ldd", str(path)])
    if "not found" in resolution:
        raise ValueError(f"Unresolved runtime dependency: {resolution}")
    for resolved in re.findall(r"=>\s+(/\S+)", resolution):
        binary_architecture(Path(resolved).read_bytes(), platform)
    return {"imports": imports, "symbol_versions": versions}


def copy(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)


def validate_payload(files, manifest):
    platform = manifest["platform"]
    if platform not in ("windows", "linux") or manifest["architecture"] != "x86":
        raise ValueError("Unsupported distribution platform")
    build = manifest["build"]
    if (
        build["configuration"] != "Release"
        or build["pointer_bytes"] != 4
        or build["sanitize"] != "OFF"
        or build["commit"] != manifest["commit"]
        or build["version"] != manifest["version"]
        or build["system"] != {"windows": "Windows", "linux": "Linux"}[platform]
    ):
        raise ValueError("Build metadata does not describe the package")
    if not re.fullmatch(r"[0-9a-f]{40}", manifest["commit"]):
        raise ValueError("Invalid source commit")
    expected_name = archive_name(manifest["version"], manifest["commit"], platform)
    if manifest["archive"] != expected_name:
        raise ValueError("Archive name does not match version/commit/platform")
    component = f"components/omp-postgresql.{ 'dll' if platform == 'windows' else 'so'}"
    required = {
        component,
        "qawno/include/omp-postgresql.inc",
        "README.md",
        "LICENSE",
        "NOTICE.md",
        "DEPENDENCIES.md",
        "docs/installation.md",
        "docs/api.md",
        "docs/security.md",
        "docs/validation.md",
        "docs/packaging.md",
        "docs/debugging.md",
        "docs/prestable-cleanup.md",
        "examples/sql/accounts.sql",
        "licenses/libpq/COPYRIGHT",
        "share/omp-postgresql/LICENSE",
        "share/omp-postgresql/NOTICE.md",
        "share/omp-postgresql/patches/README.md",
        "share/omp-postgresql/patches/libpq-windows-mutex.patch",
    }
    required.update(
        f"share/omp-postgresql/licenses/{name}/{license}" for name, license in LICENSES.items()
    )
    required.add("libpq.dll" if platform == "windows" else "lib/libpq.so.5")
    if not required <= files.keys():
        raise ValueError(f"Missing package files: {sorted(required - files.keys())}")
    if platform == "windows":
        info = manifest.get("libpq_build", {})
        if (
            not isinstance(info, dict)
            or not isinstance(info.get("source_version"), str)
            or not re.fullmatch(r"\d+\.\d+(?:\.\d+)?", info["source_version"])
            or info.get("dll_sha256") != digest(files["libpq.dll"])
            or info.get("mutex_patch_sha256")
            != digest(files["share/omp-postgresql/patches/libpq-windows-mutex.patch"])
            or info.get("source_version") != build.get("libpq_version")
        ):
            raise ValueError("Windows libpq correction provenance does not match the payload")
    if set(manifest["files"]) != set(files):
        raise ValueError("Payload does not match the manifest inventory")
    for name, data in files.items():
        if digest(data) != manifest["files"][name] or not data:
            raise ValueError(f"Empty or altered payload: {name}")
        allowed = (
            name in required
            or name in WINDOWS_RUNTIME
            and platform == "windows"
            or re.fullmatch(r"docs/[a-z0-9-]+\.md", name)
            or re.fullmatch(r"examples/[a-z0-9-]+\.pwn", name)
            or name
            in {
                "examples/pg_service.conf",
                "examples/pg_hba.conf",
                "examples/postgresql.sql",
                "examples/sql/tutorial-select.sql",
                "examples/sql/tutorial-auth.sql",
            }
            or name in {"licenses/openssl/LICENSE", "licenses/libwinpthread/COPYING"}
        )
        if not allowed:
            raise ValueError(f"Unexpected package file: {name}")
        if name.endswith(".md"):
            for link in re.findall(r"\]\(([^)]+)\)", data.decode("utf8")):
                if re.match(r"[a-z]+://", link) or link.startswith("#"):
                    continue
                # Public docs currently use simple relative paths; reject missing targets.
                target = str(PurePosixPath(name).parent / link.split("#", 1)[0])
                parts = []
                for part in PurePosixPath(target).parts:
                    if part == ".." and parts:
                        parts.pop()
                    elif part != ".":
                        parts.append(part)
                if "/".join(parts) not in files:
                    raise ValueError(f"Broken package documentation link in {name}: {link}")
    binaries = {
        name
        for name in files
        if name == component or name.endswith(".dll") or name == "lib/libpq.so.5"
    }
    if set(manifest["dependencies"]) != binaries:
        raise ValueError("Dependency inventory must cover every binary")
    bundled = {PurePosixPath(name).name for name in binaries}
    for name in binaries:
        binary_architecture(files[name], platform)
        for dependency in manifest["dependencies"][name]["imports"]:
            if dependency not in bundled and not (
                windows_system(dependency) if platform == "windows" else dependency in LINUX_SYSTEM
            ):
                raise ValueError(f"Unaccounted dependency in {name}: {dependency}")
    if {
        "libssl-3.dll",
        "libcrypto-3.dll",
    } & files.keys() and "licenses/openssl/LICENSE" not in files:
        raise ValueError("Missing OpenSSL license")
    if "libwinpthread-1.dll" in files and "licenses/libwinpthread/COPYING" not in files:
        raise ValueError("Missing MinGW pthread license")
    # Dynamic libpq is a required contract, not merely a file bundled by convention.
    pq_name = "libpq.dll" if platform == "windows" else "libpq.so.5"
    if pq_name not in manifest["dependencies"][component]["imports"]:
        raise ValueError("Component must link the packaged libpq")


def verify(archive, destination=None):
    checksum = archive.with_suffix(archive.suffix + ".sha256").read_text(encoding="utf8").strip()
    if checksum != f"{digest(archive.read_bytes())}  {archive.name}":
        raise ValueError("Archive checksum mismatch")
    files = {}
    with zipfile.ZipFile(archive) as package:
        for entry in package.infolist():
            path = PurePosixPath(entry.filename)
            if (
                path.parts[:1] != ("omp-postgresql",)
                or len(path.parts) < 2
                or ".." in path.parts
                or "\\" in entry.filename
                or ":" in entry.filename
                or entry.filename != path.as_posix()
                or entry.is_dir()
                or stat.S_ISLNK(entry.external_attr >> 16)
            ):
                raise ValueError(f"Unsafe archive entry: {entry.filename}")
            name = PurePosixPath(*path.parts[1:]).as_posix()
            if name in files:
                raise ValueError(f"Duplicate archive entry: {name}")
            files[name] = package.read(entry)
    manifest = json.loads(files.pop("manifest.json"))
    if manifest["archive"] != archive.name:
        raise ValueError("Renamed archive")
    validate_payload(files, manifest)
    if destination:
        # Refuse to overlay another installation or erase anything already present.
        destination.mkdir(parents=True, exist_ok=False)
        for name, data in {
            **files,
            "manifest.json": json.dumps(manifest, indent=4, sort_keys=True).encode() + b"\n",
        }.items():
            target = destination / "omp-postgresql" / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            target.chmod(
                0o755
                if name.startswith(("components/", "lib/")) or name.endswith(".dll")
                else 0o644
            )
    return manifest


def ensure_checkout(source):
    if run(["git", "status", "--porcelain", "--untracked-files=no"], cwd=source):
        raise ValueError("Package requires a clean tracked checkout")
    # CI downloads build inputs beside the checkout. They are never installed, but
    # every untracked asset in an install directory must be rejected, even if ignored.
    if run(
        [
            "git",
            "ls-files",
            "--others",
            "--",
            "docs",
            "examples",
            "pawn",
            "scripts/patches",
        ],
        cwd=source,
    ):
        raise ValueError("Package installation directories contain untracked assets")


def create(args):
    ensure_checkout(ROOT)
    commit = run(["git", "rev-parse", "HEAD"], cwd=ROOT)
    epoch = int(run(["git", "show", "-s", "--format=%ct", "HEAD"], cwd=ROOT))
    build = args.build.resolve()
    metadata = json.loads((build / "package-build-info-Release.json").read_text(encoding="utf8"))
    if metadata["commit"] != commit:
        raise ValueError("Reconfigure/rebuild: build metadata does not match HEAD")
    if run(["git", "diff", "HEAD", "--", "CMakeLists.txt"], cwd=ROOT):
        raise ValueError("CMake version must come from HEAD")
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="omp-pg-package-") as temp:
        stage = Path(temp) / "omp-postgresql"
        subprocess.run(
            [
                "cmake",
                "--install",
                str(build),
                "--config",
                "Release",
                "--component",
                "omp-postgresql",
                "--prefix",
                str(stage),
            ],
            check=True,
        )
        platform = args.platform
        component = (
            stage / "components" / f"omp-postgresql.{'dll' if platform == 'windows' else 'so'}"
        )
        copy(args.libpq, stage / ("libpq.dll" if platform == "windows" else "lib/libpq.so.5"))
        copy(args.postgres_license, stage / "licenses/libpq/COPYRIGHT")
        # API guides/reference pages share the installed include, not the source layout.
        for document in (stage / "docs").glob("*.md"):
            document.write_bytes(
                document.read_bytes().replace(
                    b"../pawn/omp-postgresql.inc", b"../qawno/include/omp-postgresql.inc"
                )
            )
        dependencies = {}
        pending = [component, stage / ("libpq.dll" if platform == "windows" else "lib/libpq.so.5")]
        while pending:
            path = pending.pop()
            name = path.relative_to(stage).as_posix()
            if name in dependencies:
                continue
            dependencies[name] = inspect_binary(path, platform, args.objdump)
            for imported in dependencies[name]["imports"]:
                if platform == "windows" and not windows_system(imported):
                    if imported not in WINDOWS_RUNTIME:
                        raise ValueError(f"Unexpected Windows runtime: {imported}")
                    target = stage / imported
                    if not target.exists():
                        if not args.runtime_dir:
                            raise ValueError("Windows runtime directory is required")
                        copy(args.runtime_dir / imported, target)
                    pending.append(target)
                elif (
                    platform == "linux"
                    and imported != "libpq.so.5"
                    and imported not in LINUX_SYSTEM
                ):
                    raise ValueError(f"Unexpected Linux system runtime: {imported}")
        for binary, license, target in (
            ("libcrypto-3.dll", args.openssl_license, "licenses/openssl/LICENSE"),
            ("libwinpthread-1.dll", args.pthread_license, "licenses/libwinpthread/COPYING"),
        ):
            if (stage / binary).exists():
                if not license:
                    raise ValueError(f"License required for {binary}")
                copy(license, stage / target)
        files = {
            path.relative_to(stage).as_posix(): path.read_bytes()
            for path in stage.rglob("*")
            if path.is_file()
        }
        name = archive_name(metadata["version"], commit, platform)
        manifest = {
            "archive": name,
            "version": metadata["version"],
            "commit": commit,
            "platform": platform,
            "architecture": "x86",
            "source_epoch": epoch,
            "build": metadata,
            "dependencies": dependencies,
            "files": {name: digest(data) for name, data in files.items()},
        }
        if platform == "windows":
            manifest["libpq_build"] = json.loads(
                args.libpq.with_name("libpq-build-info.json").read_text(encoding="utf8")
            )
        validate_payload(files, manifest)
        files["manifest.json"] = json.dumps(manifest, indent=4, sort_keys=True).encode() + b"\n"
        archive = args.output / name
        timestamp = datetime.fromtimestamp(max(epoch, 315532800), timezone.utc).timetuple()[:6]
        with zipfile.ZipFile(
            archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9
        ) as package:
            for name, data in sorted(files.items()):
                entry = zipfile.ZipInfo(f"omp-postgresql/{name}", timestamp)
                entry.create_system = 3
                mode = (
                    0o755
                    if name.startswith(("components/", "lib/")) or name.endswith(".dll")
                    else 0o644
                )
                entry.external_attr = (stat.S_IFREG | mode) << 16
                package.writestr(entry, data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
        archive.with_suffix(".zip.sha256").write_text(
            f"{digest(archive.read_bytes())}  {archive.name}\n", encoding="utf8"
        )
        verify(archive)
        print(f"Verified package: {archive}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    record = commands.add_parser("record-libpq")
    record.add_argument("--libpq", type=Path, required=True)
    record.add_argument("--source", type=Path, required=True)
    assemble = commands.add_parser("create")
    assemble.add_argument("--platform", choices=("windows", "linux"), required=True)
    assemble.add_argument("--build", type=Path, required=True)
    assemble.add_argument("--libpq", type=Path, required=True)
    assemble.add_argument("--postgres-license", type=Path, required=True)
    assemble.add_argument("--runtime-dir", type=Path)
    assemble.add_argument("--openssl-license", type=Path)
    assemble.add_argument("--pthread-license", type=Path)
    assemble.add_argument("--objdump", default="objdump")
    assemble.add_argument("--output", type=Path, default=Path("build-dist"))
    check = commands.add_parser("verify")
    check.add_argument("--archive", type=Path, required=True)
    check.add_argument("--destination", type=Path)
    args = parser.parse_args()
    if args.command == "record-libpq":
        record_libpq_build(args.libpq.resolve(), args.source.resolve())
    elif args.command == "create":
        create(args)
    else:
        manifest = verify(args.archive, args.destination)
        print(f"Verified {manifest['platform']} x86 package from {manifest['commit']}")


if __name__ == "__main__":
    main()
