"""Timeline publication and failure preservation; no real network PASS is claimed."""
import io
from contextlib import redirect_stdout
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).parent))
sys.path.insert(0, str(Path(__file__).parents[1]/"scripts"))
from real_session_evidence import observation_hash, publish, sanitized_bytes, sha, verify
from run_real_sessions import Suite, Server
import run_real_session_ci


class TimelineEvidenceTests(unittest.TestCase):
    def suite(self, root):
        suite = Suite.__new__(Suite)
        suite.output=root;suite.results=[];suite.sql_results=[];suite.servers=[]
        suite.password="fixture-password";suite.wrong="fixture-wrong";suite.provenance=None
        for name in ("component","gamemode","client","provider","omp-server","omp-server.exe"):
            (root/name).write_bytes(b"unit fixture")
        suite.config={key:str(root/key) for key in ("component","gamemode","client","provider")}
        suite.config["server"]=str(root)
        return suite

    def test_success_failure_readiness_tls_and_process_exit_keep_timelines(self):
        for scenario in ("success","scenario_failure","readiness_failure","tls_failure","unexpected_exit"):
            with self.subTest(scenario=scenario), tempfile.TemporaryDirectory() as directory:
                root=Path(directory);source=root/"raw";source.mkdir();suite=self.suite(source)
                server=Server.__new__(Server);server.suite=suite;server.root=source/scenario;server.root.mkdir()
                server.log_path=server.root/"server-output.txt"
                server.log_path.write_text("QA event=database state=1 ready="+str(int(scenario=="success"))+"\n")
                server.log=server.log_path.open("ab");server.clients=[]
                process=SimpleNamespace(pid=42,returncode=9 if scenario=="unexpected_exit" else None,stdin=io.BytesIO())
                process.poll=lambda:process.returncode
                def wait(timeout):
                    process.returncode=0
                process.wait=wait;server.proc=process;suite.servers.append(server)
                def until(predicate, seconds):
                    value=predicate()
                    if not value:
                        raise AssertionError("Observed-event deadline expired")
                    return value
                suite.until=until
                def action():
                    suite.trace("server_started",server=scenario,pid=42,udp_port=1234)
                    if scenario=="scenario_failure":
                        raise AssertionError("Synthetic scenario failure")
                    if scenario=="tls_failure":
                        suite.trace("postgres_failed",exit_code=1)
                    server.wait("database",ready=1)
                    return ["Synthetic readiness observed"]
                with patch("run_real_sessions.capture_source",return_value={"source_commit":"a"*40,"source_dirty":False}), redirect_stdout(io.StringIO()):
                    suite.case(scenario,action)
                original=(source/"timeline.jsonl").read_bytes()
                expected="PASS" if scenario=="success" else "FAIL"
                self.assertEqual(suite.results[0]["status"],expected)
                target=root/"public";publish(source,target)
                self.assertEqual((target/"timeline.jsonl").read_bytes(),original)
                events=[json.loads(line) for line in original.splitlines()]
                self.assertEqual(events[-1]["status"],expected)
                self.assertEqual([e["monotonic_ns"] for e in events],sorted(e["monotonic_ns"] for e in events))
                if scenario in ("readiness_failure","tls_failure","unexpected_exit"):
                    self.assertTrue(any(e["event"]=="event_failed" for e in events))
                ledger=json.loads((target/"sha256.json").read_text())
                row=next(r for r in ledger if r["file"]=="timeline.jsonl")
                self.assertEqual(row["original_sha256"],sha(original))
                self.assertEqual(row["sha256"],sha(original))
                self.assertEqual(row["observations_sha256"],observation_hash(Path("timeline.jsonl"),original))
                self.assertEqual(verify(target),3)

    def test_event_order_clock_and_outcomes_are_fingerprinted(self):
        rows=[{"utc":"2026-10-24T12:00:00+00:00","monotonic_ns":i,"event":"case_end","status":status} for i,status in enumerate(("PASS","FAIL"))]
        data=("\r\n".join(json.dumps(row) for row in rows)+"\r\n").encode()
        self.assertEqual(sanitized_bytes(data,".jsonl")[0],data)
        original=observation_hash(Path("timeline.jsonl"),data)
        for modified in (rows[::-1],[{**rows[0],"monotonic_ns":99},rows[1]],[rows[0],{**rows[1],"status":"PASS"}]):
            other=("\n".join(json.dumps(row) for row in modified)+"\n").encode()
            self.assertNotEqual(original,observation_hash(Path("timeline.jsonl"),other))

    def test_rejects_secret_payloads_and_duplicate_fields_without_editing_original(self):
        base={"utc":"2026-10-24T12:00:00Z","monotonic_ns":1,"event":"case_begin"}
        for extra in ({"password":"fixture-secret"},{"token":"opaque-token"},{"server":"C:\\Users\\fixture-user\\private"}):
            with self.subTest(extra=extra), tempfile.TemporaryDirectory() as directory:
                root=Path(directory);source=root/"raw";source.mkdir()
                data=(json.dumps({**base,**extra})+"\n").encode();(source/"timeline.jsonl").write_bytes(data)
                with self.assertRaises(ValueError):
                    publish(source,root/"public")
                self.assertEqual((source/"timeline.jsonl").read_bytes(),data)
        with self.assertRaisesRegex(ValueError,"Duplicate"):
            sanitized_bytes(b'{"event":"a","event":"b"}\n',".jsonl")
        escaped=(json.dumps({**base,"event":"ghp_"+"A"*30})+"\n").encode().replace(b"ghp_",b"gh\\u0070_")
        with self.assertRaisesRegex(ValueError,"Sensitive timeline"):
            sanitized_bytes(escaped,".jsonl")

    def test_ci_finally_publishes_timeline_and_preserves_failure_exit(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);archive=root/"provider.zip"
            with zipfile.ZipFile(archive,"w") as package:
                package.writestr("includes/fixture.inc",b"// Fixture only\n")
            output=root/"run"
            failure=subprocess.CalledProcessError(7,["fixture-postgres"])
            def run(command,**kwargs):
                if "--only-real-sessions" in command:
                    source=output/"evidence";source.mkdir()
                    (source/"results.json").write_text('{"source_provenance":{"tracked_source_modified":false},"results":[{"status":"FAIL"}]}')
                    (source/"timeline.jsonl").write_text('{"utc":"2026-10-24T12:00:00Z","monotonic_ns":1,"event":"event_failed","exit_code":7}\n')
                    raise failure
            argv=["runner","--output",str(output),"--client",str(root/"client/bin/fixture")]
            for key in ("component","server","compiler","stdlib","pawn-include"):
                argv += ["--"+key,str(root/key)]
            def download(url,target):
                Path(target).write_bytes(archive.read_bytes())
            captured = io.StringIO()
            with patch.object(sys,"argv",argv),patch.dict(os.environ,{"GITHUB_ACTIONS":"true"}),redirect_stdout(captured),patch.object(run_real_session_ci.urllib.request,"urlretrieve",side_effect=download),patch.object(run_real_session_ci,"PROVIDER_HASHES",{key:sha(archive.read_bytes()) for key in ("linux","windows")}),patch.object(run_real_session_ci.subprocess,"run",side_effect=run):
                with self.assertRaises(subprocess.CalledProcessError) as caught:
                    run_real_session_ci.main()
            self.assertIs(caught.exception,failure)
            target=output/"sanitized-evidence"
            self.assertTrue((target/"timeline.jsonl").is_file())
            self.assertEqual(json.loads((target/"results.json").read_text())["results"][0]["status"],"FAIL")
            self.assertEqual(verify(target),2)
            self.assertIn("REAL SESSION TIMELINE events=1 original_sha256=",captured.getvalue())

    def test_timeline_observation_change_is_detected_after_rehashing_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);source=root/"raw";source.mkdir()
            (source/"timeline.jsonl").write_text('{"utc":"2026-10-24T12:00:00Z","monotonic_ns":1,"event":"case_end","status":"FAIL"}\n')
            target=root/"public";publish(source,target)
            path=target/"timeline.jsonl";data=path.read_bytes().replace(b'"FAIL"',b'"PASS"');path.write_bytes(data)
            ledger=json.loads((target/"sha256.json").read_text());ledger[0].update(sha256=sha(data),bytes=len(data))
            (target/"sha256.json").write_text(json.dumps(ledger))
            with self.assertRaisesRegex(ValueError,"observation fingerprint"):
                verify(target)
