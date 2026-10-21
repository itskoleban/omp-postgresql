"""Classify public Markdown Pawn fences and compile verbatim examples with real includes."""

import argparse
from collections import Counter
from dataclasses import dataclass
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "tests/doc_examples.json"
PRELUDE = "#include <open.mp>\n#include <omp-postgresql>\n"
DECLARATION = re.compile(r"^(?:native|forward)\s+[^;\n]+;\s*$")
MARKER = re.compile(r"^\s*\{\{([a-z][a-z0-9.-]*)\}\}\s*$")


@dataclass
class Fence:
    path: str
    line: int  # Opening fence; first code line is line + 1.
    end: int
    language: str
    code: str
    annotation: str

    @property
    def location(self):
        return f"{self.path}:{self.line}"


def scan_fences(text, path="<markdown>"):
    """Recognize CommonMark backtick/tilde fences without interpreting Pawn syntax."""
    lines = text.splitlines(keepends=True)
    fences, errors = [], []
    index = 0
    while index < len(lines):
        opening = re.match(r"^ {0,3}(`{3,}|~{3,})([^\r\n]*)", lines[index])
        if not opening and re.match(
            r"^\s*(?:>\s*)*(?:`{3,}|~{3,})(?:pawn|pwn)\b", lines[index], re.I
        ):
            errors.append(
                f"{path}:{index + 1}: put Pawn fences outside indented/list/quote containers for validation"
            )
        if not opening or (opening[1][0] == "`" and "`" in opening[2]):
            index += 1
            continue
        start, token = index, opening[1]
        previous = start - 1
        while previous >= 0 and not lines[previous].strip():
            previous -= 1
        annotation = lines[previous].strip() if previous >= 0 else ""
        index += 1
        while index < len(lines) and not re.fullmatch(
            rf" {{0,3}}{re.escape(token[0])}{{{len(token)},}}[ \t]*[\r\n]*", lines[index]
        ):
            index += 1
        if index == len(lines):
            errors.append(f"{path}:{start + 1}: unclosed fenced code block")
        language = opening[2].strip().split(maxsplit=1)
        fences.append(
            Fence(
                path,
                start + 1,
                min(index + 1, len(lines)),
                language[0].lower() if language else "",
                "".join(lines[start + 1 : index]),
                annotation,
            )
        )
        index += 1
    return fences, errors


def prose(text):
    lines = text.splitlines(keepends=True)
    for fence in scan_fences(text)[0]:
        lines[fence.line - 1 : fence.end] = ["\n"] * (fence.end - fence.line + 1)
    return "".join(lines)


def public_markdown(root):
    # Same public scopes as installation; internal audit/build directories are not tutorials.
    paths = list(root.glob("*.md")) + list((root / "docs").glob("*.md"))
    paths += list((root / "scripts/patches").glob("*.md"))
    paths += list((root / "share/omp-postgresql/patches").glob("*.md"))
    return {p.relative_to(root).as_posix(): p.read_text(encoding="utf8") for p in sorted(paths)}


def canonical(text):
    return re.sub(r"\s+", " ", text).strip()


def declarations(include):
    return [line.strip() for line in include.splitlines() if DECLARATION.fullmatch(line.strip())]


def catalog(files, include):
    examples, reference, fragments, sql = {}, [], [], {}
    allowed = {canonical(line) for line in declarations(include)}
    errors = []
    for path, text in files.items():
        fences, malformed = scan_fences(text, path)
        errors.extend(malformed)
        for fence in fences:
            kind = re.fullmatch(
                r"<!-- (pawn|sql)-example: ([a-z][a-z0-9.-]*) -->", fence.annotation
            )
            fragment = re.fullmatch(r"<!-- pawn-fragment: (.+) -->", fence.annotation)
            if fence.language == "sql" and kind and kind[1] == "sql":
                if kind[2] in sql:
                    errors.append(f"{fence.location}: duplicate SQL example ID {kind[2]}")
                sql[kind[2]] = fence
            if fence.language not in {"pawn", "pwn"}:
                if (kind and kind[1] == "pawn") or fragment:
                    errors.append(f"{fence.location}: Pawn annotation requires a Pawn fence")
                continue
            if kind and kind[1] == "pawn":
                if kind[2] in examples:
                    errors.append(f"{fence.location}: duplicate Pawn example ID {kind[2]}")
                examples[kind[2]] = fence
            elif fragment:
                if len(fragment[1].strip()) < 12:
                    errors.append(f"{fence.location}: explain why this is an incomplete fragment")
                fragments.append(fence)
            elif fence.code.strip() and all(
                DECLARATION.fullmatch(line.strip())
                for line in fence.code.splitlines()
                if line.strip()
            ):
                if any(
                    canonical(line) not in allowed
                    for line in fence.code.splitlines()
                    if line.strip()
                ):
                    errors.append(f"{fence.location}: declaration differs from the public include")
                reference.append(fence)
            else:
                errors.append(
                    f"{fence.location}: unclassified Pawn fence; add pawn-example or pawn-fragment"
                )
    if errors:
        raise ValueError("\n".join(errors))
    return examples, reference, fragments, sql


def include_text(root):
    for name in ("pawn/omp-postgresql.inc", "qawno/include/omp-postgresql.inc"):
        if (root / name).is_file():
            return (root / name).read_text(encoding="utf8")
    raise ValueError("Missing public include")


def load(root, manifest=MANIFEST):
    include = include_text(root)
    examples, reference, fragments, sql = catalog(public_markdown(root), include)
    wrappers = json.loads(manifest.read_text(encoding="utf8"))
    used = Counter()
    names = set()
    for wrapper in wrappers:
        if not re.fullmatch(r"[a-z][a-z0-9-]*", wrapper["name"]) or wrapper["name"] in names:
            raise ValueError("Invalid or duplicate wrapper name")
        names.add(wrapper["name"])
        for line in wrapper["lines"]:
            if match := MARKER.fullmatch(line):
                used[match[1]] += 1
    if set(used) != set(examples) or any(count != 1 for count in used.values()):
        missing = [
            f"{name} ({examples[name].location})" for name in sorted(set(examples) - set(used))
        ]
        raise ValueError(
            f"Wrapper coverage differs: missing={missing}, "
            f"unknown={sorted(set(used)-set(examples))}, "
            f"repeated={sorted(name for name, count in used.items() if count != 1)}"
        )
    return include, examples, reference, fragments, sql, wrappers


def split_arguments(text):
    """Split balanced call/signature arguments; leave expressions for the Pawn compiler."""
    # Comments may contain commas and parentheses but cannot split a Pawn argument.
    text = re.sub(
        r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/',
        lambda m: m[0] if m[0][0] in "\"'" else " " * len(m[0]),
        text,
        flags=re.S,
    )
    result, start, depth, quoted, escape = [], 0, 0, None, False
    for index, char in enumerate(text):
        if quoted:
            if escape:
                escape = False
            elif char == "\\":
                escape = True
            elif char == quoted:
                quoted = None
        elif char in "\"'":
            quoted = char
        elif char in "([{":
            depth += 1
        elif char in ")]}":
            depth -= 1
        elif char == "," and depth == 0:
            result.append(text[start:index].strip())
            start = index + 1
    result.append(text[start:].strip())
    return result


def callback_checks(source, include):
    """Make literal custom handlers compiler-visible; dynamic routing still needs runtime tests."""
    signatures = {}
    for declaration in declarations(include):
        match = re.search(r"\b(pg_\w+)\((.*)\);", declaration)
        if match and "const callback[]" in match[2]:
            fields = split_arguments(match[2])
            signatures[match[1]] = [
                re.search(r"(\w+)\s*(?:\[\])?\s*(?:=|$)", f)[1] if f != "{Float,_}:..." else "..."
                for f in fields
            ]
    checks = []
    # Strings/comments cannot introduce calls into the lexer: mask them while retaining offsets.
    masked = re.sub(
        r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/',
        lambda m: " " * len(m[0]),
        source,
        flags=re.S,
    )
    for call in re.finditer(r"\b(pg_\w+)\s*\(", masked):
        if call[1] not in signatures:
            continue
        depth, end = 1, call.end()
        while end < len(masked) and depth:
            depth += (masked[end] == "(") - (masked[end] == ")")
            end += 1
        if depth:
            continue  # The actual compiler diagnoses incomplete calls.
        args = split_arguments(source[call.end() : end - 1])
        fields, values = signatures[call[1]], {}
        extras = []
        for index, arg in enumerate(args):
            if named := re.fullmatch(r"\.(\w+)\s*=\s*(.*)", arg, re.S):
                values[named[1]] = named[2].strip()
            elif index < len(fields) - 1:
                values[fields[index]] = arg
            else:
                extras.append(arg)
        callback = re.fullmatch(r'"([A-Za-z_]\w*)"', values.get("callback", '""'))
        format_ = re.fullmatch(r'"([^"\\]*)"', values.get("format", '""'))
        if not callback or not format_ or values.get("notify", "true") not in {"true", "1"}:
            continue
        if re.search(r"[^ifbs]", format_[1]) or len(format_[1]) != len(extras):
            raise ValueError(
                f"line {source[:call.start()].count(chr(10))+1}: invalid literal callback format/argument count"
            )
        types = {"i": "0", "f": "0.0", "b": "false", "s": '""'}
        checks.append(
            (
                source[: call.start()].count("\n") + 1,
                f"{callback[1]}(0{''.join(', ' + types[c] for c in format_[1])});",
            )
        )
    return checks


def units(root, manifest=MANIFEST):
    include, examples, reference, fragments, _, wrappers = load(root, manifest)
    result = []
    for wrapper in wrappers:
        lines, locations = PRELUDE.splitlines(), [None, None]
        for line in wrapper["lines"]:
            if match := MARKER.fullmatch(line):
                fence = examples[match[1]]
                lines.extend(fence.code.splitlines())
                locations.extend(
                    (fence.path, fence.line + offset + 1)
                    for offset, _ in enumerate(fence.code.splitlines())
                )
            else:
                lines.append(line)
                locations.append(None)
        source = "\n".join(lines) + "\n"
        try:
            checks = callback_checks(source, include)
        except ValueError as error:
            match = re.match(r"line (\d+): (.*)", str(error))
            location = locations[int(match[1]) - 1] if match else None
            raise ValueError(
                f"{location[0]}:{location[1]}: {match[2]}" if location else str(error)
            ) from error
        if checks:
            lines += ["forward __DocCheckCallbacks();", "public __DocCheckCallbacks() {"]
            locations += [None, None]
            for line, statement in checks:
                lines.append(statement)
                locations.append(locations[line - 1])
            lines += ["return 1;", "}"]
            locations += [None, None]
        result.append((wrapper["name"], "\n".join(lines) + "\n", locations))
    if reference:
        constants = "\n".join(
            line for line in include.splitlines() if not DECLARATION.fullmatch(line.strip())
        )
        lines = ("#include <open.mp>\n" + constants + "\nmain() {}\n").splitlines()
        locations = [None] * len(lines)
        for fence in reference:
            lines.extend(fence.code.splitlines())
            locations.extend(
                (fence.path, fence.line + offset + 1)
                for offset, _ in enumerate(fence.code.splitlines())
            )
        result.append(("native-reference", "\n".join(lines) + "\n", locations))
    return result, {
        "runnable": len(examples),
        "declaration_fences": len(reference),
        "fragments": [(f.path, f.line, f.annotation) for f in fragments],
    }


def compile_examples(compiler, root, includes, output, manifest=MANIFEST):
    planned, coverage = units(root, manifest)
    output.mkdir(parents=True, exist_ok=True)
    for name, source, locations in planned:
        path = (output / f"{name}.pwn").resolve()
        path.write_text(source, encoding="utf8", newline="\n")
        result = subprocess.run(
            [
                str(compiler.resolve()),
                str(path),
                "-E",
                *(f"-i{p.resolve()}" for p in includes),
                f"-o{(output / (name + '.amx')).resolve()}",
            ],
            capture_output=True,
            text=True,
        )
        log = result.stdout + result.stderr
        (output / f"{name}.log").write_text(log, encoding="utf8")
        if result.returncode:
            messages = []
            for line in log.splitlines():
                match = re.match(r"(.*?)\((\d+)\)\s*:\s*(.*)", line)
                index = int(match[2]) - 1 if match and Path(match[1]).name == path.name else -1
                location = locations[index] if 0 <= index < len(locations) else None
                messages.append(f"{location[0]}:{location[1]}: {match[3]}" if location else line)
            raise ValueError(f"Documentation wrapper {name} failed:\n" + "\n".join(messages))
    print(
        f"Compiled {coverage['runnable']} Markdown Pawn examples and "
        f"{coverage['declaration_fences']} declaration fences; "
        f"{len(coverage['fragments'])} explicitly incomplete fragment(s). Compilation is not runtime validation."
    )
    for path, line, reason in coverage["fragments"]:
        print(f"{path}:{line}: incomplete fragment: {reason}")
    return coverage


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--include", type=Path, action="append", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    compile_examples(args.compiler, args.root, args.include, args.output)
