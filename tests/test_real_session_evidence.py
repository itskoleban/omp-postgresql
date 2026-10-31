"""Privacy transformations preserve original identities and test observations."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).parents[1] / "scripts"))
from real_session_evidence import publish, sanitized_bytes, sanitize_text, sha, verify


def personal_path(tail):
    # Synthetic fixture, not an actual operator's path.
    return "C:" + "\\Users\\" + "fixture-user\\Documents\\private-project\\work\\" + tail


class EvidencePrivacyTests(unittest.TestCase):
    def test_repository_ledgers_and_observations(self):
        directory=Path(__file__).parents[1]/"audit/real-session-evidence"
        self.assertEqual(verify(directory),127)

    def test_json_keeps_statuses_tokens_and_binary_hashes(self):
        value = {"inputs":{"component":{"path":personal_path("omp-postgresql\\build-msvc32\\Release\\PostgreSQL.dll"), "sha256":"a"*64}},
                 "results":[{"status":s,"token":i,"job":17} for i,s in enumerate(("PASS","FAIL","BLOCKED","NOT RUN"))]}
        old = json.dumps(value).encode()
        data, changes = sanitized_bytes(old, ".json")
        result = json.loads(data)
        self.assertEqual(result["inputs"]["component"]["path"], "<WORKSPACE>/build-msvc32/Release/PostgreSQL.dll")
        self.assertEqual(result["inputs"]["component"]["sha256"], "a"*64)
        self.assertEqual(result["results"], value["results"])
        self.assertTrue(changes)

    def test_escaped_caret_and_linux_paths_without_line_ending_changes(self):
        for text in (personal_path("omp-postgresql\\build"), personal_path("omp-postgresql\\build").replace("\\","\\\\"), personal_path("omp-postgresql\\build").replace(":","^:").replace("\\","^\\")):
            data, _ = sanitized_bytes((text+"\r\nQA event=sql_closed job=3 closed=1\r\n").encode(), ".txt")
            self.assertEqual(data, b"<WORKSPACE>/build\r\nQA event=sql_closed job=3 closed=1\r\n")
        text, _ = sanitize_text("/tmp/omp-pg-integration-fixture/data /home/runner/work/_temp/openmp/Server")
        self.assertEqual(text, "<POSTGRES_CLUSTER>/data <RUNNER_TEMP>/openmp/Server")
        self.assertEqual(sanitize_text(text)[0], text)

    def test_unknown_personal_directory_is_opaque_but_known_python_tool_is_named(self):
        text,_ = sanitize_text(personal_path("opaque\\private-name"))
        self.assertNotIn("private-name",text)
        self.assertNotIn("fixture-user",text)
        self.assertEqual(sanitize_text(personal_path("Python313\\python.exe"))[0],"<PYTHON_RUNTIME>/Python313/python.exe")

    def test_original_ledger_and_sanitized_hashes_verified_separately(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root/"original"
            source.mkdir()
            old = (personal_path("omp-postgresql\\build")+"\nQA event=verify_end token=7 state=0\n").encode()
            (source/"server-output.txt").write_bytes(old)
            ledger = [{"file":"server-output.txt","bytes":len(old),"sha256":sha(old)}]
            ledger_bytes = (json.dumps(ledger)+"\n").encode()
            (source/"sha256.json").write_bytes(ledger_bytes)
            target = root/"sanitized"
            meta = publish(source,target,original_commit="b"*40,original_ledger=source/"sha256.json")
            row = json.loads((target/"sha256.json").read_text())[0]
            self.assertEqual((source/"server-output.txt").read_bytes(),old)
            self.assertEqual((target/"original-sha256.json").read_bytes(),ledger_bytes)
            self.assertEqual(row["original_sha256"],sha(old))
            self.assertNotEqual(row["sha256"],row["original_sha256"])
            self.assertEqual(row["representation"],"sanitized-copy")
            self.assertEqual(meta["observations_changed"],False)
            self.assertEqual(verify(target),1)
            (target/"server-output.txt").write_bytes(b"tampered")
            with self.assertRaisesRegex(ValueError,"hash/size"):
                verify(target)

    def test_junit_remains_valid_xml_and_preserves_test_outcomes(self):
        import xml.etree.ElementTree as ET
        old=('<testsuite hostname="fixture-machine"><testcase name="unchanged" status="run"><system-out>'+personal_path("omp-postgresql\\build")+'</system-out></testcase></testsuite>').encode()
        data,changes=sanitized_bytes(old,".xml")
        result=ET.fromstring(data)
        self.assertEqual(result.get("hostname"),"SANITIZED-HOST")
        self.assertEqual(result.find("testcase").get("name"),"unchanged")
        self.assertEqual(result.find("testcase/system-out").text,"<WORKSPACE>/build")
        self.assertEqual(changes["host-identity"],1)

    def test_rejects_bad_original_and_does_not_publish_private_keys(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);source=root/"original";source.mkdir()
            (source/"results.json").write_text('{"results":[]}')
            ledger=source/"sha256.json"
            ledger.write_text('[{"file":"results.json","bytes":1,"sha256":"bad"}]')
            with self.assertRaisesRegex(ValueError,"Original evidence"):
                publish(source,root/"wrong",original_ledger=ledger)
            (source/"server-output.txt").write_text("-----BEGIN PRIVATE KEY-----\nfixture\n")
            with self.assertRaisesRegex(ValueError,"Credential"):
                publish(source,root/"private")

    def test_failed_run_keeps_cause_status_and_job_while_sanitizing_error_path(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);source=root/"original";source.mkdir()
            value={"results":[{"status":"FAIL","job":17,"error":"FileNotFoundError: "+personal_path("omp-postgresql\\missing.dll")}]}
            (source/"results.json").write_text(json.dumps(value))
            target=root/"sanitized"
            publish(source,target)
            row=json.loads((target/"results.json").read_text())["results"][0]
            self.assertEqual(row,{"status":"FAIL","job":17,"error":"FileNotFoundError: <WORKSPACE>/missing.dll"})
            self.assertEqual(verify(target),1)


if __name__ == "__main__":
    unittest.main()
