# The browser harness

`tools/harness/harness.py` is the one way to run the WebAssembly build of the
engine in a browser and act on it. It serves a build over range requests, starts
a browser it owns and takes it down again, waits for the engine to reach a named
state, sends input in the game's own coordinates, and compares screenshots.

Everybody who needed those things wrote them again, and every hand-written
version went wrong the same ways: two runs picking the same port, a profile
directory nobody removed, a browser outliving the run that started it, a click
translated into canvas pixels by hand and landing somewhere else. So it is done
once. **Use this rather than writing another one**;
[the engine source instructions](../code/AGENTS.md) make that a rule.

[Building OpenTS](BUILDING.md) owns how a build is produced and what the
WebAssembly target is; this page owns what to do with a build once it exists.

## Contents

- [Setup](#setup)
- [Running](#running)
- [States](#states)
- [Game coordinates](#game-coordinates)
- [Startup options and SUN.INI](#startup-options-and-sunini)
- [Observing](#observing)
- [Comparing screenshots](#comparing-screenshots)
- [Cleanup](#cleanup)
- [Why it is not a test](#why-it-is-not-a-test)
- [What has been run](#what-has-been-run)

## Setup

There is nothing to install. The harness has no third-party dependencies:
it speaks the DevTools protocol over a socket, answers range requests out of
`http.server`, and decodes a PNG with `zlib`, all on the standard library.
`tools/harness/requirements.txt` owns that decision and pins nothing; it is not
the manual's requirements file and must not be merged into it. The harness does
not use the manual's virtual environment and does not need one.

It needs Python 3.9 or newer, a Chromium-family browser already on the machine
(Chrome, Chromium or Edge; `OPENTS_CHROME` or `--browser` names another), and
the game discs. Ask what is missing:

```bash
python3 tools/harness/harness.py doctor --bin build-wasm/bin
```

`doctor` reports the Python, the browser and its version, the disc images, the
build directory's page and modules, and any browser an earlier run left running.
`doctor --reap` ends those and removes their profiles.

The discs are read from wherever they are, and are never copied or linked into
the repository or into a build directory. By default the harness looks for
`FIRESTORM.iso`, `TS1.iso` and `TS2.iso` under `~/Downloads`; `--discs
DIRECTORY` moves that, and `--disc PATH` names images one at a time, in the
order the engine should search them.

## Running

`run` serves a build, drives it through a list of steps, and tears everything
down. Steps are given with `--do`, one to a flag, carried out in the order
written; `--script FILE` reads them one to a line instead, and `-` reads them
from standard input.

```bash
python3 tools/harness/harness.py run --bin build-wasm/bin --out /tmp/run \
    --do "wait menu" \
    --do "shot menu.png" \
    --do "click 640 700" \
    --do "wait 2s" \
    --do "shot options.png" \
    --do "diff menu.png options.png"
```

The steps:

| Step | What it does |
| --- | --- |
| `wait <state\|N\|Ns\|log:REGEX\|frames:+N\|js:EXPR>` | Waits for a [state](#states), a duration, a log line, that many more frames, or an expression that becomes true. A second word is the timeout in seconds. |
| `sleep <seconds>` | Waits, and asks nothing. |
| `move <x> <y>` | Moves the mouse, in game coordinates. |
| `click`, `down`, `up` `<x> <y> [button]` | `left`, `middle` or `right`; `click` presses and releases. |
| `drag <x1> <y1> <x2> <y2> [button]` | Press, two moves, release. |
| `wheel <x> <y> <dx> <dy>` | A wheel event at a position. |
| `tap <x> <y>` | A touch down and up in one place. |
| `touch <down\|move\|up> <x> <y>` | One touch point, for a gesture spread over several steps. |
| `key <name>` | `escape`, `enter`, `f1`, `a`, `1`, `ctrl+s`, and the rest of what the engine's own key table names. |
| `text <string>` | Typed one key at a time. |
| `shot <path>` | A PNG screenshot. |
| `state [path]` | `OpenTS_State` and every counter the module exports, as JSON. |
| `log [path]` | Everything the page and the engine printed. |
| `diff <a> <b> [pixels]` | [Compares two screenshots](#comparing-screenshots); fails the run over a pixel budget. |
| `eval <javascript>` | An escape hatch. Its value is printed. |
| `expect <javascript>` | The same, and a false value fails the run. |

A relative path in a step lands under `--out` (the working directory by
default). `--report PATH` writes the whole run — every step, its result and how
long it took, the complete log, the last state, and what the server was asked
for — as JSON.

The run exits non-zero when a step fails or a wait times out, and `130` when it
is interrupted. `--headed` shows the browser, `--hold` keeps the run open after
the last step, and `--verbose` prints the engine's output as it arrives.

`serve --bin DIR` runs only the server, for driving a build from a browser of
one's own.

**The port is never chosen.** The operating system picks a free one and the
harness prints it; `8765` is refused even if offered, because the container and
the developer's own server use it.

## States

`wait` takes a name so that "the menu is up" means the same thing in every run.
The states are derived from what the page already reports — `window.OpenTS_State`,
the counters the module exports, and what the engine prints — and nothing about
them is a guess about the picture on screen.

| State | Reached when |
| --- | --- |
| `module` | The module's exports are on `Module`: the WebAssembly is instantiated. |
| `main` | `OpenTS_State.started`, which the page sets as it enters `callMain`. |
| `frame` | The first frame is drawn (`OpenTS_Browser_Frames` is at least one). |
| `init` | The engine printed `Game Init Completed.` (`code/init.cpp:546`). |
| `menu` | `init`, and then settled. |
| `scenario` | The engine printed `Reading scenario:` (`code/scenario.cpp:350`). |
| `playing` | `scenario`, and then settled. |

**Settled** means the log has said nothing for `--quiet` seconds (two by
default) and frames advanced across that same window: the phase has stopped
reporting progress and the loop is turning. That is the whole of the rule, and
it is worth knowing its edge: a phase that is quiet for its own reasons settles
early, and a mission that opens with a briefing movie prints nothing while it
plays. `GDI1A.MAP` from `--scenario` settles on the map itself, but where a
scenario has more in front of it, wait for a further `frames:+N` or for a log
line of the mission's own rather than trusting `playing` alone.

`wait log:REGEX` and `wait js:EXPR` are there for everything these do not name.

## Game coordinates

Every position a step takes is in the game's frame — the same coordinates
`Window_Point_To_Game` hands the engine — and the harness does the translation
onto the page. It is the translation people got wrong by hand: the canvas is
sized in CSS pixels, its drawing buffer in device pixels, and the frame is
letterboxed inside that buffer.

The run pins what the translation depends on, so it is the same twice:
`--window WIDTHxHEIGHT` (1280x800 by default) is imposed on the page whether the
browser is headed or headless, and `--scale` fixes the device pixel ratio at 1.
With the page's default `?display=native` the frame is then the window, and a
game coordinate is a page coordinate. `--display WIDTHxHEIGHT` pins a frame
instead and the harness follows `Update_Scale_Info` (`code/video.cpp:148`) to
find where it lands.

`eval JSON.stringify(window.OpenTS_Harness.geometry())` prints what the harness
believes: the canvas box, the device pixel ratio, the drawing buffer, the game
frame, and the frame's destination rectangle inside the buffer.

Touch is presented to the page unless `--no-touch` is given. Nothing in the
engine or in the page asks whether the device has a touch screen, so a
mouse-only run is unaffected by it.

## Startup options and SUN.INI

The page has no command line, so the query string stands in for one, and the
harness fills it in from flags rather than from a hand-edited copy of the page:

| Flag | Becomes |
| --- | --- |
| `--scenario NAME` | `?scenario=` |
| `--campaign NAME` | `?campaign=` |
| `--display native\|scaled\|WxH` | `?display=` |
| `--arg SWITCH` | `?arg=`, repeatable, passed to the engine verbatim |
| `--query NAME=VALUE` | anything else the page reads, such as `hud=on` or `jspi=ignore` |
| `--disc PATH` | `?image=`, in the order given |

Settings that exist only in the configuration file are given as
`--ini SECTION.KEY=VALUE`, repeatable:

```bash
python3 tools/harness/harness.py run --bin build-wasm/bin --ini Video.UIScale=200 ...
```

The harness writes those into a `SUN.INI` beside the module, before `main` runs
and outside the persistent directory the run reconciles with IndexedDB. The
engine's file layer looks there before it asks a disc image
(`code/win32compat.cpp:428`), so this is what an installed settings file would
have been. Nothing on disk is edited and no copy of the page is made.

Only the keys given are written, so every other setting is whatever the engine
defaults to; a `SUN.INI` a disc carries is shadowed for that run.

## Observing

`state` returns, and optionally writes, one snapshot: `OpenTS_State` — frames,
waits, saves written, whether storage is persistent — together with every
zero-argument `OpenTS_*` counter the module exports, which is where the image
request, fetch, read-ahead, stall and deferred-read figures live. The list is
read off the module rather than kept here, so a counter added to the engine
appears without this tool being changed.

`log` is everything the page and the engine printed, in order, with a timestamp
and the console level, including the lines the page's own diagnostic panel drops
once it is full. `--report` carries the same log, so a failed run explains
itself without a second run.

## Comparing screenshots

`diff A B` reports whether two screenshots are identical, and when they are not,
how many pixels differ, the box that encloses them, and the largest difference
any one channel showed. `--threshold` lets a channel differ by that much and
still count as the same. As a step it takes a pixel budget and fails the run
when the difference is over it, which is how "this change is pixel-identical" is
established rather than asserted.

`harness.py diff A B` does the same to two files without running anything.

## Cleanup

A run leaves nothing behind, however it ends.

- The browser is launched into a profile directory of its own under the system
  temporary directory, named with a fixed prefix, and into its own process
  group. Teardown signals the group, so the renderer and GPU children go with
  the browser process, and removes the profile.
- The server is shut down and its port closed.
- The teardown runs on success, on a failed step, on an exception, and on an
  interrupt. `SIGTERM` is turned into an interrupt for the same reason.
- Nothing is shared with the developer's own browser: not the profile, not a
  port, not a window.

`doctor` reports any browser that survived anyway — there is one way for that
to happen, which is the harness itself being killed outright — and
`doctor --reap` ends it. A browser whose harness is still running belongs to
somebody else's live run: it is never reported and never reaped, because a
stray hunt once ended a run that was in progress.

## Why it is not a test

The harness reads the game data off the developer's own discs, and
[automated checks must not depend on proprietary game assets](../CONTRIBUTING.md#validation).
It is therefore deliberately not registered with CTest and no part of
`ctest --test-dir <build>` reaches it. The CTest suite covers the engine's own
layers and reads no game data; this is the tool for the evidence a behavior
change needs on top of that, and its results are reported as runtime
observations, never as build or test results.

## What has been run

| | |
| --- | --- |
| Date | August 31, 2026 |
| Host | macOS 26.5.1, Python 3.14.7, Google Chrome 151.0.7922.175, headless |
| Build | A `Release` JSPI browser build's `Game.js`, `Game.wasm` and `index.html` |
| Data | `FIRESTORM.iso`, `TS1.iso` and `TS2.iso`, served over the harness's own range server |

- `menu` was reached eleven to thirteen seconds after the page opened, on the
  disc chooser. A `click 330 400` took it to the main menu and a `tap 640 700`
  opened the **Options** dialog there, both given in game coordinates.
- `GDI1A.MAP` from `--scenario` reached `playing` on the map itself, two seconds
  after `scenario`. A click and a tap were dispatched into the running mission.
- `--display 1024x768` in the same 1280x800 window letterboxed the frame to
  `dest [107, 0, 1066, 800]`, and a `click 589 370` in game coordinates landed
  on the Firestorm disc. A click that ignored the offset and the scale would
  have landed on the Tiberian Sun one, so the translation is established rather
  than assumed.
- `--ini Video.UIScale=200` was written and read: the engine printed
  `UIScale = 200 (drawn at 2)` out of `Options::Load_Settings`.
- Two screenshots of a still screen compared identical over all 1,024,000
  pixels; screenshots either side of the click compared as 1,021,540 differing
  with a bounding box, and `--budget 1000` failed the comparison as it should.
- `state` returned `OpenTS_State` with every `OpenTS_Iso_*` and
  `OpenTS_Browser_*` counter, the stall record and the deferred-read count among
  them.
- The server answered `206` with a `Content-Range` for a byte range, `200` for a
  whole file, and `416` for a multi-range request rather than the whole file.
- Teardown was checked after a run that succeeded, a run whose step failed, a
  `SIGINT` and a `SIGTERM`: each left no browser process, no listening port and
  no profile directory. A harness killed with `SIGKILL` did leave its browser,
  which is what `doctor` then reported and `doctor --reap` ended; another
  developer's run was in progress throughout and was neither reported nor
  touched.

Not established: any browser other than Chrome, any host other than macOS, an
Asyncify build, a device pixel ratio other than 1, and the headed mode.
