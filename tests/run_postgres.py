"""Run CTest against a disposable, loopback-only PostgreSQL cluster with TLS.

Requires initdb, pg_ctl, psql and openssl on PATH. Never opens an existing cluster.
The temporary directory is retained for inspection; server is always stopped.
"""

import argparse
import csv
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

parser = argparse.ArgumentParser()
parser.add_argument("--build", default="build")
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
parser.add_argument("--openmp-dir", help="Disposable extracted open.mp server for end-to-end smoke")
parser.add_argument("--component")
parser.add_argument("--gamemode")
parser.add_argument("--runtime", action="append", default=[])
parser.add_argument("--unload-script")
parser.add_argument("--callback-gamemode")
parser.add_argument("--extension-gamemode")
parser.add_argument("--bootstrap-gamemode")
parser.add_argument(
    "--only-openmp",
    action="store_true",
    help="Use fresh PostgreSQL fixture and run only server smoke",
)
args = parser.parse_args()
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
run(
    [
        "openssl",
        "req",
        "-x509",
        "-newkey",
        "rsa:2048",
        "-noenc",
        "-days",
        "1",
        "-subj",
        "/CN=localhost",
        "-addext",
        "subjectAltName=DNS:localhost",
        "-keyout",
        str(data / "server.key"),
        "-out",
        str(data / "server.crt"),
    ],
    stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL,
)
os.chmod(data / "server.key", 0o600)
run(
    [
        "openssl",
        "req",
        "-new",
        "-newkey",
        "rsa:2048",
        "-noenc",
        "-subj",
        "/CN=omp_cert",
        "-keyout",
        str(root / "client.key"),
        "-out",
        str(root / "client.csr"),
    ],
    stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL,
)
run(
    [
        "openssl",
        "x509",
        "-req",
        "-in",
        str(root / "client.csr"),
        "-CA",
        str(data / "server.crt"),
        "-CAkey",
        str(data / "server.key"),
        "-CAcreateserial",
        "-days",
        "1",
        "-out",
        str(root / "client.crt"),
    ],
    stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL,
)
os.chmod(root / "client.key", 0o600)
(data / "pg_hba.conf").write_text(
    "hostssl all omp_cert 127.0.0.1/32 cert\nhost all omp_password 127.0.0.1/32 scram-sha-256\nhost all all 127.0.0.1/32 trust\n",
    encoding="utf8",
)
with (data / "postgresql.conf").open("a", encoding="utf8") as f:
    f.write(
        f"\nlisten_addresses='127.0.0.1'\nport={port}\nunix_socket_directories=''\n"
        "ssl=on\nssl_ca_file='server.crt'\n"
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
monitor = None
try:
    startup_attempted = True
    run(["pg_ctl", "-D", str(data), "-l", str(root / "server.log"), "-w", "start"])
    proxy = CommitLossProxy(port)
    with service.open("a", encoding="utf8") as f:
        f.write(
            f"\n[commit_loss]\nhost=127.0.0.1\nport={proxy.port}\ndbname=postgres\nuser=omp_test\nsslmode=disable\ngssencmode=disable\n"
        )
    env["OMP_PG_TEST_COMMIT_LOSS"] = "commit_loss"
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
    if not args.only_openmp:
        monitor = threading.Thread(target=restart_monitor, daemon=True)
        monitor.start()
        run(["ctest", "--test-dir", args.build, "-C", args.config, "--output-on-failure"], env=env)
        if restart_errors:
            raise RuntimeError("PostgreSQL restart fixture failed") from restart_errors[0]
        if not proxy.dropped.is_set():
            raise RuntimeError("COMMIT response loss was not exercised")
    elif not args.openmp_dir:
        raise ValueError("--only-openmp requires --openmp-dir")
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
            run(command, env=env)
finally:
    exception_in_flight = sys.exc_info()[0] is not None
    stop_monitor.set()
    if monitor:
        monitor.join(timeout=15)
    if proxy:
        proxy.close()
    # A failed startup wait can leave the postmaster alive. This PID file can only
    # belong to the disposable cluster initialized above, never an existing one.
    if startup_attempted and (data / "postmaster.pid").is_file():
        try:
            run(["pg_ctl", "-D", str(data), "-m", "immediate", "-w", "stop"])
        except subprocess.CalledProcessError:
            if not exception_in_flight:
                raise
    print(f"Isolated test artifacts: {root}")
