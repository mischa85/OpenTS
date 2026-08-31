"""Serves one build directory and the disc images beside it.

The engine reads its game data out of an ISO image with HTTP range requests and
refuses a server that answers with the whole file instead, so the one thing this
server must get right is ``206`` and a ``Content-Range`` that can be read back.
``http.server`` answers no range at all, which is why this is here rather than a
line of shell.

The images are named into the URL space rather than copied or linked into the
build directory: the discs stay wherever the developer keeps them, and nothing
the harness serves ever lands inside the repository.
"""

import email.utils
import http.server
import os
import posixpath
import re
import socketserver
import threading
import urllib.parse


# The port the container publishes and the developer's own server sits on. A
# harness that took it would take it from them, so it is refused even if the
# operating system offers it.
RESERVED_PORTS = {8765}

CONTENT_TYPES = {
    ".css": "text/css; charset=utf-8",
    ".data": "application/octet-stream",
    ".html": "text/html; charset=utf-8",
    ".ico": "image/x-icon",
    ".iso": "application/octet-stream",
    ".js": "text/javascript; charset=utf-8",
    ".json": "application/json; charset=utf-8",
    ".map": "application/json; charset=utf-8",
    ".mem": "application/octet-stream",
    ".png": "image/png",
    ".svg": "image/svg+xml",
    ".wasm": "application/wasm",
}

_RANGE = re.compile(r"^bytes=(\d*)-(\d*)$")


class _Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    # Set by the server.
    root = None
    extras = {}
    record = None

    def log_message(self, format, *args):        # noqa: A002 - the base class names it
        pass

    def _resolve(self):
        path = urllib.parse.urlsplit(self.path).path
        path = urllib.parse.unquote(path)
        name = posixpath.normpath(path).lstrip("/")

        if name in ("", "."):
            name = "index.html"

        if name.startswith("..") or "/../" in name:
            return None

        # A named image answers before the build directory does, so a build that
        # happens to hold a disc of its own is still served the one that was named.
        if name in self.extras:
            return self.extras[name]

        candidate = os.path.join(self.root, name)

        if os.path.isfile(candidate):
            return candidate

        # The engine spells its archives in upper case and a host may or may not
        # care; the served directory is matched the way the engine's own file
        # layer matches one.
        lowered = name.lower()
        for entry in os.listdir(self.root):
            if entry.lower() == lowered and os.path.isfile(os.path.join(self.root, entry)):
                return os.path.join(self.root, entry)

        return None

    def _send_common(self, path, stat, length):
        extension = os.path.splitext(path)[1].lower()
        self.send_header("Content-Type", CONTENT_TYPES.get(extension, "application/octet-stream"))
        self.send_header("Content-Length", str(length))
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Last-Modified", email.utils.formatdate(stat.st_mtime, usegmt=True))
        self.send_header("ETag", '"%x-%x"' % (int(stat.st_mtime), stat.st_size))
        self.send_header("Cache-Control", "no-cache")

    def do_HEAD(self):                           # noqa: N802 - the base class names it
        self._serve(body=False)

    def do_GET(self):                            # noqa: N802 - the base class names it
        self._serve(body=True)

    def _serve(self, body):
        path = self._resolve()

        # The browser asks for one whatever the page says, and a 404 for it in
        # the console is the sort of thing a run then goes looking into.
        if path is None and self.path.split("?", 1)[0] == "/favicon.ico":
            self.send_response(204)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return

        if path is None:
            self.send_error(404, "not served")
            if self.record is not None:
                self.record(self.path, 404, 0)
            return

        stat = os.stat(path)
        total = stat.st_size
        header = self.headers.get("Range")
        start = 0
        length = total
        status = 200

        if header:
            match = _RANGE.match(header.strip())

            if match is None:
                # A multipart or unreadable range: say so rather than answering
                # with the whole file, which is the failure the engine refuses.
                self.send_response(416)
                self.send_header("Content-Range", "bytes */%d" % total)
                self.send_header("Content-Length", "0")
                self.end_headers()
                if self.record is not None:
                    self.record(self.path, 416, 0)
                return

            first, last = match.group(1), match.group(2)

            if first == "":
                length = min(int(last or 0), total)
                start = total - length
            else:
                start = int(first)
                end = int(last) if last else total - 1
                if end >= total:
                    end = total - 1
                length = end - start + 1

            if start >= total or length <= 0:
                self.send_response(416)
                self.send_header("Content-Range", "bytes */%d" % total)
                self.send_header("Content-Length", "0")
                self.end_headers()
                if self.record is not None:
                    self.record(self.path, 416, 0)
                return

            status = 206

        self.send_response(status)
        self._send_common(path, stat, length)

        if status == 206:
            self.send_header("Content-Range", "bytes %d-%d/%d" % (start, start + length - 1, total))

        self.end_headers()

        if self.record is not None:
            self.record(self.path, status, length)

        if not body:
            return

        with open(path, "rb") as handle:
            handle.seek(start)
            remaining = length
            while remaining > 0:
                chunk = handle.read(min(262144, remaining))
                if not chunk:
                    break
                try:
                    self.wfile.write(chunk)
                except (BrokenPipeError, ConnectionResetError):
                    return
                remaining -= len(chunk)


class _Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


class BuildServer:
    """A running server for one build directory, on a port nobody chose."""

    def __init__(self, root, images=(), host="127.0.0.1"):
        self.root = os.path.abspath(root)

        if not os.path.isdir(self.root):
            raise FileNotFoundError("no such build directory: %s" % self.root)

        self.extras = {}
        for image in images:
            path = os.path.abspath(os.path.expanduser(image))
            if not os.path.isfile(path):
                raise FileNotFoundError("no such disc image: %s" % path)
            self.extras[os.path.basename(path)] = path

        self.requests = []
        self._lock = threading.Lock()

        handler = type("Handler", (_Handler,), {
            "root": self.root,
            "extras": self.extras,
            "record": staticmethod(self._record),
        })

        for _ in range(8):
            self._server = _Server((host, 0), handler)
            if self._server.server_address[1] not in RESERVED_PORTS:
                break
            self._server.server_close()
        else:
            raise RuntimeError("could not get a port outside the reserved set")

        self.host = host
        self.port = self._server.server_address[1]
        self._thread = threading.Thread(target=self._server.serve_forever,
                                        name="harness-http", daemon=True)
        self._thread.start()

    def _record(self, path, status, length):
        with self._lock:
            self.requests.append((path, status, length))

    @property
    def origin(self):
        return "http://%s:%d" % (self.host, self.port)

    def url(self, path="/"):
        return self.origin + (path if path.startswith("/") else "/" + path)

    def summary(self):
        """Counts what was asked for, which is how a run explains its own traffic."""

        with self._lock:
            requests = list(self.requests)

        served = {}
        for path, status, length in requests:
            key = path.split("?", 1)[0]
            entry = served.setdefault(key, {"requests": 0, "bytes": 0, "statuses": {}})
            entry["requests"] += 1
            entry["bytes"] += length
            entry["statuses"][str(status)] = entry["statuses"].get(str(status), 0) + 1

        return served

    def close(self):
        self._server.shutdown()
        self._server.server_close()
        self._thread.join(timeout=5.0)
