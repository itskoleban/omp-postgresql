"""Bounded real-network qualification. Invoke ONLY through run_postgres.py.

Requires a supplied disposable official server, compiled real_sessions.pwn,
samp-bcrypt and the pinned RakSAMP client with the documented pipe adapter.
Passwords exist in memory only. No rendered game/client-encryption claim.
"""

import argparse
import configparser
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import socket
import subprocess
import sys
import time
from commit_proxy import CommitLossProxy

FLAGS = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def parse_events(content):
    events = []
    for line in content.splitlines():
        if "QA event=" in line:
            fields = dict(re.findall(r"(\w+)=([^\s]*)", line.split("QA ", 1)[1]))
            events.append({k: int(v) if k != "sqlstate" and re.fullmatch(r"-?\d+", v) else v for k, v in fields.items()})
    return events


def free_port(kind=socket.SOCK_STREAM):
    with socket.socket(type=kind) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


class Suite:
    def __init__(self, config, postgres_bin):
        self.config = config
        self.output = Path(config["output"]).resolve()
        self.output.mkdir(parents=True, exist_ok=False)
        self.psql = str(Path(postgres_bin) / ("psql.exe" if os.name == "nt" else "psql")) if postgres_bin else "psql"
        self.pg_ctl = str(Path(postgres_bin) / ("pg_ctl.exe" if os.name == "nt" else "pg_ctl")) if postgres_bin else "pg_ctl"
        self.env = os.environ.copy()
        self.password = secrets.token_urlsafe(24)
        self.wrong = secrets.token_urlsafe(24)
        self.results = []
        self.servers = []
        self.sql_results = []
        # Capture provenance before work starts, rather than rebinding earlier
        # observations if somebody edits the checkout during a long test run.
        self.provenance = None
        self.save()
        self.data = self.sql("SHOW data_directory")
        # Preserve TLS verification while restricting the tutorial to its own role.
        profiles = configparser.ConfigParser(interpolation=None)
        profiles.read(self.env["PGSERVICEFILE"], encoding="utf8")
        profiles["game"]["user"] = "qa_runtime"
        profiles["game"]["application_name"] = "qa_game"
        profile = self.output / "pg_service.conf"
        with profile.open("w", encoding="utf8") as stream:
            profiles.write(stream, space_around_delimiters=False)
        self.env["PGSERVICEFILE"] = str(profile)
        self.sql("ALTER SYSTEM SET log_min_error_statement='panic'")
        self.sql("ALTER SYSTEM SET log_parameter_max_length_on_error=0")
        self.sql("SELECT pg_reload_conf()")
        self.sql("CREATE ROLE qa_runtime LOGIN")
        schema = (Path(__file__).parents[1] / "examples/sql/tutorial-auth.sql").read_text(encoding="utf8")
        self.sql(schema)
        self.sql("GRANT SELECT,INSERT ON tutorial_accounts TO qa_runtime; GRANT USAGE ON SEQUENCE tutorial_accounts_id_seq TO qa_runtime")

    def sql(self, statement):
        result = subprocess.run([self.psql, "service=omp_test", "-X", "-At", "-v", "ON_ERROR_STOP=1", "-c", statement],
                                env=self.env, capture_output=True, creationflags=FLAGS, timeout=15)
        if result.returncode:
            # Error text can include bound SQL data. Do not emit it.
            raise RuntimeError("Disposable SQL operation failed")
        output = result.stdout.decode("utf8", errors="replace").strip()
        self.sql_results.append({"sql": statement, "result": output})
        return output

    def clone_account(self, name, source="QSeed", score=731):
        assert re.fullmatch(r"Q[A-Za-z0-9_]{1,18}", name)
        assert source in ("QSeed", "QSlow")
        self.sql(f"INSERT INTO tutorial_accounts(username,password_hash,score) SELECT '{name}',password_hash,{score} FROM tutorial_accounts WHERE username='{source}'")

    def count(self, name):
        assert re.fullmatch(r"Q[A-Za-z0-9_]{1,18}", name)
        return int(self.sql(f"SELECT count(*) FROM tutorial_accounts WHERE username='{name}'"))

    def lock(self, mode, relation="tutorial_accounts"):
        assert mode in ("ACCESS EXCLUSIVE", "SHARE")
        assert relation in ("tutorial_accounts", "tutorial_custom_accounts", "tutorial_custom_accounts,tutorial_custom_properties")
        env = dict(self.env, PGAPPNAME="qa_lock")
        proc = subprocess.Popen([self.psql, "service=omp_test", "-X", "-At", "-c", f"BEGIN; LOCK {relation} IN {mode} MODE; SELECT pg_sleep(60); ROLLBACK"],
                                env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=FLAGS)
        first = relation.split(",")[0]
        self.until(lambda: self.sql(f"SELECT count(*) FROM pg_locks l JOIN pg_stat_activity a ON a.pid=l.pid WHERE a.application_name='qa_lock' AND l.relation='{first}'::regclass AND l.granted") == "1")
        return proc

    def unlock(self, proc):
        self.sql("SELECT pg_terminate_backend(pid) FROM pg_stat_activity WHERE application_name='qa_lock'")
        proc.wait(timeout=5)

    @staticmethod
    def until(predicate, seconds=15):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            value = predicate()
            if value:
                return value
            time.sleep(0.025)
        raise AssertionError("Observed-event deadline expired")

    def case(self, name, action):
        if self.config.get("cases") and name not in self.config["cases"]:
            return
        started = time.monotonic()
        print(f"REAL SESSION RUN {name}", flush=True)
        try:
            detail = action()
            self.results.append({"id": name, "status": "PASS", "assertions": detail, "seconds": round(time.monotonic()-started, 3)})
        except Exception as error:
            self.results.append({"id": name, "status": "FAIL", "error": f"{type(error).__name__}: {error}", "seconds": round(time.monotonic()-started, 3)})
        finally:
            for server in self.servers:
                try:
                    server.close()
                except Exception as error:
                    self.results[-1].update(status="FAIL", cleanup_error=f"{type(error).__name__}: {error}")
            self.servers.clear()
        self.save()
        print(f"REAL SESSION {self.results[-1]['status']} {name}", flush=True)

    def server(self, name, gamemode=None):
        server = Server(self, name, gamemode)
        return server

    def save(self):
        if self.provenance is not None:
            manifest = dict(self.provenance, results=self.results, sql=self.sql_results)
            self.write_manifest(manifest)
            return
        identity = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=Path(__file__).parents[1]).decode().strip()
        dirty = subprocess.check_output(["git", "status", "--porcelain"], cwd=Path(__file__).parents[1]).decode().strip()
        manifest = {"source_commit": identity, "source_dirty": bool(dirty), "platform": sys.platform,
                    "inputs": {k: {"path": str(Path(self.config[k]).resolve()), "sha256": digest(self.config[k])} for k in ("component", "gamemode", "client", "provider")},
                    "results": self.results, "sql": self.sql_results}
        manifest["inputs"]["server"] = {"sha256": digest(Path(self.config["server"]) / ("omp-server.exe" if os.name == "nt" else "omp-server")), "version": "1.5.8.3079 (supplied distribution; verify archive separately)"}
        for key in ("custom_gamemode",):
            if self.config.get(key):
                manifest["inputs"][key] = {"path": self.config[key], "sha256": digest(self.config[key])}
        manifest["runtime"] = [{"file": Path(p).name, "sha256": digest(p)} for p in self.config.get("runtime", [])]
        manifest["harness_sha256"] = digest(__file__)
        self.provenance = dict(manifest)
        self.write_manifest(manifest)

    def write_manifest(self, manifest):
        serialized = json.dumps(manifest, indent=2)
        for secret in (self.password, self.wrong):
            if secret in serialized:
                raise RuntimeError("Secret in qualification manifest")
        (self.output / "results.json").write_text(serialized+"\n", encoding="utf8")


class Server:
    def __init__(self, suite, name, gamemode=None):
        self.suite = suite
        self.root = suite.output / name
        shutil.copytree(suite.config["server"], self.root)
        self.port = free_port(socket.SOCK_DGRAM)
        config_path = self.root / "config.json"
        config = json.loads(config_path.read_text(encoding="utf8"))
        config.update(announce=False, enable_query=False, max_players=8, max_bots=1, name="Isolated SQL session qualification")
        config["network"].update(bind="127.0.0.1", port=self.port, use_omp_encryption=False, allow_037_clients=True)
        config["pawn"].update(main_scripts=["qa 1"], side_scripts=[], legacy_plugins=["samp_bcrypt" if os.name == "nt" else "samp_bcrypt.so"])
        config["rcon"]["enable"] = False
        config["artwork"]["enable"] = False
        config["logging"].update(log_chat=False, log_cookies=False, log_queries=False)
        config_path.write_text(json.dumps(config, indent=2), encoding="utf8")
        shutil.copy2(suite.config["component"], self.root / "components" / Path(suite.config["component"]).name)
        (self.root / "plugins").mkdir(exist_ok=True)
        shutil.copy2(suite.config["provider"], self.root / "plugins" / Path(suite.config["provider"]).name)
        shutil.copy2(gamemode or suite.config["gamemode"], self.root / "gamemodes/qa.amx")
        for runtime in suite.config.get("runtime", []):
            shutil.copy2(runtime, self.root / Path(runtime).name)
        self.log_path = self.root / "server-output.txt"
        self.log = self.log_path.open("wb")
        self.clients = []
        exe = self.root / ("omp-server.exe" if os.name == "nt" else "omp-server")
        env = dict(suite.env)
        if os.name != "nt":
            env["LD_LIBRARY_PATH"] = str(self.root) + ":" + env.get("LD_LIBRARY_PATH", "")
        self.proc = subprocess.Popen([str(exe)], cwd=self.root, env=env, stdin=subprocess.PIPE,
                                     stdout=self.log, stderr=subprocess.STDOUT, creationflags=FLAGS)
        suite.servers.append(self)
        self.wait("database", ready=1)

    def events(self):
        return parse_events(self.log_path.read_text(encoding="utf8", errors="replace"))

    def mark(self):
        return len(self.events())

    def wait(self, event, since=0, seconds=15, **fields):
        def find():
            matches = [e for e in self.events()[since:] if e.get("event") == event and all(e.get(k) == v for k,v in fields.items())]
            if self.proc.poll() is not None and not matches:
                raise AssertionError(f"open.mp exited with {self.proc.returncode}")
            return matches[0] if matches else None
        try:
            return self.suite.until(find, seconds)
        except AssertionError as error:
            raise AssertionError(f"{event} {fields}: {error}") from None

    def command(self, text):
        self.proc.stdin.write((text+"\n").encode("utf-16le" if os.name == "nt" else "utf8"))
        self.proc.stdin.flush()

    def client(self, name):
        client = Client(self, name)
        return client

    def close(self):
        if self.log.closed:
            return
        for client in self.clients:
            client.close()
        if self.proc.poll() is None:
            self.command("exit")
            try:
                self.proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=5)
                raise AssertionError("open.mp required forced shutdown")
        self.log.close()
        if self.proc.returncode != 0:
            raise AssertionError(f"open.mp shutdown exit {self.proc.returncode}")
        content = self.log_path.read_text(encoding="utf8", errors="replace")
        if any(secret in content for secret in (self.suite.password, self.suite.wrong)) or re.search(r"\$2[aby]\$\d\d\$[./A-Za-z0-9]{53}", content):
            raise AssertionError("Sensitive authentication material in server log")


class Client:
    def __init__(self, server, name):
        self.server = server
        assert re.fullmatch(r"Q[A-Za-z0-9_]{1,18}", name)
        self.name = name
        self.root = server.root / (name+"-"+secrets.token_hex(3))
        self.root.mkdir()
        config = self.root / "client.xml"
        config.write_text(f'<RakSAMPClient console="1" dialogs="console" runmode="1" manual_spawn="1" autorun="0" find="0" log_level="1" clientversion="0.3.7"><server ip="127.0.0.1" port="{server.port}" nickname="{name}" password=""/><intervals reconnect="60000"/></RakSAMPClient>', encoding="utf8")
        self.log = (self.root / "client-output.txt").open("wb")
        mark = server.mark()
        self.proc = subprocess.Popen([str(Path(server.suite.config["client"]).resolve()), "--config", str(config)],
                                     cwd=self.root, stdin=subprocess.PIPE, stdout=self.log, stderr=subprocess.STDOUT, creationflags=FLAGS)
        server.clients.append(self)
        self.p = server.wait("connect", since=mark, name=name)["p"]
        self.initial = server.wait("connected", since=mark, p=self.p, npc=0, connected=1)

    def send(self, text):
        self.proc.stdin.write((text+"\n").encode("utf8"))
        self.proc.stdin.flush()

    def control(self, command):
        mark = self.server.mark()
        self.send("/qa "+command)
        return self.server.wait("control", since=mark, p=self.p)

    def state(self):
        mark = self.server.mark()
        self.control("state")
        return self.server.wait("snapshot", since=mark, p=self.p)

    def prompt(self, state):
        return self.server.wait("sql_end", p=self.p, state=state)

    def password(self, register=True, wrong=False):
        mark = self.server.mark()
        secret = self.server.suite.wrong if wrong else self.server.suite.password
        self.send(f"!dialogresponse {701 if register else 700} 1 -1 {secret}")
        return self.server.wait("dialog", since=mark, p=self.p, state=4 if register else 5)

    def close(self):
        if self.log.closed:
            return
        if self.proc.poll() is None:
            mark = self.server.mark()
            self.send("!quit")
            try:
                self.proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=3)
            if self.server.proc.poll() is None and hasattr(self, "p"):
                self.server.wait("disconnect_end", since=mark, p=self.p)
        self.log.close()


def run_cases(s):
    def registration():
        server = s.server("registration")
        a = server.client("QSeed")
        a.prompt(2)
        mark = server.mark()
        a.password()
        result = server.wait("sql_end", since=mark, p=a.p, state=7)
        assert result["account"] > 0 and s.count("QSeed") == 1
        assert server.wait("sql_closed", since=mark)["closed"] == 1
        return ["real non-NPC connection", "real bcrypt cost 12", "INSERT confirmed", "one account", "job closed"]
    s.case("register", registration)

    def valid_login():
        server = s.server("login")
        a = server.client("QSeed")
        a.prompt(3)
        mark = server.mark()
        a.password(False)
        server.wait("verify_end", since=mark, p=a.p, state=7)
        return ["provider verification succeeded", "authenticated only after provider callback"]
    s.case("login_valid", valid_login)

    def wrong_login():
        server = s.server("wrong")
        a = server.client("QSeed")
        a.prompt(3)
        mark = server.mark()
        a.password(False, True)
        result = server.wait("verify_end", since=mark, p=a.p, state=0, token=0)
        assert result["score"] == 0
        return ["real bcrypt rejection", "token invalidated", "no score applied"]
    s.case("login_wrong", wrong_login)

    def late_lookup():
        s.clone_account("QLookA")
        server = s.server("late-lookup")
        lock = s.lock("ACCESS EXCLUSIVE")
        try:
            a = server.client("QLookA")
            a.control("retain")
            old = a.initial
            assert old["job"] > 0
            a.close()
            b = server.client("QLookB")
            assert a.p == b.p and old["token"] != b.initial["token"]
            mark = server.mark()
            s.unlock(lock)
            lock = None
            server.wait("sql_begin", since=mark, callback_job=old["job"], callback_token=old["token"])
            owner = server.wait("sql_owner", since=mark, p=b.p)
            assert owner["token"] == b.initial["token"] and owner["job"] != old["job"]
            after = server.wait("sql_end", since=mark, p=b.p)
            assert after["account"] == 0 and after["score"] == 0 and after["state"] != 7
            server.wait("sql_closed", since=mark, job=old["job"], closed=1)
            b.prompt(2)
            return [f"observed playerid reuse {a.p}", "old callback after new connect", "different generation and pending job", "no old account/score/auth", "old job closed", "explicit retain-work fault variant"]
        finally:
            if lock: s.unlock(lock)
    s.case("reuse_lookup_late", late_lookup)

    def cancel_lookup():
        server = s.server("cancel-lookup")
        lock = s.lock("ACCESS EXCLUSIVE")
        try:
            a = server.client("QCancelA")
            old = a.initial
            mark = server.mark()
            a.close()
            server.wait("cancel", since=mark, job=old["job"], forwarded=1)
            s.unlock(lock)
            lock = None
            server.wait("sql_closed", since=mark, job=old["job"], closed=1)
            b = server.client("QCancelB")
            assert a.p == b.p
            assert b.prompt(2)["account"] == 0
            return ["default cancellation forwarded", "completed job closed", f"observed reuse {a.p}", "no claim callback arrived after B"]
        finally:
            if lock: s.unlock(lock)
    s.case("disconnect_lookup_cancel", cancel_lookup)

    def reuse_hash():
        server = s.server("late-hash")
        a = server.client("QHashA")
        a.prompt(2)
        a.control("slow")
        old = a.password()
        a.close()
        b = server.client("QHashB")
        assert a.p == b.p and old["token"] != b.initial["token"]
        mark = server.mark()
        server.wait("hash_begin", since=mark, p=a.p, callback_token=old["token"], seconds=30)
        after = server.wait("hash_end", since=mark, p=b.p)
        assert after["token"] == b.initial["token"] and after["account"] == 0 and after["state"] != 7
        assert s.count("QHashA") == 0 and s.count("QHashB") == 0
        return [f"observed reuse {a.p}", "actual cost-16 provider callback after B", "stale generation ignored", "neither account inserted"]
    s.case("reuse_hash_late", reuse_hash)

    def slow_seed():
        server = s.server("slow-seed")
        a = server.client("QSlow")
        a.prompt(2)
        a.control("slow")
        mark = server.mark()
        a.password()
        server.wait("sql_end", since=mark, p=a.p, state=7, seconds=30)
        assert s.count("QSlow") == 1
        return ["real cost-16 account prepared for slow verification"]
    s.case("provider_slow_fixture", slow_seed)

    def reuse_verify():
        s.clone_account("QVerifyA", "QSlow", 937)
        server = s.server("late-verify")
        a = server.client("QVerifyA")
        a.prompt(3)
        old = a.password(False)
        a.close()
        b = server.client("QVerifyB")
        assert a.p == b.p and old["token"] != b.initial["token"]
        mark = server.mark()
        server.wait("verify_begin", since=mark, p=a.p, callback_token=old["token"], success=1, seconds=30)
        after = server.wait("verify_end", since=mark, p=b.p)
        assert after["token"] == b.initial["token"] and after["account"] == 0 and after["score"] == 0 and after["state"] != 7
        return [f"observed reuse {a.p}", "real successful verification for A after B", "B received no account, score or authentication"]
    s.case("reuse_verify_late", reuse_verify)

    def reuse_insert():
        server = s.server("late-insert")
        a = server.client("QInsertA")
        a.prompt(2)
        a.control("retain")
        lock = s.lock("SHARE")
        try:
            mark = server.mark()
            a.password()
            old = server.wait("hash_end", since=mark, p=a.p, state=6)
            assert old["job"] > 0
            a.close()
            b = server.client("QInsertB")
            assert a.p == b.p and old["token"] != b.initial["token"]
            mark = server.mark()
            s.unlock(lock)
            lock = None
            server.wait("sql_begin", since=mark, callback_job=old["job"], callback_token=old["token"])
            after = server.wait("sql_end", since=mark, p=b.p)
            assert after["account"] == 0 and after["score"] == 0 and after["state"] != 7
            server.wait("sql_closed", since=mark, job=old["job"], closed=1)
            assert s.count("QInsertA") == 1 and s.count("QInsertB") == 0
            return [f"observed reuse {a.p}", "A INSERT confirmed after B", "old job closed", "no A state applied to B", "one write; retain-work variant"]
        finally:
            if lock: s.unlock(lock)
    s.case("reuse_insert_late", reuse_insert)

    def concurrent_verify():
        s.clone_account("QConcurrentA")
        s.clone_account("QConcurrentB")
        server = s.server("concurrent-verify")
        a, b = server.client("QConcurrentA"), server.client("QConcurrentB")
        a.prompt(3); b.prompt(3)
        mark = server.mark()
        a.password(False); b.password(False)
        x = server.wait("verify_end", since=mark, p=a.p, state=7)
        y = server.wait("verify_end", since=mark, p=b.p, state=7)
        assert a.p != b.p and x["token"] != y["token"] and x["account"] != y["account"]
        events = server.events()[mark:]
        starts = [i for i,e in enumerate(events) if e["event"] == "dialog" and e["state"] == 5]
        ends = [i for i,e in enumerate(events) if e["event"] == "verify_end"]
        assert len(starts) == 2 and max(starts) < min(ends)
        return ["two connected players", "overlapping real provider work", "distinct tokens and accounts"]
    s.case("concurrent_auth", concurrent_verify)

    def duplicate():
        one, two = s.server("duplicate-one"), s.server("duplicate-two")
        a, b = one.client("QDuplicate"), two.client("QDuplicate")
        a.prompt(2); b.prompt(2)
        lock = s.lock("SHARE")
        try:
            ma, mb = one.mark(), two.mark()
            a.password(); b.password()
            one.wait("hash_end", since=ma, p=a.p, state=6)
            two.wait("hash_end", since=mb, p=b.p, state=6)
            s.unlock(lock); lock = None
            ra = one.wait("sql_end", since=ma, p=a.p)
            rb = two.wait("sql_end", since=mb, p=b.p)
            assert sorted([ra["state"], rb["state"]]) == [0, 7] and s.count("QDuplicate") == 1
            failed = one if ra["state"] == 0 else two
            failed.wait("sql_begin", sqlstate="23505")
            return ["same account name on two real servers", "two overlapping INSERT jobs", "one winner", "23505 loser remains unauthenticated", "one row"]
        finally:
            if lock: s.unlock(lock)
    s.case("duplicate_concurrent_registration", duplicate)

    def timeout():
        server = s.server("provider-timeout")
        a = server.client("QTimeout")
        a.prompt(2)
        a.control("drop")
        mark = server.mark()
        old = a.password()
        server.wait("provider_suppressed", since=mark, callback_token=old["token"])
        server.wait("deadline_end", since=mark, p=a.p, state=0, token=0, seconds=40)
        assert s.count("QTimeout") == 0
        return ["actual provider executed; response discarded by named fault adapter", "real 30-second deadline", "fail closed", "no INSERT"]
    s.case("provider_response_timeout", timeout)

    def recovery():
        server = s.server("database-recovery")
        a = server.client("QSeed")
        a.prompt(3)
        mark = server.mark(); a.password(False)
        server.wait("verify_end", since=mark, p=a.p, state=7)
        mark = server.mark()
        subprocess.run([s.pg_ctl, "-D", s.data, "-m", "immediate", "-w", "stop"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True, creationflags=FLAGS, timeout=15)
        try:
            a.control("lookup")
            server.wait("database", since=mark, ready=0)
            assert a.state()["token"] == 0
            # Hold the outage until exhaustion instead of racing a fast restart
            # against the first automatic reconnect attempt on a faster runner.
            server.wait("database", since=mark, state=4, seconds=20)
        finally:
            with (s.output / "postgres-recovery.txt").open("wb") as log:
                subprocess.run([s.pg_ctl, "-D", s.data, "-l", str(s.output / "postgres-restarted.txt"), "-w", "start"], stdout=log, stderr=subprocess.STDOUT, check=True, creationflags=FLAGS, timeout=20)
        # A failed logical pool has exhausted its policy; recreation is explicit.
        a.control("recover")
        server.wait("database", since=mark, ready=1)
        server.wait("sql_end", since=mark, p=a.p, state=3)
        assert a.state()["state"] == 3
        mark = server.mark(); a.password(False)
        server.wait("verify_end", since=mark, p=a.p, state=7)
        assert s.count("QSeed") == 1
        return ["real PostgreSQL process stopped/restarted", "readiness loss invalidated authentication", "explicit failed-pool recreation", "fresh lookup and real reauthentication", "no registration replay"]
    s.case("postgres_recovery", recovery)

    def lost_insert_response():
        # Fault peer is plaintext on loopback; normal suite connections use verify-full.
        profiles = configparser.ConfigParser(interpolation=None)
        original = s.env["PGSERVICEFILE"]
        profiles.read(original, encoding="utf8")
        proxy = CommitLossProxy(int(profiles["omp_test"]["port"]))
        # The same frame relay now discards only the autocommit INSERT completion.
        proxy.response_tag = b"INSERT 0 1"
        profiles["game"].update(host="127.0.0.1", port=str(proxy.port), sslmode="disable", gssencmode="disable")
        profile = s.output / "insert-loss-service.conf"
        with profile.open("w", encoding="utf8") as stream:
            profiles.write(stream, space_around_delimiters=False)
        s.env["PGSERVICEFILE"] = str(profile)
        try:
            server = s.server("insert-response-loss")
            a = server.client("QLostWrite")
            a.prompt(2)
            before = int(s.sql("SELECT last_value FROM tutorial_accounts_id_seq"))
            mark = server.mark(); a.password()
            server.wait("sql_begin", since=mark, status=4, seconds=20)
            assert proxy.dropped.is_set() and s.count("QLostWrite") == 1
            assert int(s.sql("SELECT last_value FROM tutorial_accounts_id_seq")) == before + 1
            assert not [e for e in server.events()[mark:] if e.get("state") == 7]
            # Explicitly reconnect the player and reconcile the committed account.
            a.close()
            b = server.client("QLostWrite")
            server.wait("sql_end", since=mark, p=b.p, state=3, seconds=20)
            mark = server.mark(); b.password(False)
            server.wait("verify_end", since=mark, p=b.p, state=7)
            assert s.count("QLostWrite") == 1
            assert int(s.sql("SELECT last_value FROM tutorial_accounts_id_seq")) == before + 1
            return ["real INSERT committed", "wire CommandComplete discarded", "PG_UNCERTAIN observed", "no automatic authentication", "explicit reconnect/lookup/provider verification", "one row before/after reconciliation", "identity sequence advanced once: no replayed INSERT attempt"]
        finally:
            s.env["PGSERVICEFILE"] = original
            for server in s.servers: server.close()
            proxy.close()
    s.case("insert_confirmation_loss", lost_insert_response)

    def reload_hash():
        server = s.server("gmx-provider")
        a = server.client("QReload")
        a.prompt(2); a.control("gmxslow")
        old = a.password()
        mark = server.mark(); server.command("gmx")
        server.wait("init", since=mark)
        server.wait("sql_end", since=mark, p=a.p, state=2)
        now = a.state()
        assert now["token"] > old["token"]
        server.wait("hash_begin", since=mark, p=a.p, callback_token=old["token"], seconds=60)
        after = server.wait("hash_end", since=mark, p=a.p)
        assert after["token"] == now["token"] and after["state"] == 2 and after["account"] == 0
        assert s.count("QReload") == 0
        return ["actual GMX with real provider work pending", "SVar generation survived reload", "old provider callback rejected by new VM"]
    s.case("gmx_pending_provider", reload_hash)

    def shutdown():
        server = s.server("shutdown")
        a = server.client("QShutdown")
        a.prompt(2); a.control("pending")
        server.wait("shutdown_pending")
        mark = server.mark(); server.command("exit")
        server.proc.wait(timeout=10)
        assert server.proc.returncode == 0
        assert not [e for e in server.events()[mark:] if e["event"] == "shutdown_callback"]
        return ["official server exit with SQL pending", "exit 0", "no callback after owner unload"]
    s.case("shutdown_pending_sql", shutdown)

    def official_npc():
        server = s.server("official-npc")
        a = server.client("QNpcControl")
        mark = server.mark(); a.control("npc")
        npc = server.wait("npc_created", since=mark, connected=1, npc=1)
        # NPC_Destroy queues removal; connection flags clear after this callback.
        server.wait("npc_destroyed", since=mark, p=npc["p"])
        server.wait("disconnect_end", since=mark, p=npc["p"])
        server.wait("sql_owner", since=mark, p=npc["p"], connected=0)
        assert a.initial["npc"] == 0 and a.p != npc["p"]
        return ["official NPC_Create/NPC_Destroy executed", "NPC flagged npc=1; real network client npc=0", "NPC evidence does not qualify client protocol or real-player reuse"]
    s.case("official_npc_scope", official_npc)

    if s.config.get("custom_gamemode"):
        s.sql("CREATE TABLE tutorial_custom_accounts(id integer PRIMARY KEY, username text UNIQUE NOT NULL, score integer NOT NULL); CREATE TABLE tutorial_custom_clans(id integer PRIMARY KEY,name varchar(64) NOT NULL); CREATE TABLE tutorial_custom_properties(id integer PRIMARY KEY,owner_id integer NOT NULL REFERENCES tutorial_custom_accounts(id),label text NOT NULL); INSERT INTO tutorial_custom_accounts VALUES(1,'QCustomA',853); INSERT INTO tutorial_custom_clans VALUES(1,'QA clan'); INSERT INTO tutorial_custom_properties VALUES(1,1,'Original'); GRANT SELECT,UPDATE ON tutorial_custom_accounts,tutorial_custom_properties TO qa_runtime; GRANT SELECT ON tutorial_custom_clans TO qa_runtime")

        def custom_reuse(saving=False):
            server = s.server("custom-save" if saving else "custom-load", s.config["custom_gamemode"])
            a = server.client("QCustomA")
            if saving:
                a.send("/pgload")
                server.wait("load_end", p=a.p, account=1)
            lock = s.lock("SHARE" if saving else "ACCESS EXCLUSIVE", "tutorial_custom_accounts,tutorial_custom_properties" if saving else "tutorial_custom_accounts")
            try:
                mark = server.mark(); a.send("/pgsave" if saving else "/pgload")
                submitted = server.wait("submitted", since=mark, p=a.p)
                jobs = [submitted["save"], submitted["property"]] if saving else [submitted["load"]]
                assert all(jobs)
                a.close()
                b = server.client("QCustomB")
                assert a.p == b.p and a.initial["token"] != b.initial["token"]
                mark = server.mark(); s.unlock(lock); lock = None
                event = "save_begin" if saving else "load_begin"
                for job in jobs:
                    server.wait(event, since=mark, callback_job=job, callback_token=a.initial["token"])
                    server.wait("sql_closed", since=mark, job=job, closed=1)
                state = b.state()
                assert state["account"] == 0 and state["score"] == 0
                return ["actual custom.pwn callbacks", f"observed reuse {a.p}", "generation/request ownership rejected stale results", "all retained jobs closed", "RCON gate bypass and retain-work explicitly confined to fixture"]
            finally:
                if lock: s.unlock(lock)
        s.case("custom_reuse_load", custom_reuse)
        s.case("custom_reuse_save", lambda: custom_reuse(True))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", required=True)
    parser.add_argument("--postgres-bin", default="")
    args = parser.parse_args()
    if "OMP_PG_TEST_SERVICE" not in os.environ or "PGSERVICEFILE" not in os.environ:
        parser.error("Use run_postgres.py to create a disposable cluster first")
    config = json.loads(Path(args.config).read_text(encoding="utf8"))
    suite = Suite(config, args.postgres_bin)
    run_cases(suite)
    suite.save()
    return 1 if any(r["status"] != "PASS" for r in suite.results) else 0


if __name__ == "__main__":
    raise SystemExit(main())
