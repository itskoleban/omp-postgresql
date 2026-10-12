"""Test-only PostgreSQL proxy: discard a COMMIT CommandComplete after the server
has executed it. Plain loopback transport only; do not deploy as infrastructure.
"""

import socket
import struct
import threading


class CommitLossProxy:
    def __init__(self, port):
        self.upstream_port = port
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen()
        self.listener.settimeout(0.1)
        self.port = self.listener.getsockname()[1]
        self.stopping = threading.Event()
        self.dropped = threading.Event()
        self.sockets = []
        self.lock = threading.Lock()
        self.thread = threading.Thread(target=self.accept, daemon=True)
        self.thread.start()

    def accept(self):
        while not self.stopping.is_set():
            try:
                client, _ = self.listener.accept()
            except (TimeoutError, socket.timeout):
                continue
            except OSError:
                return
            upstream = socket.create_connection(("127.0.0.1", self.upstream_port))
            with self.lock:
                self.sockets.extend((client, upstream))
            threading.Thread(target=self.relay, args=(client, upstream, False), daemon=True).start()
            threading.Thread(target=self.relay, args=(upstream, client, True), daemon=True).start()

    def relay(self, source, target, inspect):
        buffer = b""
        try:
            while not self.stopping.is_set():
                chunk = source.recv(65536)
                if not chunk:
                    break
                if not inspect:
                    target.sendall(chunk)
                    continue
                buffer += chunk
                while len(buffer) >= 5:
                    length = struct.unpack("!I", buffer[1:5])[0]
                    if length < 4 or length > 16 * 1024 * 1024:
                        raise ValueError("Invalid PostgreSQL frame in test proxy")
                    if len(buffer) < length + 1:
                        break
                    frame, buffer = buffer[: length + 1], buffer[length + 1 :]
                    if frame[:1] == b"C" and frame[5:] == b"COMMIT\0":
                        self.dropped.set()
                        return  # Close both sockets before client receives confirmation.
                    target.sendall(frame)
        except (OSError, ValueError):
            pass
        finally:
            for sock in (source, target):
                try:
                    sock.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                sock.close()

    def close(self):
        self.stopping.set()
        self.listener.close()
        self.thread.join(timeout=2)
        with self.lock:
            for sock in self.sockets:
                try:
                    sock.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                sock.close()
