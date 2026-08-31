"""Starts a browser the harness owns, and makes sure it does not outlive the run.

Every browser this starts gets a profile directory of its own under the system
temporary directory and its own process group, so that tearing it down is one
signal to the group and one directory removed.  Nothing is shared with the
developer's own browser: no profile, no port, no window.

The profile name carries a fixed prefix, which is what lets :func:`strays` find
a browser some earlier run failed to clean up.
"""

import os
import re
import shutil
import signal
import subprocess
import tempfile
import time

import cdp


PROFILE_PREFIX = "opents-harness-"

CANDIDATES = (
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    "/Applications/Google Chrome Canary.app/Contents/MacOS/Google Chrome Canary",
    "/Applications/Chromium.app/Contents/MacOS/Chromium",
    "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge",
    "/usr/bin/google-chrome",
    "/usr/bin/chromium",
    "/usr/bin/chromium-browser",
    "/usr/bin/microsoft-edge",
)


def find_browser(explicit=None):
    """Locates a Chromium-family browser, or returns ``None``."""

    for candidate in (explicit, os.environ.get("OPENTS_CHROME")):
        if candidate:
            return candidate if os.path.exists(candidate) else None

    for candidate in CANDIDATES:
        if os.path.exists(candidate):
            return candidate

    return shutil.which("google-chrome") or shutil.which("chromium")


def version(path):
    """Asks the browser what it is, for ``doctor``."""

    try:
        answer = subprocess.run([path, "--version"], capture_output=True, text=True, timeout=20)
        return answer.stdout.strip() or answer.stderr.strip()
    except Exception as error:                   # noqa: BLE001 - reported, never raised
        return "could not be run: %s" % error


def strays():
    """Finds browsers whose harness is gone.

    A run that was killed outright leaves one behind, holding a port and a
    profile directory the next run has to work around.  A browser whose harness
    is still running is somebody else's live run and is never reported: the
    reason this exists at all is that a stray hunt once ended a run in progress.

    The harness is the browser's parent, so a browser whose parent has been
    reparented away, or whose parent is not a harness, is the orphan.
    """

    try:
        listing = subprocess.run(["ps", "-ax", "-o", "pid=,ppid=,command="],
                                 capture_output=True, text=True, timeout=20).stdout
    except Exception:                            # noqa: BLE001 - no listing is no strays
        return []

    pattern = re.compile(r"--user-data-dir=(\S*%s\S*)" % re.escape(PROFILE_PREFIX))
    commands = {}
    browsers = []

    for line in listing.splitlines():
        fields = line.strip().split(None, 2)
        if len(fields) < 3:
            continue
        try:
            pid, parent = int(fields[0]), int(fields[1])
        except ValueError:
            continue

        commands[pid] = fields[2]
        match = pattern.search(fields[2])
        if match is not None:
            browsers.append((pid, parent, match.group(1)))

    known = {pid for pid, _, _ in browsers}
    found = []

    for pid, parent, profile in browsers:
        # A renderer carries the same profile on its command line; it belongs to
        # the browser above it and goes when that one does.
        if parent in known:
            continue

        owner = commands.get(parent)
        if owner is not None and "harness.py" in owner:
            continue

        found.append({"pid": pid, "profile": profile, "parent": parent})

    return found


def reap(found):
    """Ends the processes :func:`strays` reported and removes their profiles."""

    ended = []

    for stray in found:
        try:
            os.killpg(os.getpgid(stray["pid"]), signal.SIGKILL)
            ended.append(stray["pid"])
        except OSError:
            try:
                os.kill(stray["pid"], signal.SIGKILL)
                ended.append(stray["pid"])
            except OSError:
                pass

    for stray in found:
        profile = stray["profile"]
        if PROFILE_PREFIX in os.path.basename(profile):
            shutil.rmtree(profile, ignore_errors=True)

    return ended


class Browser:
    """A launched browser and the DevTools connection to it."""

    def __init__(self, path, width, height, scale=1.0, headless=True, extra=()):
        self.path = path
        self.profile = tempfile.mkdtemp(prefix=PROFILE_PREFIX)
        self.process = None
        self.connection = None
        self.port = None
        self._closed = False

        arguments = [
            path,
            "--user-data-dir=" + self.profile,
            "--remote-debugging-port=0",
            "--remote-allow-origins=*",
            "--no-first-run",
            "--no-default-browser-check",
            "--disable-background-networking",
            "--disable-component-update",
            "--disable-client-side-phishing-detection",
            "--disable-sync",
            "--disable-default-apps",
            "--disable-popup-blocking",
            "--disable-search-engine-choice-screen",
            "--no-service-autorun",
            "--password-store=basic",
            "--use-mock-keychain",
            "--mute-audio",
            "--autoplay-policy=no-user-gesture-required",
            # Headless has no GPU, and Chrome will not fall back to its software
            # renderer for WebGL without being told to. The engine's renderer is
            # WebGL 2, so without this a headless run has no picture at all.
            "--enable-unsafe-swiftshader",
            "--window-size=%d,%d" % (width, height),
            "--window-position=0,0",
            "--force-device-scale-factor=%g" % scale,
        ]

        if headless:
            arguments.append("--headless=new")

        arguments.extend(extra)
        arguments.append("about:blank")

        self.arguments = arguments
        self.process = subprocess.Popen(
            arguments,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            # Its own process group, so that teardown reaches the renderer and
            # GPU children as well as the browser process itself.
            start_new_session=True,
        )

        self.port = self._wait_for_port()
        target = cdp.endpoint(self.port, "/json/version")
        self.connection = cdp.Connection(target["webSocketDebuggerUrl"])

    def _wait_for_port(self, timeout=30.0):
        marker = os.path.join(self.profile, "DevToolsActivePort")
        deadline = time.monotonic() + timeout

        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise RuntimeError("the browser exited before it opened a debugging port")
            try:
                with open(marker, "r", encoding="utf-8") as handle:
                    first = handle.readline().strip()
                if first:
                    return int(first)
            except (OSError, ValueError):
                pass
            time.sleep(0.05)

        raise RuntimeError("the browser did not open a debugging port within %gs" % timeout)

    def close(self):
        """Ends the browser and removes its profile, however the run finished."""

        if self._closed:
            return
        self._closed = True

        if self.connection is not None:
            try:
                self.connection.call("Browser.close", timeout=5.0)
            except Exception:                    # noqa: BLE001 - the signal below is the real one
                pass
            try:
                self.connection.close()
            except Exception:                    # noqa: BLE001
                pass

        if self.process is not None and self.process.poll() is None:
            try:
                os.killpg(os.getpgid(self.process.pid), signal.SIGTERM)
            except OSError:
                try:
                    self.process.terminate()
                except OSError:
                    pass

            try:
                self.process.wait(timeout=8.0)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(os.getpgid(self.process.pid), signal.SIGKILL)
                except OSError:
                    try:
                        self.process.kill()
                    except OSError:
                        pass
                try:
                    self.process.wait(timeout=5.0)
                except subprocess.TimeoutExpired:
                    pass

        shutil.rmtree(self.profile, ignore_errors=True)

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
        return False
