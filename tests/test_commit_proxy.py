"""Protocol fault fixture regression: real sockets, fragmented server frames."""
import socket
import struct
import threading
import unittest
from commit_proxy import CommitLossProxy


def frame(tag, body):
    return tag + struct.pack("!I", len(body) + 4) + body


class CommitProxyTests(unittest.TestCase):
    def test_only_selected_finish_response_is_discarded(self):
        for selected, ordinary in ((b"ROLLBACK", b"COMMIT"), (b"COMMIT", b"ROLLBACK")):
            with self.subTest(selected=selected):
                upstream = socket.socket()
                upstream.bind(("127.0.0.1", 0))
                upstream.listen()
                upstream.settimeout(3)
                proxy = CommitLossProxy(upstream.getsockname()[1], selected)
                errors = []
                kept = frame(b"C", ordinary + b"\0")
                lost = frame(b"C", selected + b"\0")

                def server():
                    try:
                        with upstream.accept()[0] as peer:
                            peer.settimeout(3)
                            if peer.recv(1) != b"x":
                                raise AssertionError("Frontend byte not forwarded")
                            for byte in kept + lost:
                                peer.sendall(bytes([byte]))
                    except Exception as error:
                        errors.append(error)

                worker = threading.Thread(target=server)
                worker.start()
                try:
                    with socket.create_connection(("127.0.0.1", proxy.port), timeout=3) as client:
                        client.sendall(b"x")
                        received = b""
                        while chunk := client.recv(1024):
                            received += chunk
                    self.assertEqual(received, kept, "Wrong frame forwarded/discarded")
                    self.assertTrue(proxy.dropped.wait(1), "Fault never injected")
                finally:
                    proxy.close()
                    upstream.close()
                    worker.join(4)
                self.assertFalse(worker.is_alive(), "Fake backend leaked a worker")
                self.assertEqual(errors, [])

    def test_invalid_tag_is_rejected_before_opening_listener(self):
        with self.assertRaises(ValueError):
            CommitLossProxy(1, b"SELECT")


if __name__ == "__main__":
    unittest.main()
