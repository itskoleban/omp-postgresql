"""Loopback-only fault fixture; never connects to a database or accepts credentials.

Hold libpq at SSLRequest, TLS ClientHello, or plaintext StartupMessage. Marker files
acknowledge the actual protocol boundary, so tests need no timing-based startup sleeps.
"""

from pathlib import Path
import json
import socket
import struct
import threading
import time


class StallServer:
    def __init__(self, mode, directory):
        if mode not in ("ssl", "tls", "startup"):
            raise ValueError("Unknown stalled connection phase")
        self.mode = mode
        self.directory = Path(directory)
        self.directory.mkdir()
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(32)
        self.listener.settimeout(0.1)
        self.port = self.listener.getsockname()[1]
        self.stopping = threading.Event()
        self.lock = threading.Lock()
        self.clients = set()
        self.workers = []
        self.errors = []
        self.accepted = 0
        self.timeline = []
        self.timeline_dropped = 0
        self.timeline_lock = threading.Lock()
        self.record("listening")
        self.thread = threading.Thread(target=self.accept, daemon=True)
        self.thread.start()

    def record(self, event, peer=0):
        # Numeric metadata only. At most 256 peers produce three events each.
        with self.timeline_lock:
            if len(self.timeline) < 2048:
                self.timeline.append(dict(seq=len(self.timeline), event=event, mode=self.mode,
                                          peer=peer, monotonic_ns=time.monotonic_ns(),
                                          utc_ns=time.time_ns()))
            else:
                self.timeline_dropped += 1

    def mark_ready(self, number):
        (self.directory / f"ready-{number}").touch()
        self.record("ready", number)

    def accept(self):
        while not self.stopping.is_set():
            try:
                client, _ = self.listener.accept()
            except TimeoutError:
                continue
            except OSError as error:
                if not self.stopping.is_set():
                    self.errors.append(str(error))
                return
            with self.lock:
                self.accepted += 1
                number = self.accepted
                if number > 256:
                    client.close()
                    self.errors.append("Stall fixture connection limit exceeded")
                    return
                self.clients.add(client)
            try:
                (self.directory / f"accepted-{number}").touch()
            except OSError as error:
                # Observation failure must be explicit without changing the peer's
                # protocol behavior or killing the accept thread silently.
                self.errors.append("Stall accepted marker write failed: " + type(error).__name__)
                self.record("accepted_marker_failed", number)
            self.record("accepted", number)
            worker = threading.Thread(target=self.hold, args=(client, number), daemon=True)
            self.workers.append(worker)
            worker.start()

    def read_exact(self, client, size):
        data = bytearray()
        while len(data) < size and not self.stopping.is_set():
            try:
                chunk = client.recv(size - len(data))
            except TimeoutError:
                continue
            if not chunk:
                raise EOFError
            data.extend(chunk)
        if len(data) != size:
            raise EOFError
        return bytes(data)

    def hold(self, client, number):
        try:
            client.settimeout(0.1)
            header = self.read_exact(client, 8)
            if self.mode in ("ssl", "tls"):
                if header != struct.pack("!II", 8, 80877103):
                    raise ValueError("Expected PostgreSQL SSLRequest")
                if self.mode == "tls":
                    client.sendall(b"S")
                    hello = self.read_exact(client, 6)
                    if hello[:2] != b"\x16\x03" or hello[5] != 1:
                        raise ValueError("Expected TLS handshake record and ClientHello")
            else:
                length, protocol = struct.unpack("!II", header)
                if protocol != 196608 or not 8 < length <= 10000:
                    raise ValueError("Expected bounded PostgreSQL 3.0 StartupMessage")
                self.read_exact(client, length - 8)
            # Never send AuthenticationRequest/ReadyForQuery or any SQL response.
            self.mark_ready(number)
            while not self.stopping.is_set():
                try:
                    if not client.recv(4096):
                        break
                except TimeoutError:
                    continue
        except (EOFError, ConnectionError):
            pass  # libpq timeout/stop closes the test-owned connection.
        except OSError as error:
            if not self.stopping.is_set():
                self.errors.append(str(error))
        except ValueError as error:
            self.errors.append(str(error))
        finally:
            with self.lock:
                self.clients.discard(client)
                client.close()
            (self.directory / f"closed-{number}").touch()
            self.record("closed", number)

    def close(self):
        self.stopping.set()
        self.listener.close()
        self.thread.join(timeout=2)
        with self.lock:
            for client in self.clients:
                try:
                    client.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
        for worker in self.workers:
            worker.join(timeout=2)
        if self.thread.is_alive() or any(worker.is_alive() for worker in self.workers):
            self.errors.append("Stall fixture thread did not stop")
        ready = len(list(self.directory.glob("ready-*")))
        closed = len(list(self.directory.glob("closed-*")))
        summary = f"mode={self.mode} accepted={self.accepted} ready={ready} closed={closed}"
        with self.timeline_lock:
            events = list(self.timeline)
            dropped = self.timeline_dropped
        timeline = self.directory.parent / f"stall-{self.mode}-events.log"
        with timeline.open("w", encoding="utf8") as output:
            for event in events:
                output.write(json.dumps(event) + "\n")
            output.write(json.dumps(dict(event="footer", records=len(events), dropped=dropped)) + "\n")
        if dropped:
            self.errors.append("Stall diagnostic timeline overflow")
        (self.directory.parent / f"stall-{self.mode}.log").write_text(
            summary + "\n" + "\n".join(self.errors), encoding="utf8"
        )
        print(summary, flush=True)
