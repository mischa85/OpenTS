# Building OpenTS

> [!IMPORTANT]
> Visual Studio 2022 Win32 Debug and Release builds are supported. Both have
> been verified from a fresh CMake configuration. A successful build
> establishes compilation, not runtime behavior.

## Supported target

| Component | Requirement |
| --- | --- |
| Host and architecture | Windows, 32-bit (`Win32`) target |
| Processor | SSE2, so a Pentium 4 or Athlon 64 onward |
| Generator and compiler | Visual Studio 2022 MSVC 19.30 or newer |
| Windows SDK | A Visual Studio-installed Windows SDK |
| CMake | 3.23 or newer |
| C++ language level | C++20 |
| Configurations | Debug and Release |

Other generators, compilers, architectures, and configurations are not
supported by the current tree. A WebAssembly target is
[in progress](#webassembly-in-progress-and-unsupported); it builds, links, and
runs under Emscripten, and it is not supported.

Install Visual Studio 2022 with the **Desktop development with C++** workload,
a Windows SDK, CMake 3.23 or newer, and Git for Windows.

## Dependencies

The renderer is built on [bgfx](https://github.com/bkaradzic/bgfx), vendored as the
`thirdparty/bgfx.cmake` submodule and pinned to a tested tag. It carries bgfx, bx, and
bimg as submodules of its own, so the checkout must be recursive:

```powershell
git submodule update --init --recursive
```

A fresh clone can do the same in one step with `git clone --recurse-submodules`.
Configuration fails with instructions if the submodule is missing. Updating the
dependency means moving the submodule to a new tag in its own change.

## Configure and build

Run these commands from the repository root in PowerShell:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Debug
cmake --build build --config Release
```

CMake normally discovers Visual Studio through the Visual Studio Installer. If
the installation is not registered, provide its installation directory and
product version through `CMAKE_GENERATOR_INSTANCE`.

The generated solution exposes only Debug and Release. Successful builds write
the engine executable under `build/bin/<configuration>/` under its runtime
name and copy the runtime files into `TS_RUN_DIR`, which defaults to `Run/`:

| Configuration | Runtime files |
| --- | --- |
| Debug | `GameD.exe`, `GameD.pdb`, `GameD.map`, `Language.dll` |
| Release | `Game.exe`, `Game.pdb`, `Game.map`, `Language.dll` |

`Language.dll` has the same name in both configurations, so the most recently
built configuration replaces the previous copy in `Run/`. Compiler and linker
intermediates remain under the selected build directory.

## Build from Visual Studio Code

Visual Studio Code (VSCode) support includes (assuming recommended extensions are installed):

- pre-configured CMake Tools settings;
- build tasks corresponding to the configs (a configure task and a config
  picker, plus two hidden per-configuration ones backing the launch configs);
- launch and attach configs;
- full Test Explorer integration.

Standard VSCode shortcuts (`Ctrl+Shift+B`, `F5`, `Ctrl+F5`) and interface apply.

## WebAssembly, in progress and unsupported

> [!WARNING]
> The WebAssembly target builds, links, and runs, and it is still unsupported.
> Continuous integration does not build it, no part of the port has been
> compiled with MSVC, and the observations recorded under
> [what has been run](#what-has-been-run) are the whole of the evidence for it.
> Visual Studio 2022 Win32 remains the supported target.

The build accepts the Emscripten toolchain alongside MSVC. Emscripten's wasm32
is ILP32, which gives the engine the 4-byte pointers and 4-byte `long` its
layouts assume, the same as Win32 x86; the hard `FATAL_ERROR` on any other
pointer width at `code/CMakeLists.txt:6` applies to both toolchains and wasm32
satisfies it.

```bash
source /path/to/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-wasm -G Ninja -DCMAKE_BUILD_TYPE=Debug
ninja -C build-wasm
```

Emscripten splits an executable in two, so a successful build writes the loader
and the module it looks for beside it:

| Configuration | Build output under `build-wasm/bin/` |
| --- | --- |
| Debug | `GameD.js`, `GameD.wasm` |
| Release | `Game.js`, `Game.wasm` |

A configuration built with `-DOPENTS_WASM_SUSPEND=ASYNCIFY` writes
`Game-asyncify.js` and `Game-asyncify.wasm` instead, so the two artifacts can
sit in one served directory; see [build options](#build-options).

A post-build step stages both halves into `TS_RUN_DIR` (`code/CMakeLists.txt:556`
and `:566`), which is what a node run needs. The page a browser run is served
from is generated separately, as `build-wasm/bin/index.html` from
`wasm/game.html` (`wasm/CMakeLists.txt:85`); it is not staged into the run
directory, so a browser run is served out of `build-wasm/bin`.

`wasm/demo.cpp` builds a second, unrelated target, `opents-wasm-demo`, that
links the renderer seam and nothing else (`wasm/CMakeLists.txt:22`). It is not
part of the game.

### Build options

Both options exist only under Emscripten and do not appear in an MSVC cache.

| Option | Default | Effect |
| --- | --- | --- |
| `OPENTS_WASM_SUSPEND` | `JSPI` | How the engine's not-yet-flattened waits hand the thread back to the page (`code/CMakeLists.txt:378`). One of `JSPI`, `ASYNCIFY`, or `NONE`; any other value fails the configure. |
| `OPENTS_WASM_NODERAWFS` | `ON` | Links `-sNODERAWFS=1` (`code/CMakeLists.txt:454`), handing the module the host filesystem. It is node-only: a page built with it throws before `main` runs, so a browser build is configured with `-DOPENTS_WASM_NODERAWFS=OFF`. |

`OPENTS_WASM_SUSPEND` decides both the mechanism and the artifact's name, since
the two suspending builds are the same engine and a served directory holds both:

| Value | Links | Module written | What it buys |
| --- | --- | --- | --- |
| `JSPI` | `-sJSPI` | `Game.js`, `Game.wasm` | The virtual machine suspends a real WebAssembly stack. Nothing is instrumented, so nothing is paid for at run time. Not in a released Safari. |
| `ASYNCIFY` | `-sASYNCIFY -sASYNCIFY_STACK_SIZE=65536` | `Game-asyncify.js`, `Game-asyncify.wasm` | Binaryen rewrites the module so an instrumented function can unwind and rewind. Plain WebAssembly, so it runs anywhere, and it is [paid for in size and speed](#the-cost-of-the-asyncify-build). |
| `NONE` | nothing | `Game.js`, `Game.wasm` | Nothing carries a wait. The engine keeps the thread, the page stops answering at the first one, and says so once. This is the destination rather than a way to run the game: what such a build fails at is the work `docs/WASM-PORT.md` A.6 still has left. |

Both suspending values define `OPENTS_WASM_JSPI` for the compiler, which the
source reads as "a wait can suspend" rather than as a named mechanism; `NONE`
defines nothing.

### How the target differs from the Win32 one

| Component | Treatment |
| --- | --- |
| `.rc` resources | Excluded; a Visual Studio toolchain input (`code/CMakeLists.txt:72`). The version and icon resources have no replacement. |
| `code/language/` | Not built (`CMakeLists.txt:71`), because `Language.dll` is a Win32 resource library. The strings are not lost: `code/peresource.cpp` reads the shipped library as a data file and walks its PE resource directory, so `Fetch_String` and `Fetch_Resource` answer out of the same library the Win32 build loads. |
| `code/wonline.cpp` | Excluded (`code/CMakeLists.txt:53`); it drives a service retired in 2004 through ATL. `wonlinestub.cpp` supplies what the rest of the engine references. |
| `tests/` | Built. `logstress` is Win32-only (`tests/CMakeLists.txt:2`) and `tests/audio` builds only here (`:18`). |
| Renderer | bgfx's OpenGL ES 3 renderer, reached through WebGL 2 (`code/CMakeLists.txt:318`). `thirdparty/CMakeLists.txt:32` compiles the bgfx tree with `-msimd128`, because Emscripten reports an x86 processor and bx therefore asks for SSE4.2 intrinsics that clang lowers to WebAssembly SIMD only when that feature is enabled. |
| Exceptions | `-fwasm-exceptions` rather than `-fexceptions` (`code/CMakeLists.txt:165`), because `-fexceptions` routes unwinding through `invoke_` imports that Emscripten declares suspending whenever JSPI is on, which traps in a static initializer. |

### Where the WebAssembly target finds game data

The engine opens its archives out of the directory it runs in, through the Win32
file API that `code/win32compat.cpp` puts on POSIX. Paths are resolved
case-insensitively when the exact spelling is missing, so an archive installed as
`tibsun.mix` answers the engine's `TIBSUN.MIX` on a case-sensitive filesystem.

**Under node**, the directory that reaches is the host's own, which
`-sNODERAWFS=1` hands the module directly, so a run is

```bash
cd Run && node GameD.js
```

with the game data staged in `Run` exactly as the Win32 build expects it.

**In a browser** there is no host filesystem, so the data is left on a web server
as disc images and read with HTTP range requests. Configure with
`-DOPENTS_WASM_NODERAWFS=OFF`, serve `build-wasm/bin` and the images from a
server that answers ranges, and open `index.html`. The images are named by
`Module.opentsImage`, set on the page before the module loads, which takes a
list or a single name (`code/isohttp.h:214`); the shipped `wasm/game.html`
names the three original discs beside the page and lets `?image=` override them.

Several images mount at once and are searched in the order they are named, each
contributing its `INSTALL` directory and then its root, with the first answer
winning (`code/win32compat.cpp:647`). Naming Firestorm ahead of the base discs
is therefore what an installation that upgraded over the base game looks like.
A volume is mounted lazily, on the first name the host cannot answer for, and
reads shorter than a block are served from a per-image block cache rather than
one request apiece (`code/isohttp.h:144`).

A server that ignores the range and answers with the whole image is rejected
rather than accommodated: the transport requires a `206` and a `Content-Range`
it can read (`code/isohttp.cpp:70`, `:117`). Under node the same list can be
named through the `OPENTS_IMAGE` environment variable.

Every read is synchronous, so one the caches miss stops the engine until the server
answers. A single reader is exempt. `ISODeferredReadClass` (`code/iso9660.h:81`)
marks a scope in which a block source may answer that the bytes are not here yet:
it asks for them without waiting and reports a read of nothing, which the layers
above pass up unchanged rather than retrying (`code/iso9660.cpp:545`,
`code/rawfile.cpp:491`, `code/win32compat.cpp:1122`) and which the scope tells
apart from the end of a file. The score player is the only caller —
`ThemeClass::Play_Song` asks for a stream that may decline (`code/theme.cpp:439`)
and the two refill sites in `code/dsaudio.cpp` enter the scope around that
stream's reads alone, so sound effects, speech, and every ordinary file read still
wait for what they asked for. A block source with the bytes at hand never
declines, so nothing changes on any other target. `OpenTS_Iso_Deferred` counts the
reads that declined, beside the stall figures the same page reports.

The build offers no `--preload-file` bundling. [README](../README.md) is
explicit that OpenTS supplies the engine and not the game data, so a deployment
that serves an image is one serving data it has the right to serve.

The menu withholds what a page cannot play. The three network games and the World
Domination Tour are shown but not offered, and the exit is dropped entirely, since
there is nothing to quit back to (`code/newmenu.cpp:199`). An unavailable choice wears a disabled face
dimmed from its own lettering rather than the one the game data drew for it, so that
every withheld choice reads the same way whether artwork was supplied for it or not
(`code/grphmimg.cpp:127`). Every other target still uses the supplied artwork.

The first-run `EVA.VQA` sequence does not play here. It belongs to a first run
that follows an installation, and a page installs nothing, so it covers no
setup; `PlayIntro=true` under `[Intro]` in `SUN.INI` asks for it anyway
(`code/startup.cpp:642`). Every other target still plays it once.

### Which module a page loads

How a wait hands the thread back is decided at link time, and no one module runs
everywhere, so a deployment that means to be reachable from every browser holds
both artifacts. Build the tree twice into the same served directory:

```bash
emcmake cmake -S . -B build-wasm -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DOPENTS_WASM_NODERAWFS=OFF
emcmake cmake -S . -B build-wasm-asyncify -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DOPENTS_WASM_NODERAWFS=OFF -DOPENTS_WASM_SUSPEND=ASYNCIFY
```

Either configuration generates the same `index.html`, which names both modules
and picks between them before it fetches anything. It asks for
`WebAssembly.Suspending` and `WebAssembly.promising`, the whole of what a JSPI
module touches while it is being created, and loads `Game.js` when both are
there and `Game-asyncify.js` when they are not. A browser therefore never
fetches a module it could not have run. `?jspi=ignore` takes the answer out of
the decision and loads `Game.js` regardless, which is how a `NONE` build is
reached from the same page.

The gate screen remains, and it now reports a deployment rather than a browser:
it is shown when the module the page chose answers with a `404`, and it names
which one was missing. Serving only `Game.js` leaves a browser without JSPI
looking at it.

The side stack an unwind spills its locals to is sized explicitly because
Binaryen's 4KB default is not enough for this engine: linked with the default it
reaches the second frame and aborts with `RuntimeError: unreachable` out of
`maybeStopUnwind`, which is why `-sASYNCIFY_STACK_SIZE=65536` is on the link.

Emscripten warns that `ASYNCIFY=1` is not compatible with `-fwasm-exceptions`
and that "parts of the program that mix ASYNCIFY and exceptions will not
compile". The engine links and runs anyway, and a mission plays; take the
warning as the standing reason to run the Asyncify artifact against the JSPI one
rather than as a settled question, because a throw across a suspended frame is
not something the observations below exercised.

#### The cost of the Asyncify build

Measured on the tree and toolchain under [what has been run](#what-has-been-run).
Size, as the container builds and serves the two — `Release`, `-O1 -DNDEBUG`:

| | JSPI | Asyncify |
| --- | --- | --- |
| `.wasm` | 3,859,221 bytes | 11,759,933 bytes (3.05x) |
| `.wasm`, gzipped | 1,247,022 bytes | 5,182,539 bytes (4.16x) |
| `.js` | 446,915 bytes | 456,785 bytes |

The same pair built with `-O1 -g2 -DNDEBUG` is 4,371,890 against 12,276,262
bytes. Nothing is contained: `ASYNCIFY_ONLY` is not used, so the rewrite reaches
the whole engine, and the size is what that costs.

Speed was measured in one campaign mission, `GDI1A.MAP`, in three interleaved
pairs of runs against those same served modules — a 60 second window opened 130
seconds after the page did, so past the briefing movie, with the same disc
images on the same machine. Every one of the six runs held the display's
refresh rate exactly: 7,200 to 7,209 yields in 60 seconds on a 120Hz panel. The
cost therefore shows in what a frame took rather than in how many arrived:

| | JSPI | Asyncify |
| --- | --- | --- |
| Main-thread task time per frame | 0.783, 0.814, 0.789 ms | 0.993, 0.915, 0.967 ms |
| Share of the wall clock | 9.4%, 9.8%, 9.5% | 11.9%, 11.0%, 11.6% |

**About a fifth more CPU for the same frame** — the three pairs give 27%, 12%
and 23% — against a frame budget that is four fifths idle either way, so
nothing was dropped. It is a good deal short of the "something like 50% or so"
Emscripten's own documentation warns of on size and speed together, though the
size is well past it. This is one mission on one machine with headroom to
spare; a build already missing frames would show the same tax as lost frames
instead.

### In a container

`Dockerfile` builds the page with the pinned Emscripten and serves the result
from nginx, which answers ranges without being asked to. `compose.yaml` publishes
it and mounts the discs, so a run needs neither a toolchain nor a server of one's
own:

```bash
OPENTS_DISCS=~/Downloads docker compose up
```

The image is built twice over, once for each of the two modules the page chooses
between, so it serves a browser with JSPI and one without alike.

It is also configured for a reverse proxy in front of it. nginx builds a
redirect's `Location` out of the scheme and address it sees itself, which behind
a TLS terminator is plain HTTP and the container's own address, and it does not
consult `X-Forwarded-Proto` or `X-Forwarded-Host` to correct that; the image
therefore turns `absolute_redirect` off, so a redirect carries only the path and
the browser resolves it against the origin it actually used.

`OPENTS_DISCS` names the directory holding `FIRESTORM.iso`, `TS1.iso` and
`TS2.iso`; the three are mounted read only under the names the page looks for,
and `OPENTS_PORT` moves the published port from its default of 8765. The discs
are never copied into the image — `.dockerignore` keeps `Run/` and every `*.iso`
out of the build context, because an image carrying the game data would be
redistributing it.

The build context is the working tree rather than a fresh clone, so
`thirdparty/bgfx.cmake` has to be populated first:

```bash
git submodule update --init --recursive
```

### Tests

`tests/` builds under Emscripten, and the Emscripten toolchain file points
`CMAKE_CROSSCOMPILING_EMULATOR` at the emsdk's node, so `ctest` runs the
harnesses without further configuration:

```bash
ctest --test-dir build-wasm
```

Eighteen tests are registered there: `iso9660`, `lcw`, `lcwstream`,
`lcwuncomp`, `soscodec`, `vqacodec`, `voxel`, `lighting`, `win32file`,
`resources`, `win32process`, `win32user`, `win32window`, `com`, `save`,
`audiobackend`, `timer`, and `isohttp`. The last three exercise the WebAssembly
target's own layers and build only there; an MSVC configuration registers the
rest with `logstress` alongside. None of them reads game data.

`save` drives the compound file a saved game is written into. It builds
`code/docfile.cpp` on both targets, so the same writer and reader are checked in
both places, and under MSVC it additionally reads what it wrote with OLE and
reads what OLE wrote with `docfile.cpp` — the interchange that decides whether a
save crosses between the two builds, and that only Windows can establish.

### What has been run

| | |
| --- | --- |
| Date | August 30, 2026 |
| Tree | `732f984` |
| Toolchain | Emscripten 6.0.8, CMake 4.4.2, Ninja 1.13.2, macOS host; node 24.19.0 from the emsdk for `ctest` |
| Configuration | `Debug`, both options at their defaults |

A fresh `emcmake` configure, an engine build, and `ctest --test-dir build-wasm`
completed: eleven tests, eleven passed. Debug and Release engine binaries have
both been produced.

Observed in a browser, from a disc image over HTTP: the graphical main menu, a
campaign mission started and played, unit movement and selection, building
placement, terrain, the radar, the sidebar and its cameos, an audio device
opening, and movies playing.

The three values of `OPENTS_WASM_SUSPEND` were run separately, on August 30,
2026, `Release` with `-O1 -g2 -DNDEBUG` and `-DOPENTS_WASM_NODERAWFS=OFF`, in
Chrome 151 on a macOS host, from the same three disc images over HTTP:

- `JSPI` and `ASYNCIFY` each reached the disc chooser and each played `GDI1A.MAP`
  from `?scenario=`, and under Asyncify the in-game **Options** dialog opened
  over a still-advancing game — the `Dialog_Message_Handler` re-entry of
  `Main_Loop` that `docs/WASM-PORT.md` A.3 names as the nesting an Asyncify
  unwind has to survive.
- A browser without JSPI was simulated by deleting `WebAssembly.Suspending` and
  `WebAssembly.promising` before any document script ran. The page then fetched
  `Game-asyncify.js` and `Game-asyncify.wasm` and nothing else; with only
  `Game.js` on the server it fetched neither module and showed the gate screen.
  **This was not run in a real browser that lacks JSPI**, Safari included.
- `NONE` loaded through `?jspi=ignore`, started, read the discs, and then wedged
  the tab, which is what that configuration means.
- `ctest --test-dir` passed against the `JSPI` and the `ASYNCIFY` build
  directories alike: 17 of 17 then, and 18 of 18 on August 31, 2026 once `save`
  was registered.

Not established, and not to be read into the above:

- **Sound.** The backend queues samples to OpenAL over Web Audio
  (`code/audiobackend.cpp:280`), and it also carries a silent fallback that
  advances the play cursors off the wall clock when the page will not start
  audio (`code/audiobackend.cpp:226`). An advancing cursor is therefore not
  evidence of a sound, and nobody has confirmed hearing one.
- **Saving and loading.** The container is implemented and covered:
  `StgCreateDocfile`, `StgOpenStorage`, and `StgIsStorageFile` answer out of
  `code/docfile.cpp` (`code/win32compat.cpp:3167`, `:3179`, `:3191`), and the
  `save` harness passes here. Somewhere to keep one is implemented too: the page
  mounts `/save` from IndexedDB and reads it back before the engine starts
  (`wasm/game.html:357`), a bare name resolves into that directory ahead of the
  game directory (`code/win32compat.cpp:421`), a wildcard scan folds it into the
  results (`code/win32compat.cpp:1928`), and a write to it is flushed with
  `FS.syncfs` (`code/win32compat.cpp:452`). What is not established is any of it
  end to end: no save has been written by `Save_Game` or read back by
  `Load_Game` (`code/saveload.cpp`) in a browser, and the dialog that reaches
  them is dead for the reason the next entry gives.
- **The owner-draw Win32 front end.** `code/win32user.cpp` is a real in-process
  window manager and the dialog-template entry points are no longer stubs:
  `CreateDialogIndirectParamA` builds the dialog and its controls out of the
  template (`code/win32user.cpp:2432`), converting dialog units at `:2463` and
  `:2485`. Dialogs open and are drawn. What has not been established is each
  dialog doing its job: the options and load screens have been used, skirmish
  setup has not.
- **The mouse cursor.** `code/win32window.cpp:541` encodes each cursor frame as
  a PNG data URL for `canvas.style.cursor`, but what a player sees is still the
  browser's own arrow, and that path is under active work.
- **Anything under MSVC.** No part of this port has been compiled on Windows.

[WebAssembly target status](wasm-compile-status.md) records the state of the
port's subsystems and the Win32 substitute they are built on.
[WebAssembly port design](WASM-PORT.md) records the design the port was started
from, and where it was wrong.

## Build identity

The project version is declared once, by `project(OpenTS VERSION ...)` in the
top-level `CMakeLists.txt`, with any SemVer prerelease label alongside it in
`OPENTS_VERSION_PRERELEASE`, because `project()` accepts numbers only. Both must
match the development entry of the manual's release registry, which
`python manual/tools/manage.py check` enforces. That tool runs on its own pinned
Python and packages rather than on whatever `python` resolves to; the
[manual's README](../manual/README.md) owns setting it up, and
`manage.py doctor` reports what is missing.

Each build writes two generated headers from that version and the repository
state:

| Header | Contents |
| --- | --- |
| `opents_version.h` | The version components, the version string, a prerelease flag, and the packed version number |
| `opents_build.h` | The commit, branch, commit date, whether tracked files were modified, and the version as it is displayed |

The packed version number is the major, minor, and patch components in one byte
each. The save game stamp and the network version are that number, so different
release-cycle versions refuse one another. Development snapshots within one
cycle share the number; their saves, replays, and network sessions are not
promised to interoperate. A prerelease is not distinguished there and carries
the identity of the release it leads up to.

Everything that names a version to the player reads these headers: the version
resources of `Game.exe` and `Language.dll`, the title screen, the version
dialog, the crash report, and the debug log's opening banner. A build reports
its version with the commit it came from, as in `0.1.0 (ab12cd3)`, and adds a
modification marker when tracked files differ from that commit. The commit is a
diagnostic build identity, not an enforced save or network compatibility stamp.
Configuring with
`-DOPENTS_OFFICIAL_BUILD=ON` reports the version alone, for a build published
under the version it declares.

The version stamp is rewritten only when the version changes, so an ordinary
commit does not recompile the code that reads it. The build stamp refreshes on
every build, so committing is reflected without reconfiguring, and an unchanged
stamp is not rewritten.

A detached checkout, which is what building a tag or a pull request produces, has
no branch of its own. The stamp then reports a ref that points at the commit,
preferring a tag, so a continuous integration build of a pull request reports
that pull request rather than the bare word `HEAD`.

Git is not required. A build with no Git available, or from a source archive
with no repository, succeeds and reports the commit as `unknown` and the version
without one.

## Continuous integration

The `Engine` workflow builds every pull request that is ready for review and
every push to `main` that touches the engine, its build files, or the workflows
themselves. A draft pull request builds nothing until it is marked ready, which
starts the build for the commit it then carries. The `Engine nightly` workflow
builds on a daily schedule; when nothing has been committed since the last one,
the scheduled run cancels itself so that the latest successful nightly is
always one that produced artifacts, which keeps the nightly download links
resolvable. Both call the same reusable `Engine build` workflow, which on a
Windows runner with Visual Studio 2022 configures and builds Win32 Debug and
Release with the commands above, runs the CTest suite, and uploads each
configuration's executable, language library, and symbol file as an artifact
named for the configuration and the short commit. The linker map is not
uploaded, because the symbol file covers the same ground. After a successful
pull-request build, the `Engine build comment` workflow keeps one comment on
the pull request with direct nightly.link downloads of that build's artifacts.

The `Engine release` workflow runs when a GitHub release is published. It
builds the release's commit with `-DOPENTS_OFFICIAL_BUILD=ON`, packages
`Game.exe`, `Language.dll`, and `Game.pdb` into a zip named for the release
tag, attaches the zip to the release, and appends release notes generated from
the manual's change records by `python manual/tools/manage.py release-notes`.
[Maintaining](../manual/MAINTAINING.md) owns the release procedure around it.

Continuous integration builds redirect `TS_RUN_DIR` to an empty directory, so an
uploaded artifact holds only the files that build produced.

Continuous integration establishes the same thing a local build does, on the
runner's toolchain. It does not establish runtime behavior.

## Verification boundary

The supported matrix was verified on August 16, 2026 with CMake 4.3.3, Visual
Studio 2022 Community 17.14.37328.6, MSVC 19.44.35228, and Windows SDK
10.0.26100. Fresh Win32 Debug and Release builds completed successfully. The
builds retain inherited MSVC warnings; warnings are not treated as errors, but
contributions should not add new warnings.

Build verification establishes that the supported toolchain compiles and links
the configured targets and produces the listed artifacts. Runtime behavior is
established separately, by play testing, and is outside this build-support
record.

The repository contains no maps, movies, audio, or other original game assets.
Keep legally obtained runtime data local and outside version control. Do not
commit populated run directories, original executables, proprietary SDKs, IDE
state, compiler output, generated CMake projects, or credentials.
