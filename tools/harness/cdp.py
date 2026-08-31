"""A small synchronous DevTools Protocol client.

The harness drives a browser it launched itself, over one WebSocket, from one
thread.  That is a narrow enough need that the protocol is spoken directly
rather than through a driver library: it keeps the harness installable with no
packages, and it keeps the browser the developer already has rather than a
second copy downloaded for automation.

What is implemented is the client half of RFC 6455 for a connection that never
negotiates an extension, plus request and event dispatch on top of it.
"""

import base64
import http.client
import json
import os
import socket
import struct
import threading
import time


class ProtocolError(Exception):
    """The browser answered a command with an error, or would not talk at all."""


class WebSocket:
    """A client WebSocket, framing only what a DevTools connection uses."""

    def __init__(self, url, timeout=30.0):
        if not url.startswith("ws://"):
            raise ProtocolError("only an unencrypted DevTools socket is spoken: %s" % url)

        rest = url[len("ws://"):]
        authority, _, path = rest.partition("/")
        host, _, port = authority.partition(":")

        self._socket = socket.create_connection((host, int(port or 80)), timeout=timeout)
        self._socket.settimeout(timeout)
        self._buffer = b""
        self._send_lock = threading.Lock()

        key = base64.b64encode(os.urandom(16)).decode("ascii")
        request = (
            "GET /%s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: %s\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n" % (path, authority, key)
        )
        self._socket.sendall(request.encode("ascii"))

        while b"\r\n\r\n" not in self._buffer:
            chunk = self._socket.recv(4096)
            if not chunk:
                raise ProtocolError("the browser closed the connection during the handshake")
            self._buffer += chunk

        head, _, self._buffer = self._buffer.partition(b"\r\n\r\n")
        status = head.split(b"\r\n", 1)[0]

        if b"101" not in status:
            raise ProtocolError("the browser refused the upgrade: %s" % status.decode("latin-1"))

    def _read(self, count):
        while len(self._buffer) < count:
            chunk = self._socket.recv(65536)
            if not chunk:
                raise ProtocolError("the browser closed the connection")
            self._buffer += chunk

        taken, self._buffer = self._buffer[:count], self._buffer[count:]
        return taken

    def send(self, text):
        payload = text.encode("utf-8")
        length = len(payload)

        if length < 126:
            header = struct.pack(">BB", 0x81, 0x80 | length)
        elif length < 65536:
            header = struct.pack(">BBH", 0x81, 0x80 | 126, length)
        else:
            header = struct.pack(">BBQ", 0x81, 0x80 | 127, length)

        mask = os.urandom(4)
        masked = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))

        with self._send_lock:
            self._socket.sendall(header + mask + masked)

    def receive(self):
        """Returns the next complete text message, reassembling continuations."""

        message = b""

        while True:
            first, second = struct.unpack(">BB", self._read(2))
            final = bool(first & 0x80)
            opcode = first & 0x0F
            length = second & 0x7F

            if length == 126:
                length = struct.unpack(">H", self._read(2))[0]
            elif length == 127:
                length = struct.unpack(">Q", self._read(8))[0]

            if second & 0x80:
                mask = self._read(4)
                body = bytes(byte ^ mask[index % 4]
                             for index, byte in enumerate(self._read(length)))
            else:
                body = self._read(length)

            if opcode == 0x8:
                raise ProtocolError("the browser closed the connection")
            if opcode == 0x9:
                # A pong owes the same body back, and is framed like any other send.
                with self._send_lock:
                    self._socket.sendall(struct.pack(">BB", 0x8A, 0x80 | len(body)) +
                                         b"\x00\x00\x00\x00" + body)
                continue
            if opcode == 0xA:
                continue

            message += body

            if final:
                return message.decode("utf-8", "replace")

    def close(self):
        try:
            self._socket.close()
        except OSError:
            pass


def endpoint(port, path, timeout=20.0):
    """Reads one of the browser's HTTP DevTools endpoints, waiting for it to open."""

    deadline = time.monotonic() + timeout
    last = None

    while time.monotonic() < deadline:
        try:
            connection = http.client.HTTPConnection("127.0.0.1", port, timeout=5.0)
            connection.request("GET", path)
            response = connection.getresponse()
            body = response.read()
            connection.close()
            return json.loads(body)
        except Exception as error:               # noqa: BLE001 - the browser is still starting
            last = error
            time.sleep(0.05)

    raise ProtocolError("the browser did not answer %s: %s" % (path, last))


class Connection:
    """Commands and events over one DevTools socket."""

    def __init__(self, url, timeout=30.0):
        self._socket = WebSocket(url, timeout=timeout)
        self._next = 0
        self._lock = threading.Lock()
        self._pending = {}
        self._listeners = []
        self._failure = None
        self._closed = False

        self._reader = threading.Thread(target=self._pump, name="cdp", daemon=True)
        self._reader.start()

    def _pump(self):
        try:
            while True:
                message = json.loads(self._socket.receive())

                if "id" in message:
                    with self._lock:
                        waiter = self._pending.pop(message["id"], None)
                    if waiter is not None:
                        waiter[1] = message
                        waiter[0].set()
                    continue

                with self._lock:
                    listeners = list(self._listeners)
                for listener in listeners:
                    listener(message)
        except Exception as error:               # noqa: BLE001 - reported to every waiter
            with self._lock:
                self._failure = error
                pending = list(self._pending.values())
                self._pending.clear()
            for waiter in pending:
                waiter[1] = {"error": {"message": str(error)}}
                waiter[0].set()

    def listen(self, listener):
        """Registers a callable that every event is handed to."""

        with self._lock:
            self._listeners.append(listener)

    def call(self, method, params=None, session=None, timeout=60.0):
        """Sends one command and returns its result."""

        with self._lock:
            if self._failure is not None and not self._closed:
                raise ProtocolError("the browser connection failed: %s" % self._failure)
            self._next += 1
            identifier = self._next
            waiter = [threading.Event(), None]
            self._pending[identifier] = waiter

        message = {"id": identifier, "method": method, "params": params or {}}
        if session is not None:
            message["sessionId"] = session

        self._socket.send(json.dumps(message))

        if not waiter[0].wait(timeout):
            with self._lock:
                self._pending.pop(identifier, None)
            raise ProtocolError("%s did not answer within %gs" % (method, timeout))

        answer = waiter[1]

        if "error" in answer:
            raise ProtocolError("%s failed: %s" % (method, answer["error"].get("message", answer["error"])))

        return answer.get("result", {})

    def close(self):
        self._closed = True
        self._socket.close()
