"""Check fault boundaries and cleanup independently of the libpq integration suite."""

from pathlib import Path
import socket
import struct
import tempfile
import time
import unittest

from stall_server import StallServer


class StallServerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="omp-pg-stall-unit-")
        self.addCleanup(self.temporary.cleanup)

    def start(self, mode):
        server = StallServer(mode, Path(self.temporary.name) / mode)
        self.addCleanup(server.close)
        client = socket.create_connection(("127.0.0.1", server.port), timeout=2)
        self.addCleanup(client.close)
        return server, client

    def until(self, condition):
        deadline = time.monotonic() + 3
        while not condition():
            self.assertLess(time.monotonic(), deadline, "Fixture deadline")
            time.sleep(0.005)

    def test_ssl_request_fragmentation_and_peer_close(self):
        server, client = self.start("ssl")
        request = struct.pack("!II", 8, 80877103)
        client.sendall(request[:4])
        client.settimeout(0.05)
        with self.assertRaises(TimeoutError):
            client.recv(1)
        self.assertFalse(list(server.directory.glob("ready-*")))
        client.sendall(request[4:])
        self.until(lambda: (server.directory / "ready-1").exists())
        with self.assertRaises(TimeoutError):
            client.recv(1)
        client.close()
        self.until(lambda: (server.directory / "closed-1").exists())
        self.assertFalse(server.errors)

    def test_tls_waits_for_client_hello(self):
        server, client = self.start("tls")
        client.sendall(struct.pack("!II", 8, 80877103))
        self.assertEqual(client.recv(1), b"S")
        self.assertFalse(list(server.directory.glob("ready-*")))
        client.sendall(b"\x16\x03\x01\x00\x01\x01")
        self.until(lambda: (server.directory / "ready-1").exists())
        client.settimeout(0.05)
        with self.assertRaises(TimeoutError):
            client.recv(1)
        self.assertFalse(server.errors)

    def test_startup_withholds_authentication_and_closes_active_peer(self):
        server, client = self.start("startup")
        payload = b"user\0omp_test\0\0"
        client.sendall(struct.pack("!II", 8 + len(payload), 196608) + payload)
        self.until(lambda: (server.directory / "ready-1").exists())
        client.settimeout(0.05)
        with self.assertRaises(TimeoutError):
            client.recv(1)
        server.close()
        self.assertFalse(server.thread.is_alive())
        self.assertTrue(all(not worker.is_alive() for worker in server.workers))
        self.assertEqual(client.recv(1), b"")
        self.assertFalse(server.errors)

    def test_partial_peer_shutdown_does_not_claim_boundary(self):
        server, client = self.start("ssl")
        client.sendall(b"\0\0")
        client.close()
        self.until(lambda: (server.directory / "closed-1").exists())
        self.assertFalse(list(server.directory.glob("ready-*")))
        self.assertFalse(server.errors)

    def test_malformed_startup_fails_fixture(self):
        server, client = self.start("startup")
        client.sendall(struct.pack("!II", 10001, 196608))
        self.until(lambda: (server.directory / "closed-1").exists())
        self.assertTrue(server.errors)
        self.assertFalse(list(server.directory.glob("ready-*")))


if __name__ == "__main__":
    unittest.main()
