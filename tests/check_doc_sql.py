"""Check selected documented SQL inside the existing runner's disposable PostgreSQL cluster.

This verifies SQL behavior, not Pawn callbacks, bind encoding, or gameplay. All objects
live in a unique schema in one transaction that is rolled back, including on failure.
"""

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import check_doc_examples


def prepared_sql(examples, name):
    fence = examples[name]
    match = re.search(r'pg_prepare\(\w+,\s*"([^"\\]+)"', fence.code)
    if not match:
        raise ValueError(f"{fence.location}: SQL check needs one plain pg_prepare SQL literal")
    return match[1]


def build_sql(root=ROOT):
    _, examples, _, _, sql, _ = check_doc_examples.load(root)
    schema = "doc_examples_" + uuid.uuid4().hex
    sections = re.split(r"^--\s*name:\s*\w+\s*\n", sql["section.file"].code, flags=re.M)[1:]
    if len(sections) != 2:
        raise ValueError(f"{sql['section.file'].location}: expected the two documented sections")
    return f"""
BEGIN;
CREATE SCHEMA {schema};
SET LOCAL search_path TO {schema};
{sql['crud.schema'].code}
PREPARE doc_insert(text, integer) AS {prepared_sql(examples, 'crud.insert')};
EXECUTE doc_insert('O''Brien',7);
PREPARE doc_read(integer) AS {prepared_sql(examples, 'account.submit')};
EXECUTE doc_read(1);
EXECUTE doc_read(999);
DO $$ BEGIN
  IF (SELECT username <> 'O''Brien' OR score <> 7 FROM tutorial_inline_accounts WHERE id=1)
    OR (SELECT count(*) FROM tutorial_inline_accounts) <> 1 THEN RAISE EXCEPTION 'insert/read mismatch'; END IF;
END $$;
PREPARE doc_delete(integer) AS {prepared_sql(examples, 'crud.delete')};
EXECUTE doc_delete(1);
EXECUTE doc_delete(1);
CREATE TABLE tutorial_saves(id integer PRIMARY KEY, score integer NOT NULL);
INSERT INTO tutorial_saves VALUES(1,0),(2,0),(3,0);
PREPARE doc_update(integer, integer) AS {prepared_sql(examples, 'save.one')};
EXECUTE doc_update(1,101);
DO $$ BEGIN IF (SELECT score FROM tutorial_saves WHERE id=1) <> 101 THEN RAISE EXCEPTION 'single update mismatch'; END IF; END $$;
EXECUTE doc_update(1,10);
EXECUTE doc_update(2,20);
EXECUTE doc_update(3,30);
DO $$ BEGIN IF (SELECT array_agg(score ORDER BY id) FROM tutorial_saves) <> ARRAY[10,20,30] THEN RAISE EXCEPTION 'snapshot mismatch'; END IF; END $$;
PREPARE doc_bulk(integer[], integer[]) AS {sql['save.sql'].code.strip().rstrip(';')};
EXECUTE doc_bulk(ARRAY[1,2,3],ARRAY[100,200,300]);
DO $$ BEGIN IF (SELECT array_agg(score ORDER BY id) FROM tutorial_saves) <> ARRAY[100,200,300] THEN RAISE EXCEPTION 'bulk mismatch'; END IF; END $$;
SAVEPOINT before_missing_id;
EXECUTE doc_bulk(ARRAY[1,999],ARRAY[111,999]);
ROLLBACK TO before_missing_id;
DO $$ BEGIN
  IF (SELECT score FROM tutorial_saves WHERE id=1) <> 100 THEN RAISE EXCEPTION 'rollback mismatch'; END IF;
  IF (SELECT count(*) FROM unnest(ARRAY[1,2],ARRAY[10]) AS v(id,score) WHERE score IS NULL) <> 1 THEN RAISE EXCEPTION 'array padding mismatch'; END IF;
END $$;
CREATE TABLE accounts(id integer PRIMARY KEY, username text UNIQUE, money integer, score integer);
INSERT INTO accounts VALUES(1,'alice',10,20);
PREPARE doc_section_load(text) AS {sections[0].strip().rstrip(';')};
EXECUTE doc_section_load('alice');
PREPARE doc_section_save(integer,integer,integer) AS {sections[1].strip().rstrip(';')};
EXECUTE doc_section_save(30,40,1);
DO $$ BEGIN IF (SELECT money <> 30 OR score <> 40 FROM accounts WHERE id=1) THEN RAISE EXCEPTION 'section mismatch'; END IF; END $$;
ROLLBACK;
SELECT 'DOCUMENTATION SQL PASS';
"""


def check(service, psql="psql", root=ROOT):
    if service != "omp_test" or os.environ.get("OMP_PG_TEST_SERVICE") != service:
        raise ValueError(
            "Run through tests/run_postgres.py --doc-sql using its disposable omp_test fixture"
        )
    result = subprocess.run(
        [
            str(psql),
            "-X",
            "--no-password",
            "-A",
            "-t",
            "--set=ON_ERROR_STOP=1",
            "--dbname=service=" + service,
        ],
        input=build_sql(root),
        capture_output=True,
        text=True,
        encoding="utf8",
        errors="replace",
        timeout=30,
        creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
    )
    if result.returncode:
        raise ValueError(
            "Documented SQL failed in the disposable fixture:\n" + result.stderr + result.stdout
        )
    lines = result.stdout.splitlines()
    if (
        "DOCUMENTATION SQL PASS" not in lines
        or "DELETE 1" not in lines
        or "DELETE 0" not in lines
        or lines.count("UPDATE 1") != 6
        or lines.count("UPDATE 3") != 1
    ):
        raise ValueError(
            "Documented SQL affected-row counts or completion marker differ:\n" + result.stdout
        )
    print(
        "DOCUMENTATION SQL PASS: parameterized CRUD/zero rows, individual/snapshot/UNNEST updates, "
        "affected counts, savepoint rollback, array padding and section queries. No Pawn runtime claim."
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--service", required=True)
    parser.add_argument("--psql", default="psql")
    args = parser.parse_args()
    check(args.service, args.psql)
