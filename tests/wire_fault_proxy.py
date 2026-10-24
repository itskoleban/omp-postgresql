"""Bounded loopback proxy faults, never a physical network partition.

Framed scenarios deliberately disable TLS only on their disposable profile.
tls_hold relays opaque TLS bytes and never terminates or inspects encryption.
No SQL, credentials, keys, startup payloads or bind values are logged.
"""

from datetime import datetime, timezone
import json
from pathlib import Path
import socket
import struct
import threading
import time


class WireFaultProxy:
    MODES = {"lose", "hold", "partial_parse", "fragment", "tls_hold"}

    def __init__(self, port, directory, mode, tag=b"INSERT 0 1"):
        if mode not in self.MODES or tag not in (
            b"BEGIN",
            b"COMMIT",
            b"ROLLBACK",
            b"SAVEPOINT",
            b"RELEASE",
            b"INSERT 0 1",
        ):
            raise ValueError("Unsupported bounded wire fault")
        self.directory = Path(directory)
        self.directory.mkdir(parents=True, exist_ok=False)
        self.mode, self.tag, self.upstream_port = mode, tag, port
        self.stop = threading.Event()
        self.injected = False
        self.lock = threading.Lock()
        self.sockets, self.workers, self.errors = [], [], []
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen()
        self.listener.settimeout(0.1)
        self.port = self.listener.getsockname()[1]
        self.thread = threading.Thread(target=self.accept)
        self.thread.start()

    def event(self, event, **fields):
        with self.lock:
            with (self.directory / "wire.jsonl").open("a", encoding="utf8") as stream:
                stream.write(
                    json.dumps(
                        dict(
                            event=event,
                            utc=datetime.now(timezone.utc).isoformat(),
                            monotonic_ns=time.monotonic_ns(),
                            **fields
                        ),
                        sort_keys=True,
                    )
                    + "\n"
                )
            if event in ("partial_parse", "command_response_withheld", "opaque_tls_response_held"):
                (self.directory / "injected").touch()

    def claim(self):
        with self.lock:
            if self.injected:
                return False
            self.injected = True
            return True

    def accept(self):
        while not self.stop.is_set():
            try:
                client, _ = self.listener.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            with self.lock:
                self.sockets.append(client)
            try:
                upstream = socket.create_connection(("127.0.0.1", self.upstream_port), timeout=2)
            except OSError:
                client.close()
                self.event("upstream_unavailable")
                continue
            with self.lock:
                self.sockets.append(upstream)
            self.event("accepted")
            for source, target, backend in ((client, upstream, False), (upstream, client, True)):
                source.settimeout(0.2)
                worker = threading.Thread(target=self.relay, args=(source, target, backend))
                self.workers.append(worker)
                worker.start()

    def read(self, source, size):
        result = bytearray()
        deadline = time.monotonic() + 15
        while len(result) < size and not self.stop.is_set():
            try:
                chunk = source.recv(size - len(result))
            except socket.timeout:
                if time.monotonic() >= deadline:
                    raise TimeoutError("Bounded proxy read deadline")
                continue
            if not chunk:
                raise EOFError
            result.extend(chunk)
        if len(result) != size:
            raise EOFError
        return bytes(result)

    def send(self, target, data):
        step = 7 if self.mode == "fragment" else len(data)
        for offset in range(0, len(data), step):
            target.sendall(data[offset : offset + step])

    def relay(self, source, target, backend):
        held = False
        try:
            if self.mode == "tls_hold":
                while not self.stop.is_set():
                    try:
                        chunk = source.recv(65536)
                    except socket.timeout:
                        continue
                    if not chunk:
                        break
                    if backend and (self.directory / "arm").exists() and self.claim():
                        held = True
                        self.event("opaque_tls_response_held", bytes=len(chunk))
                    if not held:
                        target.sendall(chunk)
                return
            if not backend:
                header = self.read(source, 4)
                length = struct.unpack("!I", header)[0]
                if not 8 <= length <= 65536:
                    raise ValueError("Invalid startup length")
                self.send(target, header + self.read(source, length - 4))
            while not self.stop.is_set():
                header = self.read(source, 5)
                length = struct.unpack("!I", header[1:])[0]
                if not 4 <= length <= 16 * 1024 * 1024:
                    raise ValueError("Invalid protocol length")
                frame = header + self.read(source, length - 4)
                if backend and header[:1] == b"K" and length == 12:
                    # Backend PID is public diagnostic identity; secret key is excluded.
                    self.event("backend_identity", pid=struct.unpack("!I", frame[5:9])[0])
                if (
                    not backend
                    and self.mode == "partial_parse"
                    and header[:1] == b"P"
                    and b"phase2_fault" in frame
                    and self.claim()
                ):
                    self.send(target, frame[:-1])
                    self.event("partial_parse", sent=len(frame) - 1, total=len(frame))
                    return
                if (
                    backend
                    and self.mode in ("lose", "hold")
                    and header[:1] == b"C"
                    and frame[5:] == self.tag + b"\0"
                    and self.claim()
                ):
                    self.event(
                        "command_response_withheld",
                        command=self.tag.decode("ascii"),
                        mode=self.mode,
                    )
                    if self.mode == "lose":
                        return
                    held = True
                if not held:
                    self.send(target, frame)
        except (EOFError, OSError):
            pass  # Expected transport teardown, including the injected close.
        except Exception as error:
            self.errors.append(type(error).__name__)
            self.event("fixture_error", error=type(error).__name__)
        finally:
            self.event("relay_closed", backend=backend)
            for peer in (source, target):
                try:
                    peer.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                peer.close()

    def close(self):
        self.stop.set()
        self.listener.close()
        self.thread.join(3)
        with self.lock:
            peers = list(self.sockets)
        for peer in peers:
            try:
                peer.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            peer.close()
        for worker in self.workers:
            worker.join(3)
        if self.thread.is_alive() or any(w.is_alive() for w in self.workers):
            raise RuntimeError("Wire proxy worker did not stop")
        if self.errors:
            raise RuntimeError("Wire proxy fixture failed: " + ",".join(self.errors))
        self.event("fixture_closed", workers=len(self.workers), injected=self.injected)
