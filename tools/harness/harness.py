#!/usr/bin/env python3
"""The one way to run the WebAssembly build of the engine in a browser.

Serving a build over range requests, starting a browser and taking it down
again, waiting for the engine to reach a state worth acting on, sending input in
the game's own coordinates, and comparing two screenshots are the same job every
time, and every hand-written version of it has gone wrong in the same ways: a
port two runs both chose, a profile directory nobody removed, a browser that
outlived the run that started it.  So it is done once, here.

Run ``harness.py doctor`` first; ``harness.py run --help`` lists the steps.
This is a developer tool and it reads the game data off the developer's own
discs, so it is deliberately not wired into CTest.
"""

import argparse
import contextlib
import json
import os
from pathlib import Path
import shlex
import signal
import sys
import time
import urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import cdp                                       # noqa: E402
import chrome                                    # noqa: E402
import imaging                                   # noqa: E402
import serving                                   # noqa: E402
import session as session_module                 # noqa: E402
from session import HarnessError                 # noqa: E402


ROOT = Path(__file__).resolve().parents[2]

DEFAULT_DISCS = ("FIRESTORM.iso", "TS1.iso", "TS2.iso")
DEFAULT_DISC_DIRECTORY = "~/Downloads"

STEP_HELP = """\
steps, given in order with --do (or one to a line with --script):

  wait <state|N|Ns|log:REGEX|frames:+N|js:EXPR>
                          module, main, frame, init, menu, scenario, playing
  sleep <seconds>
  move <x> <y>            mouse position, in game coordinates
  click <x> <y> [button]  left, middle or right; press and release
  down <x> <y> [button]   press without releasing
  up <x> <y> [button]     release
  drag <x1> <y1> <x2> <y2> [button]
  wheel <x> <y> <dx> <dy>
  tap <x> <y>             a touch down and up in one place
  touch <down|move|up> <x> <y>
  key <name>              escape, enter, f1, a, 1, ctrl+s ...
  text <string>           typed one key at a time
  shot <path>             a PNG screenshot
  state [path]            OpenTS_State and every module counter, as JSON
  log [path]              everything the page and the engine printed
  diff <a> <b> [pixels]   compare two screenshots, failing over a pixel budget
  eval <javascript>       an escape hatch; its value is printed
  expect <javascript>     the same, but a false value fails the run
"""


# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------

def say(text):
    print(text, flush=True)


def resolve_discs(named, directory):
    """Works out which images to serve: the ones named, or the usual three."""

    if named:
        return [str(Path(os.path.expanduser(one)).resolve()) for one in named]

    base = Path(os.path.expanduser(directory))
    found = []

    for name in DEFAULT_DISCS:
        candidate = base / name
        if candidate.is_file():
            found.append(str(candidate.resolve()))

    return found


def build_ini(settings):
    """Turns ``Section.Key=Value`` settings into the text of a SUN.INI."""

    sections = {}
    order = []

    for setting in settings:
        name, _, value = setting.partition("=")
        section, _, key = name.partition(".")

        if not _ or not section or not key:
            raise SystemExit("--ini wants Section.Key=Value, not %r" % setting)

        if section not in sections:
            sections[section] = []
            order.append(section)
        sections[section].append((key, value))

    lines = []
    for section in order:
        lines.append("[%s]" % section)
        for key, value in sections[section]:
            lines.append("%s=%s" % (key, value))
        lines.append("")

    return "\n".join(lines)


def page_url(server, options, discs):
    """Builds the address of the page, with everything the run asked of it."""

    query = []

    for disc in discs:
        query.append(("image", os.path.basename(disc)))

    if options.scenario:
        query.append(("scenario", options.scenario))
    if options.campaign:
        query.append(("campaign", options.campaign))
    if options.display:
        query.append(("display", options.display))
    for argument in options.arg:
        query.append(("arg", argument))
    for extra in options.query:
        name, _, value = extra.partition("=")
        query.append((name, value))

    return server.url("/index.html") + ("?" + urllib.parse.urlencode(query) if query else "")


def game_size(options):
    """The frame the engine will be running at, when the run pinned one."""

    if options.display and "x" in options.display:
        width, _, height = options.display.partition("x")
        return [int(width), int(height)]
    return None


def read_steps(options):
    steps = []

    if options.script:
        text = sys.stdin.read() if options.script == "-" else Path(options.script).read_text("utf-8")
        for line in text.splitlines():
            line = line.strip()
            if line and not line.startswith("#"):
                steps.append(line)

    steps.extend(options.do)
    return steps


def duration(text):
    return float(text[:-1]) if text.endswith("s") else float(text)


# ---------------------------------------------------------------------------
# doctor
# ---------------------------------------------------------------------------

def command_doctor(options):
    problems = 0

    say("python           %s" % sys.version.split()[0])
    if sys.version_info < (3, 9):
        say("                 ! 3.9 or newer is wanted")
        problems += 1

    browser = chrome.find_browser(options.browser)
    if browser:
        say("browser          %s" % browser)
        say("                 %s" % chrome.version(browser))
    else:
        say("browser          ! no Chrome, Chromium or Edge found; set OPENTS_CHROME")
        problems += 1

    discs = resolve_discs(options.disc, options.discs)
    if discs:
        for disc in discs:
            size = os.path.getsize(disc)
            say("disc             %s (%.0f MB)" % (disc, size / 1048576))
    else:
        say("disc             ! none of %s under %s" %
            (", ".join(DEFAULT_DISCS), options.discs))
        problems += 1

    if options.bin:
        directory = Path(options.bin)
        page = directory / "index.html"
        modules = sorted(p.name for p in directory.glob("*.wasm") if p.name.startswith("Game"))
        say("build            %s" % directory)
        say("                 index.html %s" % ("present" if page.is_file() else "! missing"))
        say("                 modules    %s" % (", ".join(modules) or "! none"))
        if not page.is_file() or not modules:
            problems += 1
    else:
        say("build            (none given; pass --bin to check one)")

    found = chrome.strays()
    if found:
        say("strays           ! %d browser(s) left over from an earlier run" % len(found))
        for stray in found:
            say("                   pid %d  %s" % (stray["pid"], stray["profile"]))
        if options.reap:
            ended = chrome.reap(found)
            say("                 reaped %s" % (", ".join(str(one) for one in ended) or "nothing"))
        else:
            say("                 run: %s doctor --reap" % os.path.relpath(__file__, ROOT))
            problems += 1
    else:
        say("strays           none")

    say("")
    say("ready" if problems == 0 else "%d thing(s) to fix" % problems)
    return 0 if problems == 0 else 1


# ---------------------------------------------------------------------------
# serve
# ---------------------------------------------------------------------------

def command_serve(options):
    discs = resolve_discs(options.disc, options.discs)

    with contextlib.closing(serving.BuildServer(options.bin, discs)) as server:
        say("serving %s on %s" % (server.root, server.origin))
        for disc in discs:
            say("  %s -> %s" % (os.path.basename(disc), disc))
        say("open %s" % server.url("/index.html"))
        say("ctrl-c to stop")
        try:
            while True:
                time.sleep(3600)
        except KeyboardInterrupt:
            say("")

    return 0


# ---------------------------------------------------------------------------
# diff
# ---------------------------------------------------------------------------

def command_diff(options):
    answer = imaging.compare(imaging.read(options.first), imaging.read(options.second),
                             options.threshold)
    say(json.dumps(answer, indent=2, sort_keys=True))
    return 0 if answer["identical"] or options.budget is None or \
        answer.get("differing", 0) <= options.budget else 1


# ---------------------------------------------------------------------------
# run
# ---------------------------------------------------------------------------

class Runner:
    """Carries out the step list against one live session."""

    def __init__(self, page, options):
        self.page = page
        self.options = options
        self.out = Path(options.out)
        self.results = []

    def path(self, name):
        candidate = Path(name)
        return str(candidate if candidate.is_absolute() else self.out / candidate)

    def run(self, steps):
        for index, step in enumerate(steps, 1):
            words = shlex.split(step)
            if not words:
                continue

            started = time.monotonic()
            say("[%2d] %s" % (index, step))
            answer = self.dispatch(words[0], words[1:], step)
            elapsed = time.monotonic() - started

            if answer is not None:
                say("     %s" % json.dumps(answer, sort_keys=True)[:800])
            say("     %.2fs" % elapsed)

            self.results.append({"step": step, "seconds": round(elapsed, 3), "result": answer})

    def dispatch(self, name, words, step):
        page = self.page
        options = self.options

        if name == "wait":
            if not words:
                raise HarnessError("wait wants something to wait for")
            target = words[0]
            timeout = float(words[1]) if len(words) > 1 else options.timeout
            if target[0].isdigit():
                time.sleep(duration(target))
                return None
            snapshot = page.wait(target, timeout)
            return {"frames": snapshot.get("frames"), "lines": len(page.log)}

        if name == "sleep":
            time.sleep(duration(words[0]))
            return None

        if name == "move":
            page.move(int(words[0]), int(words[1]))
            return None

        if name in ("click", "down", "up"):
            x, y = int(words[0]), int(words[1])
            button = words[2] if len(words) > 2 else "left"
            {"click": page.click, "down": page.press, "up": page.release}[name](x, y, button)
            return None

        if name == "drag":
            x1, y1, x2, y2 = (int(word) for word in words[:4])
            button = words[4] if len(words) > 4 else "left"
            page.move(x1, y1)
            page.press(x1, y1, button)
            page.mouse("mouseMoved", (x1 + x2) // 2, (y1 + y2) // 2, button, 0,
                       session_module.BUTTON_MASK.get(button, 1))
            page.mouse("mouseMoved", x2, y2, button, 0, session_module.BUTTON_MASK.get(button, 1))
            page.release(x2, y2, button)
            return None

        if name == "wheel":
            page.wheel(int(words[0]), int(words[1]), float(words[2]), float(words[3]))
            return None

        if name == "tap":
            x, y = int(words[0]), int(words[1])
            page.touch("touchStart", [(x, y)])
            page.touch("touchEnd", [(x, y)])
            return None

        if name == "touch":
            kind = {"down": "touchStart", "move": "touchMove", "up": "touchEnd"}[words[0]]
            page.touch(kind, [(int(words[1]), int(words[2]))])
            return None

        if name == "key":
            parts = words[0].split("+")
            modifiers = 0
            for part in parts[:-1]:
                modifiers |= session_module.MODIFIERS[part.lower()]
            page.key(parts[-1], modifiers)
            return None

        if name == "text":
            for character in " ".join(words):
                page.key(character)
            return None

        if name == "shot":
            return page.screenshot(self.path(words[0]))

        if name == "state":
            snapshot = page.state(self.path(words[0]) if words else None)
            return snapshot

        if name == "log":
            count = page.write_log(self.path(words[0]) if words else None)
            if not words:
                for line in page.log:
                    say("     %8.3f  %-9s %s" % (line["at"], line["level"], line["text"]))
            return {"lines": count}

        if name == "diff":
            answer = imaging.compare(imaging.read(self.path(words[0])),
                                     imaging.read(self.path(words[1])))
            budget = int(words[2]) if len(words) > 2 else None
            if budget is not None and answer.get("differing", 1) > budget:
                raise HarnessError("%s and %s differ in %d pixels, over the budget of %d" %
                                   (words[0], words[1], answer["differing"], budget))
            return answer

        if name == "eval":
            return page.evaluate(step.split(None, 1)[1])

        if name == "expect":
            expression = step.split(None, 1)[1]
            value = page.evaluate(expression)
            if not value:
                raise HarnessError("expect %s was %r" % (expression, value))
            return value

        raise HarnessError("no step is named %r" % name)


def command_run(options):
    steps = read_steps(options)
    discs = resolve_discs(options.disc, options.discs)

    if not discs:
        say("no disc image found; pass --disc or --discs")
        return 2

    browser_path = chrome.find_browser(options.browser)
    if not browser_path:
        say("no browser found; set OPENTS_CHROME or pass --browser")
        return 2

    ini = build_ini(options.ini) if options.ini else None
    integer_scaling = any(setting.lower().startswith("video.integerscaling=y") or
                          setting.lower().startswith("video.integerscaling=true") or
                          setting.lower().startswith("video.integerscaling=1")
                          for setting in options.ini)

    Path(options.out).mkdir(parents=True, exist_ok=True)

    report = {
        "steps": [],
        "ok": False,
    }
    started = time.monotonic()
    status = 0

    # SIGTERM has to land in the main thread as an exception, or the teardown
    # below never runs and the browser outlives the run that started it.
    def terminated(*_):
        raise KeyboardInterrupt()

    previous = signal.signal(signal.SIGTERM, terminated)

    with contextlib.ExitStack() as stack:
        stack.callback(signal.signal, signal.SIGTERM, previous)

        server = serving.BuildServer(options.bin, discs)
        stack.callback(server.close)
        say("serving %s on %s" % (server.root, server.origin))

        browser = chrome.Browser(browser_path,
                                 *(int(part) for part in options.window.split("x")),
                                 scale=options.scale,
                                 headless=not options.headed)
        stack.callback(browser.close)
        say("browser  %s (%s), profile %s" %
            (os.path.basename(browser_path), "headed" if options.headed else "headless",
             browser.profile))

        try:
            target = browser.connection.call("Target.createTarget", {"url": "about:blank"})
            attached = browser.connection.call("Target.attachToTarget",
                                               {"targetId": target["targetId"], "flatten": True})
            page = session_module.Session(browser.connection, target["targetId"],
                                          attached["sessionId"], quiet=options.quiet,
                                          verbose=options.verbose)

            width, height = (int(part) for part in options.window.split("x"))
            page.call("Emulation.setDeviceMetricsOverride", {
                "width": width, "height": height,
                "deviceScaleFactor": options.scale, "mobile": False,
            })
            # Nothing in the engine or the page asks whether the device has a
            # touch screen, so leaving this on costs a mouse-only run nothing
            # and is what makes a touch step land.
            page.call("Emulation.setTouchEmulationEnabled",
                      {"enabled": options.touch, "maxTouchPoints": 5})

            boot = (session_module.BOOT_SCRIPT
                    .replace("__INI__", json.dumps(ini) if ini else "null")
                    .replace("__GAME__", json.dumps(game_size(options)))
                    .replace("__INTEGER__", "true" if integer_scaling else "false"))
            page.call("Page.addScriptToEvaluateOnNewDocument", {"source": boot})

            url = page_url(server, options, discs)
            say("page     %s" % url)
            if ini:
                say("SUN.INI  %s" % " ".join(ini.split()))
            say("")

            page.open(url)

            runner = Runner(page, options)
            try:
                runner.run(steps)
                report["ok"] = True
            finally:
                report["steps"] = runner.results

            if options.hold:
                say("")
                say("holding; ctrl-c or enter to finish")
                with contextlib.suppress(EOFError, KeyboardInterrupt):
                    input()

        except KeyboardInterrupt:
            say("\ninterrupted")
            status = 130
        except (HarnessError, cdp.ProtocolError) as error:
            say("")
            say("FAILED: %s" % error)
            status = 1
        finally:
            report["seconds"] = round(time.monotonic() - started, 3)
            report["served"] = server.summary()
            with contextlib.suppress(Exception):
                report["log"] = page.log
                report["state"] = page.last

    if options.report:
        Path(options.report).write_text(json.dumps(report, indent=2, sort_keys=True), "utf-8")
        say("report   %s" % options.report)

    say("")
    say("torn down: browser ended, port %d closed, profile removed" % server.port)

    return status


# ---------------------------------------------------------------------------
# argument parsing
# ---------------------------------------------------------------------------

def add_common(parser):
    parser.add_argument("--disc", action="append", default=[],
                        help="a disc image to serve, in search order; repeatable")
    parser.add_argument("--discs", default=DEFAULT_DISC_DIRECTORY,
                        help="where to look for %s (default: %%(default)s)" %
                             ", ".join(DEFAULT_DISCS))
    parser.add_argument("--browser", help="the browser to launch (default: the first found)")


def parse(argv):
    parser = argparse.ArgumentParser(
        prog="harness.py",
        description=__doc__.splitlines()[0],
        formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)

    doctor = commands.add_parser("doctor", help="report what is missing")
    doctor.add_argument("--bin", help="a build directory to check as well")
    doctor.add_argument("--reap", action="store_true",
                        help="end browsers an earlier run left behind")
    add_common(doctor)
    doctor.set_defaults(handler=command_doctor)

    serve = commands.add_parser("serve", help="serve a build for a browser of one's own")
    serve.add_argument("--bin", required=True, help="the build's bin directory")
    add_common(serve)
    serve.set_defaults(handler=command_serve)

    diff = commands.add_parser("diff", help="compare two screenshots")
    diff.add_argument("first")
    diff.add_argument("second")
    diff.add_argument("--threshold", type=int, default=0,
                      help="a channel difference this large or less counts as the same")
    diff.add_argument("--budget", type=int,
                      help="fail when more than this many pixels differ")
    diff.set_defaults(handler=command_diff)

    run = commands.add_parser("run", help="serve a build, drive it, and tear it down",
                              epilog=STEP_HELP,
                              formatter_class=argparse.RawDescriptionHelpFormatter)
    run.add_argument("--bin", required=True, help="the build's bin directory")
    add_common(run)
    run.add_argument("--scenario", help="start this mission directly, as ?scenario=")
    run.add_argument("--campaign", help="start this campaign, as ?campaign=")
    run.add_argument("--display", help="native, scaled, or WIDTHxHEIGHT")
    run.add_argument("--arg", action="append", default=[],
                     help="an engine switch, passed through as ?arg=; repeatable")
    run.add_argument("--query", action="append", default=[],
                     help="another query parameter, as name=value; repeatable")
    run.add_argument("--ini", action="append", default=[], metavar="SECTION.KEY=VALUE",
                     help="a SUN.INI setting, written where the engine looks for one")
    run.add_argument("--window", default="1280x800", help="the page's size (default: %(default)s)")
    run.add_argument("--scale", type=float, default=1.0,
                     help="device pixel ratio (default: %(default)s)")
    run.add_argument("--headed", action="store_true", help="show the browser window")
    run.add_argument("--no-touch", dest="touch", action="store_false",
                     help="do not present the page with a touch screen")
    run.add_argument("--hold", action="store_true", help="keep the run open after the last step")
    run.add_argument("--quiet", type=float, default=2.0, metavar="SECONDS",
                     help="how long the log stays silent before a state counts as reached")
    run.add_argument("--timeout", type=float, default=180.0, metavar="SECONDS",
                     help="the default a wait gives up after")
    run.add_argument("--out", default=".", help="where a relative step path lands")
    run.add_argument("--report", help="write a JSON record of the run here")
    run.add_argument("--verbose", action="store_true", help="print the page's output as it arrives")
    run.add_argument("--do", action="append", default=[], metavar="STEP",
                     help="a step, carried out in the order given; repeatable")
    run.add_argument("--script", help="a file of steps, one to a line, or - for standard input")
    run.set_defaults(handler=command_run)

    return parser.parse_args(argv)


def main(argv=None):
    options = parse(argv if argv is not None else sys.argv[1:])
    try:
        return options.handler(options)
    except KeyboardInterrupt:
        say("\ninterrupted")
        return 130
    except (HarnessError, cdp.ProtocolError, FileNotFoundError, imaging.ImageError,
            RuntimeError) as error:
        say("%s" % error)
        return 1


if __name__ == "__main__":
    sys.exit(main())
