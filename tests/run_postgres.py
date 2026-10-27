"""Run CTest against a disposable, loopback-only PostgreSQL cluster with TLS.

Requires initdb, pg_ctl, psql and openssl on PATH. Never opens an existing cluster.
The temporary directory is retained for inspection; server is always stopped.
"""

import argparse
import csv
import json
import os
from pathlib import Path
import platform
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
from commit_proxy import CommitLossProxy
from ctest_qualification import qualify_ctest
from stall_server import StallServer
from wire_fault_proxy import WireFaultProxy
from network_diagnostic import Collector as NetworkDiagnostic
from postgres_tls import create_certificates

parser = argparse.ArgumentParser()
parser.add_argument("--build", default="build")
parser.add_argument("--network-diagnostics", action="store_true",
                    help="Observe one configured Linux ASan network case without retries")
parser.add_argument(
    "--openssl", default="openssl", help="OpenSSL tool path; no change to DLL search PATH"
)
parser.add_argument(
    "--postgres-bin", help="Server tools directory; does not alter client DLL search PATH"
)
parser.add_argument("--benchmark", action="store_true")
parser.add_argument("--benchmark-rows", type=int, default=1000)
parser.add_argument("--benchmark-repetitions", type=int, default=7)
parser.add_argument("--config", default="Release", help="CTest build configuration")
parser.add_argument(
    "--doc-sql", action="store_true", help="Check documented SQL in this disposable cluster"
)
parser.add_argument("--openmp-dir", help="Disposable extracted open.mp server for end-to-end smoke")
parser.add_argument("--component")
parser.add_argument("--real-sessions", help="JSON configuration for the optional real network client suite")
parser.add_argument("--only-real-sessions", action="store_true")
parser.add_argument("--network-only", action="store_true", help="Run only the bounded wire regression in a fresh cluster")
parser.add_argument("--gamemode")
parser.add_argument("--runtime", action="append", default=[])
parser.add_argument("--unload-script")
parser.add_argument("--callback-gamemode")
parser.add_argument("--extension-gamemode")
parser.add_argument("--bootstrap-gamemode")
parser.add_argument("--section-gamemode")
parser.add_argument("--logging-gamemode")
parser.add_argument("--logging-performance-gamemode", help="Compiled tests/logging_performance.pwn")
parser.add_argument("--logging-performance-csv", help="Raw real-Pawn measurement CSV output")
parser.add_argument("--section-sql", help="accounts.sql from the source or extracted package")
parser.add_argument(
    "--tutorial-dir", help="Directory containing basic/values/saving/transfer AMX files"
)
parser.add_argument(
    "--tutorial-prefix", default="", help="Optional packaged- prefix on tutorial AMX names"
)
parser.add_argument("--tutorial-sql-dir", help="Directory containing tutorial-select.sql")
parser.add_argument(
    "--only-openmp",
    action="store_true",
    help="Use fresh PostgreSQL fixture and run only server smoke",
)
args = parser.parse_args()
if args.only_real_sessions and not args.real_sessions:
    parser.error("--only-real-sessions requires --real-sessions")
if bool(args.logging_performance_gamemode) != bool(args.logging_performance_csv):
    parser.error(
        "--logging-performance-gamemode and --logging-performance-csv must be supplied together"
    )
if args.logging_performance_gamemode and not args.openmp_dir:
    parser.error("--logging-performance-gamemode requires --openmp-dir")
if not 1 <= args.benchmark_rows <= 10000 or not 1 <= args.benchmark_repetitions <= 100:
    parser.error("Benchmark rows must be 1..10000 and repetitions 1..100")
flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
command_number = 0


def run(argv, **kwargs):
    global command_number
    if argv[0] == "openssl":
        argv = [args.openssl, *argv[1:]]
    if args.postgres_bin and argv[0] in ("initdb", "pg_ctl", "psql"):
        argv = [
            str(
                Path(args.postgres_bin).resolve() / (argv[0] + (".exe" if os.name == "nt" else ""))
            ),
            *argv[1:],
        ]
    if "stdout" in kwargs:
        return subprocess.run(argv, check=True, creationflags=flags, **kwargs)
    command_number += 1
    # pg_ctl's child server can inherit a Windows stdout pipe, keeping communicate()
    # blocked after pg_ctl exits. A regular log file has no pipe EOF dependency.
    log = root / f"command-{command_number}.log"
    with log.open("wb") as output:
        result = subprocess.run(
            argv, creationflags=flags, stdout=output, stderr=subprocess.STDOUT, **kwargs
        )
    print(log.read_text(encoding="utf8", errors="replace"), flush=True)
    if result.returncode and Path(argv[0]).stem == "pg_ctl":
        server_log = root / "server.log"
        if server_log.is_file():
            print(server_log.read_text(encoding="utf8", errors="replace"), flush=True)
    result.check_returncode()
    return result


def grant_windows_fixture_access(directory):
    # initdb drops administrator privileges. Python 3.13's owner-only temporary
    # ACL may otherwise deny that token when the directory owner is Administrators.
    # Grant only the current user's SID, inheritable by this fixture's children.
    # The username may use an OEM code page; the SID itself is always ASCII.
    identity = subprocess.check_output(
        ["whoami", "/user", "/fo", "csv", "/nh"],
        creationflags=flags,
    ).decode("ascii", errors="replace")
    fields = next(csv.reader(identity.splitlines()))
    if len(fields) != 2 or not re.fullmatch(r"S-\d+(?:-\d+)+", fields[1]):
        raise RuntimeError("Could not determine the current Windows user SID")
    run(["icacls", str(directory), "/grant", f"*{fields[1]}:(OI)(CI)F"])


for program in ("initdb", "pg_ctl", "psql", "openssl", "ctest"):
    if not shutil.which(
        args.openssl if program == "openssl" else program,
        path=(
            args.postgres_bin
            if args.postgres_bin and program in ("initdb", "pg_ctl", "psql")
            else None
        ),
    ):
        raise SystemExit(f"Missing program: {program}")
root = Path(tempfile.mkdtemp(prefix="omp-pg-integration-")).resolve()
if os.name == "nt":
    grant_windows_fixture_access(root)
data = root / "data"
with socket.socket() as sock:
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
run(["initdb", "-D", str(data), "-U", "omp_test", "-A", "trust", "--encoding=UTF8", "--no-locale"])
create_certificates(run, root, data)
(data / "pg_hba.conf").write_text(
    "hostssl all omp_cert 127.0.0.1/32 cert\nhost all omp_password 127.0.0.1/32 scram-sha-256\nhost all all 127.0.0.1/32 trust\n",
    encoding="utf8",
)
with (data / "postgresql.conf").open("a", encoding="utf8") as f:
    f.write(
        f"\nlisten_addresses='127.0.0.1'\nport={port}\nunix_socket_directories=''\n"
        "ssl=on\nssl_ca_file='server.crt'\n"
        "log_timezone='UTC'\nlog_line_prefix='%m [%p] session=%c app=%a peer=%r '\n"
        "log_connections=on\nlog_disconnections=on\n"
    )
env = os.environ.copy()
base = f"host=localhost\nhostaddr=127.0.0.1\nport={port}\ndbname=postgres\nuser=omp_test\n"
cert = (data / "server.crt").as_posix()
service = root / "pg_service.conf"
service.write_text(
    f"[game]\n{base}sslmode=verify-full\nsslrootcert={cert}\n"
    f"[omp_test]\n{base}sslmode=verify-full\nsslrootcert={cert}\n"
    f"[bad_tls]\n{base.replace('host=localhost', 'host=wrong.example')}sslmode=verify-full\nsslrootcert={cert}\n"
    f"[bad_auth]\n{base.replace('user=omp_test', 'user=omp_password')}password=incorrect\nsslmode=verify-full\nsslrootcert={cert}\n"
    f"[client_cert]\n{base.replace('user=omp_test', 'user=omp_cert')}sslmode=verify-full\nsslrootcert={cert}\nsslcert={(root / 'client.crt').as_posix()}\nsslkey={(root / 'client.key').as_posix()}\n"
    f"[bad_client_cert]\n{base.replace('user=omp_test', 'user=omp_cert')}sslmode=verify-full\nsslrootcert={cert}\n"
    "[bad_connect]\nhost=127.0.0.1\nport=1\ndbname=postgres\nuser=omp_test\nconnect_timeout=1\n",
    encoding="utf8",
)
env.update(
    PGSERVICEFILE=str(service),
    OMP_PG_TEST_SERVICE="omp_test",
    OMP_PG_TEST_BAD_TLS="bad_tls",
    OMP_PG_TEST_BAD_AUTH="bad_auth",
    OMP_PG_TEST_BAD_CONNECT="bad_connect",
    OMP_PG_TEST_CERT="client_cert",
    OMP_PG_TEST_BAD_CERT="bad_client_cert",
)
restart_request = root / "restart-request"
env["OMP_PG_TEST_RESTART_REQUEST"] = str(restart_request)
stop_monitor = threading.Event()
restart_errors = []


def restart_monitor():
    while not stop_monitor.wait(0.02):
        if restart_request.exists():
            try:
                run(["pg_ctl", "-D", str(data), "-m", "immediate", "-w", "stop"])
                time.sleep(0.2)
                run(["pg_ctl", "-D", str(data), "-l", str(root / "server.log"), "-w", "start"])
            except Exception as error:
                restart_errors.append(error)
            return


startup_attempted = False
proxy = None
rollback_proxy = None
stall_servers = []
wire_proxies = []
monitor = None
network_diagnostic = None
try:
    startup_attempted = True
    run(["pg_ctl", "-D", str(data), "-l", str(root / "server.log"), "-w", "start"])
    proxy = CommitLossProxy(port)
    with service.open("a", encoding="utf8") as f:
        f.write(
            f"\n[commit_loss]\nhost=127.0.0.1\nport={proxy.port}\ndbname=postgres\nuser=omp_test\nsslmode=disable\ngssencmode=disable\n"
        )
    env["OMP_PG_TEST_COMMIT_LOSS"] = "commit_loss"
    rollback_proxy = CommitLossProxy(port, response_tag=b"ROLLBACK")
    with service.open("a", encoding="utf8") as f:
        f.write(
            f"\n[rollback_loss]\nhost=127.0.0.1\nport={rollback_proxy.port}\n"
            "dbname=postgres\nuser=omp_test\nsslmode=disable\ngssencmode=disable\n"
        )
    env["OMP_PG_TEST_ROLLBACK_LOSS"] = "rollback_loss"
    run(
        [
            "psql",
            "service=omp_test",
            "-v",
            "ON_ERROR_STOP=1",
            "-c",
            "CREATE ROLE omp_password LOGIN PASSWORD 'test-only'; CREATE ROLE omp_cert LOGIN;",
        ],
        env=env,
    )
    if args.doc_sql:
        psql = (
            Path(args.postgres_bin).resolve() / ("psql.exe" if os.name == "nt" else "psql")
            if args.postgres_bin
            else "psql"
        )
        run(
            [
                sys.executable,
                str(Path(__file__).with_name("check_doc_sql.py")),
                "--service",
                "omp_test",
                "--psql",
                str(psql),
            ],
            env=env,
        )
    if not args.only_openmp and not args.only_real_sessions:
        wire_root = root / "wire"
        env["OMP_PG_TEST_WIRE_ROOT"] = str(wire_root)
        for name, mode, tag in (
            ("begin", "lose", b"BEGIN"),
            ("savepoint", "lose", b"SAVEPOINT"),
            ("rollback_to", "lose", b"ROLLBACK"),
            ("release", "lose", b"RELEASE"),
            ("commit", "lose", b"COMMIT"),
            ("rollback", "lose", b"ROLLBACK"),
            ("insert", "lose", b"INSERT 0 1"),
            ("hold", "hold", b"INSERT 0 1"),
            ("partial_parse", "partial_parse", b"INSERT 0 1"),
            ("batch", "lose", b"INSERT 0 1"),
            ("fragment", "fragment", b"INSERT 0 1"),
            ("tls_hold", "tls_hold", b"INSERT 0 1"),
            ("tls_shutdown", "tls_hold", b"INSERT 0 1"),
        ):
            wire = WireFaultProxy(port, wire_root / name, mode, tag)
            wire_proxies.append(wire)
            transport = (
                f"host=localhost\nhostaddr=127.0.0.1\nsslmode=verify-full\nsslrootcert={data / 'server.crt'}\n"
                if mode == "tls_hold" else "host=127.0.0.1\nsslmode=disable\n"
            )
            with service.open("a", encoding="utf8") as f:
                f.write(f"\n[wire_{name}]\n{transport}port={wire.port}\n"
                        "dbname=postgres\nuser=omp_test\ngssencmode=disable\n")
        env["OMP_PG_TEST_STALL_ROOT"] = str(root)
        for mode in ("ssl", "tls", "startup"):
            stalled = StallServer(mode, root / mode)
            stall_servers.append(stalled)
            # Plaintext startup is used only by a non-authenticating fault peer.
            # Real database profiles above retain verify-full TLS.
            sslmode = "disable" if mode == "startup" else "require"
            with service.open("a", encoding="utf8") as f:
                f.write(
                    f"\n[stall_{mode}]\nhost=127.0.0.1\nport={stalled.port}\n"
                    f"dbname=postgres\nuser=omp_test\nsslmode={sslmode}\n"
                    "gssencmode=disable\nconnect_timeout=60\n"
                )
        monitor = threading.Thread(target=restart_monitor, daemon=True)
        monitor.start()
        if args.network_diagnostics:
            network_diagnostic = NetworkDiagnostic(args.build, env, root / "server.log")
            network_diagnostic.start()
        qualify_ctest(args.build, args.config, env, run,
                      test_regex="^network_recovery_integration$" if args.network_only else None)
        if any(not wire.injected for wire in wire_proxies if wire.mode != "fragment"):
            raise RuntimeError("Required wire fault was not exercised")
        if restart_errors:
            raise RuntimeError("PostgreSQL restart fixture failed") from restart_errors[0]
        if not args.network_only and not proxy.dropped.is_set():
            raise RuntimeError("COMMIT response loss was not exercised")
        if not args.network_only and not rollback_proxy.dropped.is_set():
            raise RuntimeError("ROLLBACK response loss was not exercised")
        for stalled in ([] if args.network_only else stall_servers):
            if stalled.errors or len(list(stalled.directory.glob("ready-*"))) != 20:
                raise RuntimeError(f"Stalled {stalled.mode} handshakes were not fully exercised")
    elif not args.openmp_dir and not args.only_real_sessions:
        raise ValueError("--only-openmp requires --openmp-dir")
    if args.real_sessions:
        run(
            [sys.executable, str(Path(__file__).with_name("run_real_sessions.py")),
             "--config", args.real_sessions, "--postgres-bin", args.postgres_bin or ""],
            env=env,
        )
    if args.benchmark:
        print(
            f"Benchmark host: {platform.platform()}; machine={platform.machine()}; "
            f"processor={platform.processor()}; logical_cpus={os.cpu_count()}; "
            "network=loopback; TLS=verify-full; RTT=unmeasured",
            flush=True,
        )
        for executable, arguments in (
            ("pg_benchmark", [str(args.benchmark_rows)]),
            (
                "pg_engine_benchmark",
                [str(args.benchmark_rows), str(args.benchmark_repetitions)],
            ),
        ):
            binary = Path(args.build).resolve() / (executable + (".exe" if os.name == "nt" else ""))
            if not binary.is_file():
                binary = binary.parent / args.config / binary.name
            run([str(binary), *arguments], env=env, timeout=1800)
    if args.openmp_dir:
        info = Path(args.build).resolve() / f"package-build-info-{args.config}.json"
        expected_version = json.loads(info.read_text(encoding="utf8"))["version"]
        env["OMP_PG_EXPECTED_VERSION"] = expected_version
        if not args.component or not args.gamemode:
            raise ValueError("--openmp-dir requires --component and --gamemode")
        command = [
            sys.executable,
            str(Path(__file__).with_name("run_openmp.py")),
            "--server",
            args.openmp_dir,
            "--component",
            args.component,
            "--gamemode",
            args.gamemode,
        ]
        for dll in args.runtime:
            command.extend(("--runtime", dll))
        if args.unload_script:
            command.extend(("--unload-script", args.unload_script))
        run(command, env=env)
        for gamemode, marker in (
            (args.callback_gamemode, "CALLBACK"),
            (args.extension_gamemode, "EXTENSION"),
            (args.bootstrap_gamemode, "BOOTSTRAP"),
            (args.section_gamemode, "SECTION"),
            (args.logging_gamemode, "LOGGING"),
        ):
            if not gamemode:
                continue
            command = [
                sys.executable,
                str(Path(__file__).with_name("run_openmp.py")),
                "--server",
                args.openmp_dir,
                "--component",
                args.component,
                "--gamemode",
                gamemode,
                "--gmx-rounds",
                "0",
                "--success-marker",
                f"PG {marker} PASS",
                "--failure-marker",
                f"PG {marker} FAILED",
            ]
            for dll in args.runtime:
                command.extend(("--runtime", dll))
            if marker in ("SECTION", "LOGGING"):
                if not args.section_sql:
                    raise ValueError("--section-gamemode requires --section-sql")
                command.extend(("--sql-file", args.section_sql))
            run(command, env=env)
        if args.logging_performance_gamemode:
            command = [
                sys.executable,
                str(Path(__file__).with_name("run_openmp.py")),
                "--server",
                args.openmp_dir,
                "--component",
                args.component,
                "--gamemode",
                args.logging_performance_gamemode,
                "--gmx-rounds",
                "0",
                "--success-marker",
                "PG PERFORMANCE PASS",
                "--failure-marker",
                "PG PERFORMANCE FAILED",
                "--performance-csv",
                args.logging_performance_csv,
            ]
            for dll in args.runtime:
                command.extend(("--runtime", dll))
            run(command, env=env)
        if args.tutorial_dir:
            if not args.tutorial_sql_dir:
                raise ValueError("--tutorial-dir requires --tutorial-sql-dir")
            # Validate the operator-applied schema only in this newly created cluster.
            # This does not exercise interactive players or the external hashing provider.
            run(
                [
                    "psql",
                    "service=omp_test",
                    "-v",
                    "ON_ERROR_STOP=1",
                    "-f",
                    str(Path(args.tutorial_sql_dir) / "tutorial-auth.sql"),
                ],
                env=env,
            )
            for tutorial in ("basic", "values", "saving", "transfer"):
                command = [
                    sys.executable,
                    str(Path(__file__).with_name("run_openmp.py")),
                    "--server",
                    args.openmp_dir,
                    "--component",
                    args.component,
                    "--gamemode",
                    str(Path(args.tutorial_dir) / f"{args.tutorial_prefix}{tutorial}.amx"),
                    "--gmx-rounds",
                    "0",
                    "--success-marker",
                    f"PG {tutorial.upper()} PASS",
                    "--failure-marker",
                    f"PG {tutorial.upper()} FAILED",
                ]
                for dll in args.runtime:
                    command.extend(("--runtime", dll))
                if tutorial == "saving":
                    command.extend(
                        ("--sql-file", str(Path(args.tutorial_sql_dir) / "tutorial-select.sql"))
                    )
                run(command, env=env)
finally:
    exception_in_flight = sys.exc_info()[0] is not None
    network_diagnostic_ok = True
    if network_diagnostic:
        network_diagnostic_ok = network_diagnostic.finish()
    stop_monitor.set()
    if monitor:
        monitor.join(timeout=15)
    if proxy:
        proxy.close()
    if rollback_proxy:
        rollback_proxy.close()
    wire_errors = []
    for wire in wire_proxies:
        try:
            wire.close()
        except Exception as error:
            wire_errors.append(str(error))
        finally:
            log_directory = Path(args.build) / "Testing" / "Temporary"
            log_directory.mkdir(parents=True, exist_ok=True)
            if (wire.directory / "wire.jsonl").is_file():
                shutil.copyfile(wire.directory / "wire.jsonl", log_directory / f"wire-{wire.directory.name}.log")
    for stalled in stall_servers:
        try:
            stalled.close()
            # Use the existing CI log artifact paths on every Python/toolchain variant.
            log_directory = Path(args.build) / "Testing" / "Temporary"
            log_directory.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(
                root / f"stall-{stalled.mode}.log", log_directory / f"stall-{stalled.mode}.log"
            )
        except Exception as error:
            stalled.errors.append(str(error))
            print(f"Stall fixture cleanup/log collection failed: {error}", file=sys.stderr)
    # A failed startup wait can leave the postmaster alive. This PID file can only
    # belong to the disposable cluster initialized above, never an existing one.
    if startup_attempted and (data / "postmaster.pid").is_file():
        try:
            run(["pg_ctl", "-D", str(data), "-m", "immediate", "-w", "stop"])
        except subprocess.CalledProcessError:
            if not exception_in_flight:
                raise
    print(f"Isolated test artifacts: {root}")
    if not network_diagnostic_ok:
        if exception_in_flight:
            print("Network diagnostic collection failed; original test failure retained", file=sys.stderr)
        else:
            raise RuntimeError("Network diagnostic collection failed; inspect collection.json")
    if wire_errors:
        if exception_in_flight:
            print("Wire cleanup errors: " + "; ".join(wire_errors), file=sys.stderr)
        else:
            raise RuntimeError("Wire cleanup failed: " + "; ".join(wire_errors))
    if not exception_in_flight and any(stalled.errors for stalled in stall_servers):
        raise RuntimeError("Stall fixture failed; inspect stall-*.log in the fixture directory")
