"""Actual fixture certificates must cover the maximum soak and bounded cleanup."""

from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import time
import unittest

from postgres_tls import create_certificates


@unittest.skipUnless(shutil.which("openssl"), "OpenSSL is required to verify fixture certificates")
class PostgresTlsTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix="omp-pg-tls-lifetime-")
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.data = self.root / "data"
        self.data.mkdir()
        create_certificates(lambda argv, **kwargs: subprocess.run(argv, check=True, **kwargs), self.root, self.data)

    def verify(self, certificate, *options):
        return subprocess.run([
            "openssl", "verify", *options, str(certificate),
        ], capture_output=True, text=True, timeout=10)

    def test_server_and_client_trust_survives_full_campaign_budget(self):
        now = int(time.time())
        for certificate in (self.data / "server.crt", self.root / "client.crt"):
            # Include the full 1500-minute workflow limit, not just the load's
            # 86400-second duration and its 900-second integration allowance.
            for horizon, expected in ((86400 + 900, 0), (1500 * 60, 0), (2 * 86400 + 900, 2)):
                with self.subTest(certificate=certificate.name, horizon=horizon):
                    result = self.verify(certificate, "-attime", str(now + horizon),
                                         "-CAfile", str(self.data / "server.crt"))
                    self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
                    if expected:
                        self.assertIn("certificate has expired", result.stderr)

    def test_hostname_and_client_chain_require_the_trusted_authority(self):
        authority = self.data / "server.crt"
        server = self.verify(authority, "-CAfile", str(authority),
                             "-purpose", "sslserver", "-verify_hostname", "localhost")
        self.assertEqual(server.returncode, 0, server.stdout + server.stderr)
        client = self.verify(self.root / "client.crt", "-CAfile", str(authority),
                             "-purpose", "sslclient")
        self.assertEqual(client.returncode, 0, client.stdout + client.stderr)
        wrong_host = self.verify(authority, "-CAfile", str(authority),
                                 "-purpose", "sslserver", "-verify_hostname", "wrong.example")
        self.assertNotEqual(wrong_host.returncode, 0, wrong_host.stdout + wrong_host.stderr)
        self.assertIn("hostname mismatch", wrong_host.stderr)
        subprocess.run([
            "openssl", "req", "-x509", "-newkey", "rsa:2048", "-noenc", "-days", "2",
            "-subj", "/CN=untrusted-fixture-ca", "-keyout", str(self.root / "other.key"),
            "-out", str(self.root / "other.crt"),
        ], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10)
        for certificate in (authority, self.root / "client.crt"):
            with self.subTest(certificate=certificate.name):
                untrusted = self.verify(certificate, "-CAfile", str(self.root / "other.crt"))
                self.assertNotEqual(untrusted.returncode, 0, untrusted.stdout + untrusted.stderr)

    def test_generated_keys_match_certificates_and_posix_permissions(self):
        for key, certificate in ((self.data / "server.key", self.data / "server.crt"),
                                 (self.root / "client.key", self.root / "client.crt")):
            with self.subTest(key=key.name):
                public_key = subprocess.check_output([
                    "openssl", "pkey", "-in", str(key), "-pubout",
                ], timeout=10)
                certificate_key = subprocess.check_output([
                    "openssl", "x509", "-in", str(certificate), "-pubkey", "-noout",
                ], timeout=10)
                self.assertEqual(public_key, certificate_key)
                if os.name != "nt":
                    # Windows chmod does not implement POSIX access modes. Its
                    # fixture relies on its temporary-directory ACL instead.
                    self.assertEqual(key.stat().st_mode & 0o777, 0o600)


if __name__ == "__main__":
    unittest.main()
