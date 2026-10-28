"""Issue certificates used only by the disposable loopback PostgreSQL fixture."""

import os
import subprocess


def create_certificates(run, root, data):
    # A 24-hour load is followed by lifecycle connections and bounded cleanup.
    # Keep both disposable certificates valid for that complete fixture lifetime.
    quiet = {"stdout": subprocess.DEVNULL, "stderr": subprocess.DEVNULL}
    run([
        "openssl", "req", "-x509", "-newkey", "rsa:2048", "-noenc",
        "-days", "2", "-subj", "/CN=localhost",
        "-addext", "subjectAltName=DNS:localhost",
        "-keyout", str(data / "server.key"), "-out", str(data / "server.crt"),
    ], **quiet)
    os.chmod(data / "server.key", 0o600)
    run([
        "openssl", "req", "-new", "-newkey", "rsa:2048", "-noenc",
        "-subj", "/CN=omp_cert", "-keyout", str(root / "client.key"),
        "-out", str(root / "client.csr"),
    ], **quiet)
    run([
        "openssl", "x509", "-req", "-in", str(root / "client.csr"),
        "-CA", str(data / "server.crt"), "-CAkey", str(data / "server.key"),
        "-CAcreateserial", "-days", "2", "-out", str(root / "client.crt"),
    ], **quiet)
    os.chmod(root / "client.key", 0o600)
