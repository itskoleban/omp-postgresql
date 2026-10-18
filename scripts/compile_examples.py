"""Compile every installed/source Pawn example with warnings as errors."""

import argparse
from pathlib import Path
import subprocess


def compile_examples(compiler, root, includes, output, prefix=""):
    scripts = sorted((root / "examples").glob("*.pwn"))
    if not scripts:
        raise ValueError("No Pawn examples found")
    output.mkdir(parents=True, exist_ok=True)
    for script in scripts:
        subprocess.run(
            [
                str(compiler.resolve()),
                str(script.resolve()),
                "-E",
                *(f"-i{path.resolve()}" for path in includes),
                f"-o{(output / (prefix + script.stem + '.amx')).resolve()}",
            ],
            check=True,
        )
    print(f"Compiled {len(scripts)} examples with warnings treated as errors", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--include", type=Path, action="append", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--prefix", default="")
    args = parser.parse_args()
    compile_examples(args.compiler, args.root, args.include, args.output, args.prefix)
