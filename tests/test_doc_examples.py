"""Regression checks for complete snippet coverage, real-compiler inputs and source diagnostics."""

import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).parents[1] / "scripts"))
import check_doc_examples as snippets
import check_doc_sql as sql

ROOT = Path(__file__).parents[1]
INCLUDE = (ROOT / "pawn/omp-postgresql.inc").read_text(encoding="utf8")


class SnippetTests(unittest.TestCase):
    def example(self, code="new value;", id="sample"):
        return f"# Guide\n<!-- pawn-example: {id} -->\n```pawn\n{code}\n```\n"

    def fixture(self, directory, text=None, wrappers=None):
        root = Path(directory)
        (root / "docs").mkdir()
        (root / "pawn").mkdir()
        (root / "pawn/omp-postgresql.inc").write_text(INCLUDE, encoding="utf8")
        (root / "docs/guide.md").write_text(text or self.example(), encoding="utf8")
        manifest = root / "wrappers.json"
        manifest.write_text(
            json.dumps(
                wrappers
                if wrappers is not None
                else [{"name": "sample", "lines": ["main() {", "{{sample}}", "}"]}]
            ),
            encoding="utf8",
        )
        return root, manifest

    def test_every_repository_fence_is_classified_and_every_example_is_used_once(self):
        _, examples, reference, fragments, selected_sql, wrappers = snippets.load(ROOT)
        planned, coverage = snippets.units(ROOT)
        self.assertEqual(coverage["runnable"], len(examples))
        self.assertEqual(coverage["declaration_fences"], len(reference))
        self.assertTrue(all(f.annotation.startswith("<!-- pawn-fragment:") for f in fragments))
        self.assertEqual(set(selected_sql), {"crud.schema", "save.sql", "section.file"})
        declared = sum(len(snippets.declarations(f.code)) for f in reference)
        self.assertEqual(declared, len(snippets.declarations(INCLUDE)))
        self.assertEqual(len(planned), len(wrappers) + bool(reference))

    def test_backtick_tilde_longer_fences_and_original_line_numbers(self):
        text = "# Header\n<!-- pawn-example: one -->\n````Pawn\nnew a;\n```\n````\n  ~~~pwn\nnew b;\n  ~~~\n"
        fences, errors = snippets.scan_fences(text, "docs/a.md")
        self.assertEqual(errors, [])
        self.assertEqual([(f.line, f.language) for f in fences], [(3, "pawn"), (7, "pwn")])
        self.assertEqual(fences[0].code, "new a;\n```\n")
        self.assertEqual(snippets.prose(text).count("\n"), text.count("\n"))
        self.assertNotIn("new a", snippets.prose(text))

    def test_unclosed_fence_and_mismatched_closer_are_reported(self):
        fences, errors = snippets.scan_fences("```pawn\nnew a;\n~~~\n", "docs/a.md")
        self.assertEqual(len(fences), 1)
        self.assertIn("docs/a.md:1: unclosed", errors[0])

    def test_nested_pawn_fences_cannot_silently_escape_discovery(self):
        for opening in ("    ```pawn", "> ~~~Pawn", "  > ```pwn"):
            with self.subTest(opening=opening):
                _, errors = snippets.scan_fences(opening + "\nnew a;\n", "docs/a.md")
                self.assertIn("docs/a.md:1: put Pawn fences outside", errors[0])

    def test_unclassified_new_fences_cannot_escape_validation(self):
        for language in ("pawn", "pwn", "Pawn"):
            with self.subTest(language=language), self.assertRaisesRegex(
                ValueError, "docs/a.md:1: unclassified"
            ):
                snippets.catalog({"docs/a.md": f'```{language}\npg_connect("game");\n```'}, INCLUDE)

    def test_duplicate_ids_wrong_language_and_empty_fragment_reason_fail(self):
        for text, message in (
            (self.example() * 2, "duplicate Pawn example"),
            (self.example().replace("```pawn", "```sql"), "requires a Pawn fence"),
            ("<!-- pawn-fragment: no -->\n```pawn\n...\n```", "explain why"),
        ):
            with self.subTest(message=message), self.assertRaisesRegex(ValueError, message):
                snippets.catalog({"docs/a.md": text}, INCLUDE)

    def test_incomplete_syntax_needs_an_explicit_reported_reason(self):
        text = "<!-- pawn-fragment: Generic callback tail is not a full declaration. -->\n```pawn\n...\n```"
        examples, reference, fragments, _ = snippets.catalog({"docs/a.md": text}, INCLUDE)
        self.assertEqual((examples, reference), ({}, []))
        self.assertEqual(len(fragments), 1)

    def test_reference_declarations_must_match_actual_names_tags_and_defaults(self):
        original = "native pg_connect(const profile[], pool_size = 1);"
        self.assertEqual(
            len(snippets.catalog({"a.md": f"```pawn\n{original}\n```"}, INCLUDE)[1]), 1
        )
        for changed in (
            original.replace("= 1", "= 2"),
            original.replace("pg_connect", "pg_unknown"),
        ):
            with self.subTest(changed=changed), self.assertRaisesRegex(
                ValueError, "declaration differs"
            ):
                snippets.catalog({"a.md": f"```pawn\n{changed}\n```"}, INCLUDE)

    def test_missing_stale_repeated_or_unsafe_wrappers_are_rejected(self):
        for lines, message in (
            ([], "Wrapper coverage"),
            (["{{unknown}}"], "Wrapper coverage"),
            (["{{sample}}", "{{sample}}"], "Wrapper coverage"),
        ):
            with self.subTest(lines=lines), tempfile.TemporaryDirectory() as directory:
                root, manifest = self.fixture(
                    directory, wrappers=[{"name": "unit", "lines": lines}]
                )
                with self.assertRaisesRegex(ValueError, message):
                    snippets.load(root, manifest)
        with tempfile.TemporaryDirectory() as directory:
            root, manifest = self.fixture(
                directory, wrappers=[{"name": "../escape", "lines": ["{{sample}}"]}]
            )
            with self.assertRaisesRegex(ValueError, "Invalid or duplicate"):
                snippets.load(root, manifest)

    def test_verbatim_pawn_and_wrapper_line_map_are_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            code = '\tnew values[] = {1, 2};\n\tprintf("%d", values[0]);'
            root, manifest = self.fixture(directory, self.example(code))
            planned, _ = snippets.units(root, manifest)
            _, generated, mapping = planned[0]
            self.assertIn(code, generated)
            self.assertEqual(mapping[3:5], [("docs/guide.md", 4), ("docs/guide.md", 5)])

    def test_compiler_receives_real_includes_and_warnings_as_errors(self):
        with tempfile.TemporaryDirectory() as directory:
            root, manifest = self.fixture(directory)
            with patch.object(
                snippets.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")
            ) as run:
                snippets.compile_examples(
                    root / "pawncc", root, [root / "pawn"], root / "out", manifest
                )
            command = run.call_args.args[0]
            self.assertIn("-E", command)
            self.assertIn("-i" + str((root / "pawn").resolve()), command)
            self.assertTrue(run.call_args.kwargs["capture_output"])

    def test_compiler_failure_is_not_swallowed_and_points_to_markdown(self):
        with tempfile.TemporaryDirectory() as directory:
            root, manifest = self.fixture(directory)
            failure = subprocess.CompletedProcess(
                [], 1, "sample.pwn(4) : error 017: undefined symbol\n", ""
            )
            with patch.object(
                snippets.subprocess, "run", return_value=failure
            ), self.assertRaisesRegex(ValueError, "docs/guide.md:4: error 017"):
                snippets.compile_examples(root / "pawncc", root, [], root / "out", manifest)

    def test_include_diagnostics_keep_their_original_file_and_line(self):
        with tempfile.TemporaryDirectory() as directory:
            root, manifest = self.fixture(directory)
            failure = subprocess.CompletedProcess(
                [], 1, "open.mp.inc(4) : error 017: undefined symbol\n", ""
            )
            with patch.object(
                snippets.subprocess, "run", return_value=failure
            ), self.assertRaisesRegex(ValueError, r"open.mp.inc\(4\) : error 017"):
                snippets.compile_examples(root / "pawncc", root, [], root / "out", manifest)

    def test_literal_callback_is_made_compiler_visible_with_typed_arguments(self):
        source = 'pg_execute(statement, 0, 0, 0, true, "OnResult", "ifbs", 1, 1.0, true, "text");'
        self.assertEqual(
            snippets.callback_checks(source, INCLUDE), [(1, 'OnResult(0, 0, 0.0, false, "");')]
        )
        source = 'pg_query(connection, "SELECT (1,2)", .callback = "OnReady");'
        self.assertEqual(snippets.callback_checks(source, INCLUDE), [(1, "OnReady(0);")])

    def test_literal_callback_typos_reach_the_real_compiler_at_the_original_location(self):
        with tempfile.TemporaryDirectory() as directory:
            root, manifest = self.fixture(
                directory, self.example('pg_query(1, "SELECT 1", .callback = "OnTypo");')
            )
            planned, _ = snippets.units(root, manifest)
            _, generated, mapping = planned[0]
            line = generated.splitlines().index("OnTypo(0);")
            self.assertEqual(mapping[line], ("docs/guide.md", 4))
            self.assertIn("forward __DocCheckCallbacks();", generated)

    def test_bad_literal_callback_formats_and_extra_counts_are_rejected(self):
        for tail in ('"OnDone", "z", 1', '"OnDone", "ii", 1', '"OnDone", "", 1'):
            with self.subTest(tail=tail), self.assertRaisesRegex(
                ValueError, "invalid literal callback"
            ):
                snippets.callback_checks("pg_execute(1,0,0,0,true," + tail + ");", INCLUDE)

    def test_strings_comments_and_dynamic_or_suppressed_callbacks_are_not_false_calls(self):
        source = '// pg_execute(1,0,0,0,true,"Missing","i");\nprint("pg_query(1)");\n'
        source += (
            'pg_execute(1,0,0,0,false,"Missing","");\npg_execute(1,0,0,0,true,handler,format_);'
        )
        self.assertEqual(snippets.callback_checks(source, INCLUDE), [])

    def test_balanced_arguments_keep_sql_commas_arrays_and_escaped_quotes(self):
        self.assertEqual(
            snippets.split_arguments('a, "a,\\"b", f(1,2), {1,2}'),
            ["a", '"a,\\"b"', "f(1,2)", "{1,2}"],
        )

    def test_comments_inside_arguments_cannot_hide_literal_callbacks(self):
        source = 'pg_query(1, "SELECT 1", /* ignored , ) */ .callback = "OnReady");'
        self.assertEqual(snippets.callback_checks(source, INCLUDE), [(1, "OnReady(0);")])

    def test_documented_resource_table_matches_cpp_defaults_and_environment_ceilings(self):
        types = (ROOT / "include/pg/types.hpp").read_text()
        limits = re.search(r"struct Limits \{(.*?)\n\};", types, re.S)[1]
        product = lambda value: __import__("math").prod(
            int(part.strip()) for part in value.split("*")
        )
        defaults = {
            field: product(value) for field, value in re.findall(r"\b(\w+)\s*=\s*([\d *]+)", limits)
        }
        config = (ROOT / "src/config.cpp").read_text()
        expected = {
            name: (defaults[field], product(maximum))
            for name, field, maximum in re.findall(
                r'read\("OMP_PG_(\w+)", l\.(\w+), ([\d *]+)(?:, \d+)?\);', config
            )
        }
        security = (ROOT / "docs/security.md").read_text()
        actual = {
            name: (int(default), int(maximum))
            for name, default, maximum in re.findall(
                r"^\| ([A-Z_]+)\s*\|\s*(\d+)\s*\|\s*(\d+)\s*\|", security, re.M
            )
        }
        self.assertEqual(actual, expected)


class SqlDocumentationTests(unittest.TestCase):
    def test_sql_is_extracted_from_current_fences_and_rolled_back(self):
        program = sql.build_sql()
        _, examples, _, _, fences, _ = snippets.load(ROOT)
        for id in ("crud.insert", "crud.delete", "account.submit", "save.one"):
            self.assertIn(sql.prepared_sql(examples, id), program)
        self.assertIn(fences["save.sql"].code.strip().rstrip(";"), program)
        self.assertIn("BEGIN;", program)
        self.assertIn("ROLLBACK;", program)
        self.assertNotIn("COMMIT;", program)
        self.assertRegex(program, "CREATE SCHEMA doc_examples_[a-f0-9]{32};")

    def test_sql_check_requires_the_existing_disposable_runner_service(self):
        with patch.dict(os.environ, {}, clear=True), patch.object(sql.subprocess, "run") as run:
            with self.assertRaisesRegex(ValueError, "disposable"):
                sql.check("production")
            run.assert_not_called()

    def test_psql_failure_and_missing_affected_counts_fail_the_check(self):
        with patch.dict(os.environ, {"OMP_PG_TEST_SERVICE": "omp_test"}):
            with patch.object(
                sql.subprocess,
                "run",
                return_value=subprocess.CompletedProcess([], 1, "", "syntax error"),
            ):
                with self.assertRaisesRegex(ValueError, "syntax error"):
                    sql.check("omp_test")
            with patch.object(
                sql.subprocess,
                "run",
                return_value=subprocess.CompletedProcess([], 0, "DOCUMENTATION SQL PASS\n", ""),
            ):
                with self.assertRaisesRegex(ValueError, "affected-row counts"):
                    sql.check("omp_test")

    def test_psql_is_noninteractive_and_checks_completion_and_counts(self):
        output = "DELETE 1\nDELETE 0\n" + "UPDATE 1\n" * 6 + "UPDATE 3\nDOCUMENTATION SQL PASS\n"
        with patch.dict(os.environ, {"OMP_PG_TEST_SERVICE": "omp_test"}), patch.object(
            sql.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, output, "")
        ) as run:
            sql.check("omp_test", "fixture-psql")
        self.assertIn("--no-password", run.call_args.args[0])
        self.assertIn("--set=ON_ERROR_STOP=1", run.call_args.args[0])


if __name__ == "__main__":
    unittest.main()
