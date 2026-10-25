"""Retain source, toolchain, commands and instrumentation for a Linux TSan build."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import sys
from datetime import datetime, timezone


class EvidenceError(RuntimeError):
    pass


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def sanitize(text, source, build):
    prefixes = [(str(build), "<BUILD>"), (str(source), "<SOURCE>")]
    home = os.environ.get("HOME")
    if home and home != "/":
        prefixes.append((home, "<HOME>"))
    for prefix, replacement in sorted(prefixes, key=lambda pair: len(pair[0]), reverse=True):
        text = text.replace(prefix, replacement)
    return text


def compiler_argv(command, compiler):
    """Unwrap Ninja's shell no-op/link separators without executing shell text."""
    tokens = shlex.split(command)
    try:
        start = tokens.index(compiler)
    except ValueError as error:
        raise EvidenceError("Ninja command does not contain the configured compiler") from error
    end = tokens.index("&&", start) if "&&" in tokens[start:] else len(tokens)
    return tokens[start:end]


def require_instrumentation(symbols, race):
    undefined = set(re.findall(r"^\s*U\s+(\S+)\s*$", symbols, re.MULTILINE))
    required = {"__tsan_init"}
    if race:
        required.update({"__tsan_write4", "__tsan_atomic32_load", "__tsan_atomic32_store"})
    missing = sorted(required - undefined)
    if missing:
        raise EvidenceError("Missing required TSan instrumentation: " + ", ".join(missing))
    return sorted(required)


class Recorder:
    def __init__(self, source, build):
        self.source, self.build = source, build
        self.output = build / "tsan-evidence"
        self.output.mkdir(parents=True, exist_ok=True)
        self.manifest = {
            "schema": 1,
            "status": "FAIL",
            "started_utc": datetime.now(timezone.utc).isoformat(),
            "commands": [],
            "inputs": [],
            "targets": [],
            "limitations": [
                "This records the current build; unavailable historical binaries are not reconstructed identities.",
                "Copied binaries and objects retain their original bytes, including compiler debug information.",
                "Original command stream hashes identify pre-sanitization bytes; public captures have separate hashes.",
            ],
        }

    def public(self, text):
        return sanitize(text, self.source, self.build)

    def command(self, label, argv, cwd=None, required=True):
        command_dir = self.output / "commands"
        command_dir.mkdir(exist_ok=True)
        try:
            result = subprocess.run(argv, cwd=cwd or self.source, capture_output=True, timeout=30)
        except subprocess.TimeoutExpired as error:
            result = subprocess.CompletedProcess(argv, None, error.stdout or b"", error.stderr or b"")
        except OSError as error:
            result = subprocess.CompletedProcess(argv, None, b"", str(error).encode("utf-8"))
        record = {
            "label": label,
            "argv": [self.public(str(arg)) for arg in argv],
            "original_argv_sha256": sha256(json.dumps([str(arg) for arg in argv]).encode("utf-8")),
            "cwd": self.public(str(cwd or self.source)),
            "returncode": result.returncode,
            "required": required,
        }
        for name, raw in (("stdout", result.stdout), ("stderr", result.stderr)):
            public = self.public(raw.decode("utf-8", errors="backslashreplace")).encode("utf-8")
            relative = Path("commands") / f"{label}.{name}.txt"
            (self.output / relative).write_bytes(public)
            record[name] = {
                "original_sha256": sha256(raw),
                "original_bytes": len(raw),
                "sanitized_sha256": sha256(public),
                "sanitized_bytes": len(public),
                "sanitized_path": relative.as_posix(),
            }
        self.manifest["commands"].append(record)
        if required and result.returncode != 0:
            raise EvidenceError(f"Required evidence command failed: {label}; returncode={result.returncode}")
        return result.stdout.decode("utf-8", errors="backslashreplace")

    def copy_input(self, path, relative, role):
        data = path.read_bytes()
        destination = self.output / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, destination)
        record = {
            "role": role,
            "input_path": self.public(str(path)),
            "copy_path": Path(relative).as_posix(),
            "sha256": sha256(data),
            "bytes": len(data),
            "copy_is_byte_identical": destination.read_bytes() == data,
        }
        self.manifest["inputs"].append(record)
        return record

    def run(self):
        if platform.system() != "Linux":
            raise EvidenceError("TSan build evidence requires the evaluated Linux build environment")
        self.manifest["source_sha"] = self.command("source-sha", ["git", "rev-parse", "HEAD"]).strip()
        tracked = self.command("tracked-status", ["git", "status", "--porcelain=v1", "--untracked-files=no"])
        self.manifest["tracked_source_modified"] = bool(tracked.strip())
        self.manifest["platform"] = self.public(platform.platform())
        self.manifest["uname"] = list(platform.uname())
        self.manifest["libc"] = list(platform.libc_ver())
        self.manifest["environment"] = {
            key: self.public(os.environ[key])
            for key in ("CC", "CXX", "TSAN_OPTIONS", "GITHUB_SHA", "GITHUB_RUN_ID", "GITHUB_RUN_ATTEMPT")
            if key in os.environ
        }
        try:
            self.manifest["mmap_rnd_bits"] = Path("/proc/sys/vm/mmap_rnd_bits").read_text().strip()
        except OSError as error:
            self.manifest["mmap_rnd_bits"] = f"UNAVAILABLE errno={error.errno}"
        cache = {}
        for line in (self.build / "CMakeCache.txt").read_text().splitlines():
            match = re.match(r"([^/#][^:]*):[^=]*=(.*)", line)
            if match:
                cache[match[1]] = match[2]
        compiler = cache.get("CMAKE_CXX_COMPILER")
        linker = cache.get("CMAKE_LINKER")
        ninja = cache.get("CMAKE_MAKE_PROGRAM", "ninja")
        if not compiler or not linker:
            raise EvidenceError("CMake cache lacks configured compiler or linker")
        self.manifest["cmake_configuration"] = {
            key: self.public(value) for key, value in cache.items()
            if key.startswith(("CMAKE_CXX_FLAGS", "CMAKE_EXE_LINKER_FLAGS"))
            or key in ("CMAKE_BUILD_TYPE", "CMAKE_CXX_COMPILER", "CMAKE_LINKER", "PG_THREAD_SANITIZE")
        }
        self.command("compiler-version", [compiler, "--version"])
        self.command("linker-version", [linker, "--version"])
        self.command("ninja-version", [ninja, "--version"])
        self.command("libc-version", ["getconf", "GNU_LIBC_VERSION"], required=False)
        self.command("dynamic-loader-version", ["ldd", "--version"], required=False)
        self.command("package-versions", ["dpkg-query", "-W", "-f=${binary:Package}\t${Version}\n",
                                        "clang*", "libclang-rt*", "libllvm*", "libc6", "libstdc++6", "binutils"],
                     required=False)
        self.command("nm-version", ["nm", "--version"])
        self.command("objdump-version", ["objdump", "--version"])
        runtime_archives = []
        for library in ("libclang_rt.tsan-x86_64.a", "libclang_rt.tsan_cxx-x86_64.a", "libtsan.a"):
            location = self.command("runtime-" + library.replace(".", "-"),
                                    [compiler, "-print-file-name=" + library], required=False).strip()
            path = Path(location)
            if path.is_absolute() and path.is_file():
                data = path.read_bytes()
                runtime_archives.append({"path": self.public(location), "sha256": sha256(data), "bytes": len(data)})
                self.command("runtime-package-" + library.replace(".", "-"),
                             ["dpkg-query", "-S", location], required=False)
        self.manifest["runtime_archives"] = runtime_archives
        if not runtime_archives:
            raise EvidenceError("Compiler did not identify an available TSan runtime archive")
        build_rules = (self.build / "build.ninja").read_text()
        targets = ["pg_tsan_race_control", "pg_tsan_startup_control"]
        if re.search(r"^build pg_tsan_ordered_control:", build_rules, re.MULTILINE):
            targets.append("pg_tsan_ordered_control")
        source_files = {Path("CMakeLists.txt"), Path(".github/workflows/ci.yml"), Path("tests/check_tsan_control.py")}
        for target in targets:
            commands = self.command("ninja-" + target, [ninja, "-C", str(self.build), "-t", "commands", target])
            invocations = [compiler_argv(line, compiler) for line in commands.splitlines() if line.strip()]
            compilation = [argv for argv in invocations if "-c" in argv]
            linkage = [argv for argv in invocations if "-c" not in argv]
            if len(compilation) != 1 or len(linkage) != 1:
                raise EvidenceError("Expected one fixture compilation and one link command: " + target)
            if any("-fsanitize=thread" not in argv for argv in (compilation[0], linkage[0])):
                raise EvidenceError("Fixture compilation and linkage must both enable ThreadSanitizer: " + target)
            compile_args = compilation[0]
            object_path = Path(compile_args[compile_args.index("-o") + 1])
            if not object_path.is_absolute():
                object_path = self.build / object_path
            fixture_source = Path(compile_args[compile_args.index("-c") + 1])
            if not fixture_source.is_absolute():
                fixture_source = self.build / fixture_source
            source_files.add(fixture_source.resolve().relative_to(self.source))
            binary = self.build / target
            self.copy_input(binary, Path("binaries") / target, "executable")
            self.copy_input(object_path, Path("objects") / target / object_path.name, "instrumented-object")
            symbols = self.command("nm-" + target, ["nm", "-u", str(object_path)])
            required_symbols = require_instrumentation(symbols, target != "pg_tsan_startup_control")
            self.command("disassembly-" + target, ["objdump", "-dr", str(object_path)])
            self.command("elf-" + target, ["readelf", "-h", "-n", "-d", str(binary)])
            self.command("driver-link-" + target, [compiler, "-###", *linkage[0][1:]], cwd=self.build)
            self.manifest["targets"].append({
                "target": target,
                "compile_argv": [self.public(arg) for arg in compile_args],
                "link_argv": [self.public(arg) for arg in linkage[0]],
                "required_undefined_symbols": required_symbols,
            })
        for relative in sorted(source_files):
            self.copy_input(self.source / relative, Path("source") / relative, "source-input")
        if tracked.strip():
            raise EvidenceError("Tracked source modifications prevent exact-commit build qualification")
        self.manifest["status"] = "PASS"

    def finish(self):
        self.manifest["finished_utc"] = datetime.now(timezone.utc).isoformat()
        self.manifest["public_files"] = {
            path.relative_to(self.output).as_posix(): {"sha256": sha256(path.read_bytes()), "bytes": path.stat().st_size}
            for path in sorted(self.output.rglob("*")) if path.is_file() and path.name != "manifest.json"
        }
        (self.output / "manifest.json").write_text(json.dumps(self.manifest, indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=Path("build"))
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    recorder = Recorder(args.source.resolve(), args.build.resolve())
    try:
        recorder.run()
    except (EvidenceError, OSError, ValueError) as error:
        recorder.manifest["error"] = recorder.public(str(error))
        print("TSan build evidence: FAIL: " + recorder.public(str(error)), file=sys.stderr)
    finally:
        recorder.finish()
    if recorder.manifest["status"] != "PASS":
        return 1
    print("TSan build evidence: PASS; manifest=<BUILD>/tsan-evidence/manifest.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
