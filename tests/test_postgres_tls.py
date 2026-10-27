"""Actual fixture certificates must cover the maximum soak and bounded cleanup."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest

from postgres_tls import create_certificates


@unittest.skipUnless(shutil.which("openssl"), "OpenSSL is required to verify fixture certificates")
class PostgresTlsTests(unittest.TestCase):
    def test_server_and_client_trust_survives_full_campaign_budget(self):
        with tempfile.TemporaryDirectory(prefix="omp-pg-tls-lifetime-") as directory:
            root = Path(directory)
            data = root / "data"
            data.mkdir()
            create_certificates(lambda argv, **kwargs: subprocess.run(argv, check=True, **kwargs), root, data)
            now = int(time.time())
            for certificate in (data / "server.crt", root / "client.crt"):
                for horizon, expected in ((86400 + 900, 0), (2 * 86400 + 900, 2)):
                    with self.subTest(certificate=certificate.name, horizon=horizon):
                        result = subprocess.run([
                            "openssl", "verify", "-attime", str(now + horizon),
                            "-CAfile", str(data / "server.crt"), str(certificate),
                        ], capture_output=True, text=True, timeout=10)
                        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
                        if expected:
                            self.assertIn("certificate has expired", result.stderr)


if __name__ == "__main__":
    unittest.main()
