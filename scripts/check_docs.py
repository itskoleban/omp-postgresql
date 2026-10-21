"""Check local documentation links, API declarations and tutorial fixture inventory."""

import argparse
from pathlib import Path, PurePosixPath
import re
from urllib.parse import unquote, urlsplit

import check_doc_examples

ROOT = Path(__file__).resolve().parents[1]


def prose(text):
    # Share fence rules with compilation; examples can contain apparent Markdown links.
    return check_doc_examples.prose(text)


def anchors(text):
    found = set()
    counts = {}
    for heading in re.findall(r"^#{1,6}\s+(.+?)\s*#*\s*$", prose(text), re.MULTILINE):
        slug = re.sub(r"[^\w\- ]", "", heading.lower()).replace(" ", "-")
        number = counts.get(slug, 0)
        counts[slug] = number + 1
        found.add(f"{slug}-{number}" if number else slug)
    return found


def validate_links(files):
    errors = []
    for name, text in files.items():
        if not name.endswith(".md"):
            continue
        errors += check_doc_examples.scan_fences(text, name)[1]
        for target in re.findall(r"\]\(([^)\s]+)\)", prose(text)):
            url = urlsplit(target)
            if url.scheme or url.netloc:
                continue
            parts = (
                list(PurePosixPath(name).parent.parts)
                if url.path
                else list(PurePosixPath(name).parts)
            )
            for part in PurePosixPath(unquote(url.path)).parts:
                if part == "..":
                    if not parts:
                        errors.append(f"{name}: link escapes root: {target}")
                        break
                    parts.pop()
                elif part != ".":
                    parts.append(part)
            else:
                resolved = "/".join(parts)
                if resolved not in files:
                    errors.append(f"{name}: missing target: {target}")
                elif (
                    url.fragment
                    and resolved.endswith(".md")
                    and unquote(url.fragment) not in anchors(files[resolved])
                ):
                    errors.append(f"{name}: missing heading: {target}")
    return errors


def declarations(text, kind):
    return [
        re.sub(r"\s+", " ", declaration).strip()
        for declaration in re.findall(rf"^[ \t]*{kind}\s+[^;\n]+;", text, re.MULTILINE)
    ]


def validate_api(include, reference):
    errors = []
    for kind in ("native", "forward"):
        expected = declarations(include, kind)
        actual = declarations(reference, kind)
        if actual != expected:
            # Order of the reference groups is allowed to differ, duplicates are not.
            if sorted(actual) != sorted(expected):
                errors.append(f"{kind} declarations differ between include and native reference")
    return errors


def check(root):
    # Limit reads to public install scopes; never traverse build directories or secrets.
    paths = [root / name for name in ("README.md", "DEPENDENCIES.md", "NOTICE.md", "LICENSE")]
    paths += list((root / "docs").glob("*.md"))
    paths += [p for p in (root / "examples").rglob("*") if p.suffix in {".pwn", ".sql", ".conf"}]
    paths += [root / "pawn/omp-postgresql.inc", root / "qawno/include/omp-postgresql.inc"]
    files = {
        p.relative_to(root).as_posix(): p.read_text(encoding="utf8") for p in paths if p.is_file()
    }
    errors = validate_links(files)
    include = files.get(
        "pawn/omp-postgresql.inc", files.get("qawno/include/omp-postgresql.inc", "")
    )
    if not include:
        errors.append("Missing public include")
    errors += validate_api(include, files.get("docs/native-reference.md", ""))
    try:
        check_doc_examples.load(root)
    except ValueError as error:
        errors.append(str(error))
    catalog = files.get("docs/examples.md", "")
    for name in files:
        if (
            name.startswith("examples/")
            and name.endswith((".pwn", ".sql"))
            and f"../{name}" not in catalog
        ):
            errors.append(f"Tutorial inventory omits {name}")
    if errors:
        raise ValueError("\n".join(errors))
    print(
        f"Documentation checked: {len(declarations(include, 'native'))} natives, "
        f"{len(declarations(include, 'forward'))} callbacks, links and example/SQL inventory"
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    check(parser.parse_args().root.resolve())
