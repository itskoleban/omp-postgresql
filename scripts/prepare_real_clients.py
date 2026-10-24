"""Build the pinned, pipe-adapted ordinary-protocol client in a NEW directory.

Linux: GCC, make, CMake, Ninja, Git. Windows: MSYS2 MINGW32 GCC/make/bash,
CMake and Ninja. No game installation and no precompiled client/LuaJIT needed.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

CLIENT = "f75af4cb97fed341cb45b49aff489f9f20665e99"
LUAJIT = "a4c064539ea931274d407e1249b4bb575424a0f4"


def run(argv, **kwargs):
    subprocess.run([str(a) for a in argv], check=True, **kwargs)


def checkout(url, revision, directory):
    run(["git", "-c", "core.autocrlf=false", "clone", "--no-checkout", url, directory])
    run(["git", "-C", directory, "-c", "core.autocrlf=false", "checkout", revision])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mingw-bin", type=Path)
    parser.add_argument("--bash", default="bash")
    args = parser.parse_args()
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=False)
    source, build = root / "source", root / "build"
    checkout("https://github.com/pavetr1337/RakSAMP-cli.git", CLIENT, source)
    patch = Path(__file__).parents[1] / "tests/fixtures/rakclient-pipe.patch"
    run(["git", "-C", source, "apply", "--check", patch])
    run(["git", "-C", source, "apply", patch])
    checkout("https://github.com/LuaJIT/LuaJIT.git", LUAJIT, source / "deps/luajit")
    dist = build / "luajit"
    dist.mkdir(parents=True)
    env = os.environ.copy()
    options = []
    if os.name == "nt":
        if not args.mingw_bin:
            parser.error("Windows requires --mingw-bin (32-bit GCC)")
        mingw = args.mingw_bin.resolve()
        env["PATH"] = str(mingw) + os.pathsep + env["PATH"]
        run([args.bash, "-c", "export PATH=/mingw32/bin:/usr/bin; make -j4 BUILDMODE=dynamic TARGET_SYS=Windows"], cwd=source / "deps/luajit", env=env)
        shutil.copy2(source / "deps/luajit/src/lua51.dll", dist / "lua51.dll")
        shutil.copy2(source / "deps/luajit/src/libluajit-5.1.dll.a", dist / "liblua51.dll.a")
        options = [f"-DCMAKE_C_COMPILER={(mingw / 'gcc.exe').as_posix()}", f"-DCMAKE_CXX_COMPILER={(mingw / 'g++.exe').as_posix()}"]
    else:
        run(["bash", source / "tools/build_luajit.sh", "linux", dist], env=env)
    run(["cmake", "-S", source, "-B", build, "-G", "Ninja", *options], env=env)
    run(["cmake", "--build", build, "--target", "RakSAMPClient", "--parallel", "4"], env=env)
    if os.name == "nt":
        for name in ("libwinpthread-1.dll", "libgcc_s_dw2-1.dll"):
            shutil.copy2(mingw / name, build / name)
    binary = build / ("RakSAMPClient.exe" if os.name == "nt" else "RakSAMPClient")
    manifest = {"client_commit": CLIENT, "luajit_commit": LUAJIT,
                "patch_sha256": hashlib.sha256(patch.read_bytes()).hexdigest(),
                "binary": str(binary), "sha256": hashlib.sha256(binary.read_bytes()).hexdigest()}
    (root / "client-build.json").write_text(json.dumps(manifest, indent=2)+"\n", encoding="utf8")
    print(json.dumps(manifest), flush=True)


if __name__ == "__main__":
    main()
