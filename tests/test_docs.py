"""Documentation contracts tested with both real source and adversarial small fixtures."""

import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import sys

sys.path.insert(0, str(Path(__file__).parents[1] / "scripts"))


def module(name):
    spec = importlib.util.spec_from_file_location(
        name, Path(__file__).parents[1] / f"scripts/{name}.py"
    )
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


docs = module("check_docs")
compiler = module("compile_examples")


class DocumentationTests(unittest.TestCase):
    def test_repository_contract(self):
        docs.check(docs.ROOT)

    def test_changed_defaults_tags_callbacks_and_duplicate_declarations(self):
        include = "native bool:pg_test(job, count = 1);\nforward OnPgTest(job, context);"
        self.assertEqual(docs.validate_api(include, include), [])
        for mutation in (
            include.replace("= 1", "= 2"),
            include.replace("bool:", ""),
            include.replace("job, context", "context, job"),
            include + "\nnative bool:pg_test(job, count = 1);",
        ):
            with self.subTest(mutation=mutation):
                self.assertTrue(docs.validate_api(include, mutation))

    def test_links_fragments_fences_and_parent_paths(self):
        files = {
            "README.md": "[guide](docs/guide.md#job-lifetime)",
            "docs/guide.md": "# Job lifetime\n[back](../README.md)\n```pawn\n// [fake](missing.md)\n```\n",
        }
        self.assertEqual(docs.validate_links(files), [])
        for link in ("../private.md", "docs/missing.md", "docs/guide.md#wrong"):
            with self.subTest(link=link):
                self.assertTrue(docs.validate_links({**files, "README.md": f"[broken]({link})"}))

        self.assertTrue(docs.validate_links({"README.md": "# Title\n```pawn\nmain() {}"}))

    def test_discovery_compiles_all_examples_and_propagates_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "examples").mkdir()
            for name in ("first", "newly-added"):
                (root / f"examples/{name}.pwn").write_text("main() {}", encoding="utf8")
            with patch.object(compiler.subprocess, "run") as run:
                compiler.compile_examples(root / "pawncc", root, [root / "pawn"], root / "amx")
                self.assertEqual(run.call_count, 2)
                self.assertTrue(
                    all(
                        "-E" in call.args[0] and call.kwargs["check"] for call in run.call_args_list
                    )
                )
            with patch.object(
                compiler.subprocess, "run", side_effect=RuntimeError("compile failed")
            ):
                with self.assertRaisesRegex(RuntimeError, "compile failed"):
                    compiler.compile_examples(root / "pawncc", root, [root], root / "amx")


if __name__ == "__main__":
    unittest.main()
