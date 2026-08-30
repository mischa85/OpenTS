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

A post-build step stages both halves into `TS_RUN_DIR` (`code/CMakeLists.txt:465`
and `:474`), which is what a node run needs. The page a browser run is served
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
| `OPENTS_WASM_JSPI` | `ON` | Defines `OPENTS_WASM_JSPI` and links `-sJSPI` (`code/CMakeLists.txt:345`). The engine's not-yet-flattened waits suspend on JavaScript Promise Integration; a build configured without it stops answering the page at the first wait, and says so once. |
| `OPENTS_WASM_NODERAWFS` | `ON` | Links `-sNODERAWFS=1` (`code/CMakeLists.txt:359`), handing the module the host filesystem. It is node-only: a page built with it throws before `main` runs, so a browser build is configured with `-DOPENTS_WASM_NODERAWFS=OFF`. |

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

The build offers no `--preload-file` bundling. [README](../README.md) is
explicit that OpenTS supplies the engine and not the game data, so a deployment
that serves an image is one serving data it has the right to serve.

### Tests

`tests/` builds under Emscripten, and the Emscripten toolchain file points
`CMAKE_CROSSCOMPILING_EMULATOR` at the emsdk's node, so `ctest` runs the
harnesses without further configuration:

```bash
ctest --test-dir build-wasm
```

Eleven tests are registered there: `iso9660`, `lcw`, `lcwstream`, `lcwuncomp`,
`soscodec`, `vqacodec`, `voxel`, `lighting`, `win32file`, `resources`, and
`audiobackend`. An MSVC configuration registers the same set with `logstress` in
place of `audiobackend`. None of them reads game data.

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

Not established, and not to be read into the above:

- **Sound.** The backend queues samples to OpenAL over Web Audio
  (`code/audiobackend.cpp:280`), and it also carries a silent fallback that
  advances the play cursors off the wall clock when the page will not start
  audio (`code/audiobackend.cpp:226`). An advancing cursor is therefore not
  evidence of a sound, and nobody has confirmed hearing one.
- **Saving and loading.** `StgCreateDocfile` and `StgOpenStorage` report
  `E_NOTIMPL` (`code/win32compat.cpp:2676`, `:2677`), so `Save_Game` writes
  nothing and returns false (`code/saveload.cpp:937`) and `Load_Game` returns
  false before it opens a stream (`code/saveload.cpp:1184`). A save would in any
  case land on the in-memory filesystem the tab discards
  (`code/win32disk.cpp:27`).
- **The owner-draw Win32 front end.** `code/win32user.cpp` is a real in-process
  window manager, but the dialog-template entry points are still stubs
  (`code/win32compat.cpp:2613`–`:2615`), so `OwnerDraw::Begin_Dialog` returns
  null (`code/ownrdraw.cpp:6737`). Skirmish setup and the save/load dialog then
  do nothing at all, and `Main_Options_Dialog` spins on the null handle without
  servicing the page (`code/mainopt.cpp:69`), which hangs the tab.
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
`python manual/tools/manage.py check` enforces.

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
