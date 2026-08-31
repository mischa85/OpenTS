"""One run of the game in a browser: states, input, and what can be observed.

The page already reports enough to tell a running engine from a stopped one --
``window.OpenTS_State`` and the counters the module keeps -- and the engine
prints its progress.  This turns those into named states, so that "the menu is
up" means one thing across every run, and into input expressed in the game's own
coordinates, so that nobody translates a click by hand again.
"""

import base64
import collections
import json
import re
import time

import imaging


START = time.monotonic()


class HarnessError(Exception):
    """A step could not be carried out, or a wait never came true."""


# What the engine prints on the way through. These are the only two strings the
# harness reads out of the log, and each is the last line of the phase it names.
INIT_MARKER = re.compile(r"Game Init Completed\.")
SCENARIO_MARKER = re.compile(r"Reading scenario:")

STATES = ("module", "main", "frame", "init", "menu", "scenario", "playing")

BUTTON_NAMES = {"left": "left", "middle": "middle", "right": "right", "none": "none"}
BUTTON_MASK = {"left": 1, "right": 2, "middle": 4, "none": 0}

MODIFIERS = {"alt": 1, "ctrl": 2, "control": 2, "meta": 4, "cmd": 4, "shift": 8}

# The names a step may use, against the DOM code the engine reads and the Windows
# virtual key Chrome should report. Letters, digits and function keys are derived.
NAMED_KEYS = {
    "escape": ("Escape", "Escape", 27),
    "esc": ("Escape", "Escape", 27),
    "enter": ("Enter", "Enter", 13),
    "return": ("Enter", "Enter", 13),
    "tab": ("Tab", "Tab", 9),
    "space": ("Space", " ", 32),
    "backspace": ("Backspace", "Backspace", 8),
    "delete": ("Delete", "Delete", 46),
    "insert": ("Insert", "Insert", 45),
    "home": ("Home", "Home", 36),
    "end": ("End", "End", 35),
    "pageup": ("PageUp", "PageUp", 33),
    "pagedown": ("PageDown", "PageDown", 34),
    "up": ("ArrowUp", "ArrowUp", 38),
    "down": ("ArrowDown", "ArrowDown", 40),
    "left": ("ArrowLeft", "ArrowLeft", 37),
    "right": ("ArrowRight", "ArrowRight", 39),
    "shift": ("ShiftLeft", "Shift", 16),
    "ctrl": ("ControlLeft", "Control", 17),
    "alt": ("AltLeft", "Alt", 18),
}


def key_event(name):
    """Turns a key name into the fields Chrome and the engine both want."""

    lowered = name.lower()

    if lowered in NAMED_KEYS:
        code, key, virtual = NAMED_KEYS[lowered]
        return code, key, virtual, ""

    if len(name) == 1 and name.isalpha():
        upper = name.upper()
        return "Key" + upper, name, ord(upper), name

    if len(name) == 1 and name.isdigit():
        return "Digit" + name, name, ord(name), name

    match = re.fullmatch(r"[fF]([1-9]|1[0-9]|2[0-4])", name)
    if match:
        number = int(match.group(1))
        return "F%d" % number, "F%d" % number, 111 + number, ""

    if len(name) == 1:
        punctuation = {
            "-": ("Minus", 189), "=": ("Equal", 187), "[": ("BracketLeft", 219),
            "]": ("BracketRight", 221), "\\": ("Backslash", 220), ";": ("Semicolon", 186),
            "'": ("Quote", 222), "`": ("Backquote", 192), ",": ("Comma", 188),
            ".": ("Period", 190), "/": ("Slash", 191), " ": ("Space", 32),
        }
        if name in punctuation:
            code, virtual = punctuation[name]
            return code, name, virtual, name

    raise HarnessError("no key is named %r" % name)


BOOT_SCRIPT = r"""
(function () {
    var harness = {
        ini: __INI__,
        gameSize: __GAME__,
        integerScaling: __INTEGER__,
        wroteIni: false,
        iniError: null,
        hookError: null
    };

    window.OpenTS_Harness = harness;

    /* The engine's own clamp: a frame is a multiple of four, no larger than the
       size the window is followed to, and no smaller than the one the sidebar
       still fits in. Kept in step with Video_Clamp_Frame_Size. */
    function clamp(value, maximum, minimum) {
        if (value > maximum) value = maximum;
        value = value & ~3;
        if (value < minimum) value = minimum;
        return value;
    }

    /* Where the game's frame lands on the page, by the same arithmetic
       Update_Scale_Info uses, so a game coordinate converts the way the engine
       converts it back. */
    harness.geometry = function () {
        var canvas = document.getElementById("canvas");
        if (!canvas) return null;

        var rect = canvas.getBoundingClientRect();
        var ratio = window.devicePixelRatio || 1;
        var windowWidth = canvas.width || Math.round(rect.width * ratio);
        var windowHeight = canvas.height || Math.round(rect.height * ratio);

        var gameWidth = harness.gameSize ? harness.gameSize[0] : Math.round(rect.width);
        var gameHeight = harness.gameSize ? harness.gameSize[1] : Math.round(rect.height);

        gameWidth = clamp(gameWidth, 2560, 640);
        gameHeight = clamp(gameHeight, 1600, 400);

        var scale = Math.min(windowWidth / gameWidth, windowHeight / gameHeight);
        if (harness.integerScaling && scale >= 1) scale = Math.floor(scale);

        var destWidth = Math.trunc(gameWidth * scale);
        var destHeight = Math.trunc(gameHeight * scale);

        return {
            css: {left: rect.left, top: rect.top, width: rect.width, height: rect.height},
            ratio: ratio,
            drawing: [windowWidth, windowHeight],
            game: [gameWidth, gameHeight],
            dest: [Math.floor((windowWidth - destWidth) / 2),
                   Math.floor((windowHeight - destHeight) / 2),
                   destWidth, destHeight]
        };
    };

    /* A game pixel's centre, in the page's own coordinates. The centre rather
       than the corner, so that the rounding on the way back cannot land the
       event on the neighbouring pixel. */
    harness.toPage = function (x, y) {
        var view = harness.geometry();
        if (!view) return null;

        return {
            x: view.css.left + (view.dest[0] + (x + 0.5) * view.dest[2] / view.game[0]) / view.ratio,
            y: view.css.top + (view.dest[1] + (y + 0.5) * view.dest[3] / view.game[1]) / view.ratio
        };
    };

    if (harness.ini === null) return;

    /* The settings file is a bare name, which the engine's file layer looks for
       beside the module before it asks a disc image, so writing one here is what
       an installed SUN.INI would have been. It goes in after the page's own
       preRun, which is where the save directory is mounted, and outside that
       directory, which the run's first syncfs would otherwise reconcile away. */
    function attach(module) {
        if (!module || module.__opentsHarnessAttached) return;
        module.__opentsHarnessAttached = true;
        module.preRun = module.preRun || [];
        module.preRun.push(function () {
            try {
                module.FS.writeFile("SUN.INI", harness.ini);
                harness.wroteIni = true;
            } catch (error) {
                harness.iniError = String(error);
            }
        });
    }

    try {
        var held;
        Object.defineProperty(window, "Module", {
            configurable: true,
            get: function () { return held; },
            set: function (value) { held = value; attach(value); }
        });
    } catch (error) {
        harness.hookError = String(error);
    }
}());
"""


SNAPSHOT = r"""
(function () {
    var state = window.OpenTS_State || {};
    var out = {
        page: !!window.OpenTS_State,
        started: !!state.started,
        frames: state.frames || 0,
        waits: state.waits || 0,
        syncs: state.syncs || 0,
        persistent: !!state.persistent,
        restored: !!state.restored,
        lines: (state.lines || []).length,
        module: typeof Module !== "undefined",
        /* The runtime is up once its exports are on the module object; the
           runtime's own calledRun is a local of the loader and not readable. */
        loaded: typeof Module !== "undefined" &&
                typeof Module._OpenTS_Browser_Frames === "function",
        title: document.title,
        gate: !!(document.getElementById("unsupported") &&
                 !document.getElementById("unsupported").hidden)
    };

    if (out.module) {
        var counters = {};
        for (var name in Module) {
            if (name.indexOf("_OpenTS_") === 0 && typeof Module[name] === "function") {
                try { counters[name.substring(1)] = Module[name](); } catch (error) {}
            }
        }
        out.counters = counters;
    }

    if (window.OpenTS_Harness) {
        out.geometry = window.OpenTS_Harness.geometry();
        out.ini = {
            written: window.OpenTS_Harness.wroteIni,
            error: window.OpenTS_Harness.iniError,
            hook: window.OpenTS_Harness.hookError
        };
    }

    var fault = document.getElementById("fault");
    if (fault) out.fault = fault.textContent;

    return out;
}())
"""


class Session:
    """Drives one page: navigation, waiting, input, and observation."""

    def __init__(self, connection, target_id, session_id, *, quiet=2.0, verbose=False):
        self.connection = connection
        self.target_id = target_id
        self.session = session_id
        self.quiet = quiet
        self.verbose = verbose

        self.log = []
        self.exceptions = []
        self._last_line = time.monotonic()
        self._history = collections.deque(maxlen=600)
        self.last = {}

        connection.listen(self._event)
        self.call("Runtime.enable")
        self.call("Page.enable")
        self.call("Log.enable")

    # -- protocol ---------------------------------------------------------

    def call(self, method, params=None, timeout=60.0):
        return self.connection.call(method, params, session=self.session, timeout=timeout)

    def _event(self, message):
        if message.get("sessionId") != self.session:
            return

        method = message.get("method")
        params = message.get("params", {})

        if method == "Runtime.consoleAPICalled":
            pieces = []
            for argument in params.get("args", []):
                if "value" in argument:
                    pieces.append(str(argument["value"]))
                elif "description" in argument:
                    pieces.append(argument["description"])
            self._record(params.get("type", "log"), " ".join(pieces))
        elif method == "Log.entryAdded":
            entry = params.get("entry", {})
            self._record(entry.get("level", "log"), entry.get("text", ""))
        elif method == "Runtime.exceptionThrown":
            details = params.get("exceptionDetails", {})
            text = details.get("text", "")
            thrown = details.get("exception", {})
            text = "%s %s" % (text, thrown.get("description", thrown.get("value", "")))
            self.exceptions.append(text.strip())
            self._record("exception", text.strip())

    def _record(self, level, text):
        for line in text.splitlines() or [""]:
            self.log.append({"at": round(time.monotonic() - START, 3), "level": level, "text": line})
        self._last_line = time.monotonic()
        if self.verbose:
            print("    | %s" % text.rstrip(), flush=True)

    def evaluate(self, expression, timeout=30.0):
        answer = self.call("Runtime.evaluate", {
            "expression": expression,
            "returnByValue": True,
            "awaitPromise": False,
        }, timeout=timeout)

        if answer.get("exceptionDetails"):
            details = answer["exceptionDetails"]
            thrown = details.get("exception", {})
            raise HarnessError("the page raised: %s" %
                               (thrown.get("description") or details.get("text")))

        return answer.get("result", {}).get("value")

    # -- navigation and state --------------------------------------------

    def open(self, url):
        self.call("Page.navigate", {"url": url})

    def snapshot(self):
        self.last = self.evaluate(SNAPSHOT) or {}
        self._history.append((time.monotonic(), self.last.get("frames", 0)))
        return self.last

    def geometry(self):
        view = (self.last or {}).get("geometry")
        if view is None:
            view = self.evaluate("window.OpenTS_Harness && window.OpenTS_Harness.geometry()")
        if view is None:
            raise HarnessError("the page has no canvas yet, so there are no game coordinates")
        return view

    def _matched(self, pattern):
        return any(pattern.search(line["text"]) for line in self.log)

    def _settled(self):
        """Drawing, and no longer saying anything about it.

        A phase of the engine ends with the last line it prints; after that the
        loop is turning and the picture is whatever the phase produced. So a
        state is reached when the log has been quiet for the settle time and
        frames have advanced across that same window.
        """

        now = time.monotonic()

        if now - self._last_line < self.quiet:
            return False

        current = self.last.get("frames", 0)
        for stamp, frames in self._history:
            if stamp <= now - self.quiet:
                earlier = frames
                break
        else:
            return False

        return current > earlier

    def reached(self, state):
        snapshot = self.last

        if state == "module":
            return bool(snapshot.get("loaded"))
        if state == "main":
            return bool(snapshot.get("started"))
        if state == "frame":
            return snapshot.get("frames", 0) >= 1
        if state == "init":
            return self._matched(INIT_MARKER)
        if state == "menu":
            return self._matched(INIT_MARKER) and self._settled()
        if state == "scenario":
            return self._matched(SCENARIO_MARKER)
        if state == "playing":
            return self._matched(SCENARIO_MARKER) and self._settled()

        raise HarnessError("no state is named %r; the states are %s" %
                           (state, ", ".join(STATES)))

    def wait(self, target, timeout):
        """Waits for a named state, a log line, a frame count, or an expression."""

        deadline = time.monotonic() + timeout
        baseline = None

        while True:
            self.snapshot()

            if self.last.get("gate"):
                raise HarnessError("the page put up its gate screen: the module it wanted "
                                   "is not on the server")

            if target.startswith("log:"):
                done = self._matched(re.compile(target[4:]))
            elif target.startswith("frames:+"):
                if baseline is None:
                    baseline = self.last.get("frames", 0)
                done = self.last.get("frames", 0) >= baseline + int(target[8:])
            elif target.startswith("js:"):
                done = bool(self.evaluate(target[3:]))
            else:
                done = self.reached(target)

            if done:
                return self.last

            if self.last.get("fault"):
                raise HarnessError("the engine stopped: %s" % self.last["fault"])

            if time.monotonic() >= deadline:
                raise HarnessError(
                    "waiting for %r timed out after %gs (frames %s, last line %r)" % (
                        target, timeout, self.last.get("frames"),
                        self.log[-1]["text"] if self.log else ""))

            time.sleep(0.2)

    # -- input ------------------------------------------------------------

    def _page_point(self, x, y):
        view = self.geometry()
        left, top = view["css"]["left"], view["css"]["top"]
        ratio = view["ratio"]
        dest_x, dest_y, dest_w, dest_h = view["dest"]
        game_w, game_h = view["game"]

        return (left + (dest_x + (x + 0.5) * dest_w / game_w) / ratio,
                top + (dest_y + (y + 0.5) * dest_h / game_h) / ratio)

    def mouse(self, kind, x, y, button="left", clicks=1, buttons=0, modifiers=0):
        page_x, page_y = self._page_point(x, y)
        self.call("Input.dispatchMouseEvent", {
            "type": kind,
            "x": page_x,
            "y": page_y,
            "button": BUTTON_NAMES.get(button, "left"),
            "buttons": buttons,
            "clickCount": clicks,
            "modifiers": modifiers,
        })

    def move(self, x, y):
        self.mouse("mouseMoved", x, y, button="none", clicks=0)

    def press(self, x, y, button="left", modifiers=0):
        self.mouse("mousePressed", x, y, button, 1, BUTTON_MASK.get(button, 1), modifiers)

    def release(self, x, y, button="left", modifiers=0):
        self.mouse("mouseReleased", x, y, button, 1, 0, modifiers)

    def click(self, x, y, button="left", modifiers=0):
        self.move(x, y)
        self.press(x, y, button, modifiers)
        self.release(x, y, button, modifiers)

    def wheel(self, x, y, delta_x, delta_y):
        page_x, page_y = self._page_point(x, y)
        self.call("Input.dispatchMouseEvent", {
            "type": "mouseWheel",
            "x": page_x,
            "y": page_y,
            "button": "none",
            "buttons": 0,
            "deltaX": delta_x,
            "deltaY": delta_y,
        })

    def touch(self, kind, points):
        payload = []
        for index, (x, y) in enumerate(points):
            page_x, page_y = self._page_point(x, y)
            payload.append({"x": page_x, "y": page_y, "id": index, "radiusX": 1, "radiusY": 1,
                            "force": 1.0})

        self.call("Input.dispatchTouchEvent", {
            "type": kind,
            "touchPoints": [] if kind == "touchEnd" else payload,
        })

    def key(self, name, modifiers=0):
        code, key, virtual, text = key_event(name)
        common = {
            "code": code,
            "key": key,
            "windowsVirtualKeyCode": virtual,
            "nativeVirtualKeyCode": virtual,
            "modifiers": modifiers,
        }
        down = dict(common, type="keyDown" if text else "rawKeyDown")
        if text:
            down["text"] = text
            down["unmodifiedText"] = text
        self.call("Input.dispatchKeyEvent", down)
        self.call("Input.dispatchKeyEvent", dict(common, type="keyUp"))

    # -- observation ------------------------------------------------------

    def screenshot(self, path):
        answer = self.call("Page.captureScreenshot", {"format": "png",
                                                      "captureBeyondViewport": False},
                           timeout=90.0)
        data = base64.b64decode(answer["data"])
        with open(path, "wb") as handle:
            handle.write(data)
        picture = imaging.decode(data)
        return {"path": path, "bytes": len(data), "size": [picture.width, picture.height]}

    def state(self, path=None):
        snapshot = self.snapshot()
        if path:
            with open(path, "w", encoding="utf-8") as handle:
                json.dump(snapshot, handle, indent=2, sort_keys=True)
        return snapshot

    def write_log(self, path=None):
        if path:
            with open(path, "w", encoding="utf-8") as handle:
                for line in self.log:
                    handle.write("%8.3f  %-9s %s\n" % (line["at"], line["level"], line["text"]))
        return len(self.log)
