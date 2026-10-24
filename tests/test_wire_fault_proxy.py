"""Real socket controls for one-shot faults, partial Parse and bounded teardown."""

import json
from pathlib import Path
import socket
import struct
import tempfile
import threading
import unittest
from wire_fault_proxy import WireFaultProxy


def frame(tag, body):
    return tag + struct.pack("!I", len(body) + 4) + body


class WireProxyTests(unittest.TestCase):
    def exercise(self, mode, backend, expected, frontend=None, check_front=None):
        startup = struct.pack("!II", 8, 196608)
        frontend = frontend or frame(b"P", b"\0SELECT 1\0\0\0")
        with tempfile.TemporaryDirectory() as directory:
            upstream = socket.socket()
            upstream.bind(("127.0.0.1", 0))
            upstream.listen()
            upstream.settimeout(3)
            proxy = WireFaultProxy(upstream.getsockname()[1], Path(directory) / "proxy", mode)
            received, errors = bytearray(), []

            def server():
                try:
                    with upstream.accept()[0] as peer:
                        peer.settimeout(3)
                        expected_size = len(startup) + len(frontend) - (mode == "partial_parse")
                        while len(received) < expected_size:
                            chunk = peer.recv(expected_size - len(received))
                            if not chunk:
                                break
                            received.extend(chunk)
                        if mode != "partial_parse":
                            for byte in backend:
                                peer.sendall(bytes([byte]))
                except Exception as error:
                    errors.append(error)

            worker = threading.Thread(target=server)
            worker.start()
            try:
                with socket.create_connection(("127.0.0.1", proxy.port), timeout=3) as client:
                    client.sendall(startup + frontend)
                    output = bytearray()
                    while chunk := client.recv(4096):
                        output.extend(chunk)
                self.assertEqual(bytes(output), expected)
            finally:
                proxy.close()
                upstream.close()
                worker.join(4)
            self.assertFalse(worker.is_alive())
            self.assertEqual(errors, [])
            if check_front is not None:
                self.assertEqual(bytes(received), startup + check_front)
            records = [
                json.loads(line)
                for line in (proxy.directory / "wire.jsonl").read_text().splitlines()
            ]
            self.assertEqual(records[-1]["event"], "fixture_closed")
            self.assertEqual(records[-1]["injected"], mode != "fragment")
            return records

    def test_selected_response_loss_preserves_prefix(self):
        prefix = frame(b"C", b"SELECT 1\0")
        records = self.exercise("lose", prefix + frame(b"C", b"INSERT 0 1\0"), prefix)
        self.assertEqual(sum(r["event"] == "command_response_withheld" for r in records), 1)

    def test_partial_parse_cannot_complete(self):
        request = frame(b"P", b"\0SELECT 1 /* phase2_fault */\0\0\0")
        records = self.exercise("partial_parse", b"", b"", request, request[:-1])
        fault = next(r for r in records if r["event"] == "partial_parse")
        self.assertEqual(fault["sent"] + 1, fault["total"])

    def test_fragmented_frontend_and_backend(self):
        request = frame(b"P", b"\0SELECT repeat('x',7000)\0\0\0")
        response = frame(b"D", b"x" * 7000) + frame(b"C", b"SELECT 1\0")
        self.exercise("fragment", response, response, request, request)

    def test_hold_discards_remaining_response(self):
        self.exercise("hold", frame(b"C", b"INSERT 0 1\0") + frame(b"Z", b"I"), b"")

    def test_fault_is_one_shot(self):
        with tempfile.TemporaryDirectory() as directory:
            proxy = WireFaultProxy(1, Path(directory) / "proxy", "lose")
            try:
                self.assertTrue(proxy.claim())
                self.assertFalse(proxy.claim())
                self.assertFalse(proxy.claim())
            finally:
                proxy.close()

    def test_unsupported_modes_fail_before_listening(self):
        with self.assertRaises(ValueError):
            WireFaultProxy(1, "unused", "physical_partition")


if __name__ == "__main__":
    unittest.main()
