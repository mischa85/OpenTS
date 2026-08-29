# WebAssembly compile status

> [!IMPORTANT]
> The WebAssembly target is in progress and unsupported. It configures and
> nearly all of its translation units compile; the target does not link and has
> never been run. Nothing here is a support, build, or runtime claim.
> [Building OpenTS](BUILDING.md) owns build support.

This page sizes the work between a configured Emscripten tree and one that
compiles, so that the port can be measured rather than estimated. It is a
snapshot of a moving tree and goes stale; regenerate it rather than trusting
the numbers.

## What was measured

| | |
| --- | --- |
| Date measured | August 29, 2026 |
| Tree | `ec061be` plus uncommitted in-flight work |
| Toolchain | Emscripten 6.0.8, CMake 4.4.2, Ninja, macOS host |
| Configuration | `Debug`, `CMAKE_BUILD_TYPE=Debug` |
| Scope | Every `OpenTS` and `VQALib` translation unit; compile only, no link |

Compiling is per translation unit, so it does not depend on the link
succeeding, on bgfx, or on the resource DLL. The link is far behind this and
was not attempted.

## Reproducing

```bash
source ~/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-wasm -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

One translation unit at a time, which is how progress is measured:

```bash
ninja -C build-wasm code/CMakeFiles/OpenTS.dir/lcw.cpp.o
```

All of them, without stopping at the first failure:

```bash
ninja -C build-wasm -k 0 $(ninja -C build-wasm -t targets all \
    | grep -oE '[^ ]*(OpenTS|VQALib)\.dir/[^:]*\.o')
```

`build-wasm/compile_commands.json` carries the exact command for every
translation unit, including the ones excluded from the target above.

## Result

| | Translation units |
| --- | --- |
| Configured | 408 |
| Compile clean | 404 |
| Fail | 4 |

The four are named below. Before the Win32 substitute described in the next
section existed, the same sweep over the same tree read 81 clean out of 395
configured; the count of configured units moves as the port adds and removes
files.

## The Win32 substitute

Almost every failure in the earlier measurement was a missing Windows SDK
header, and the failures were fatal, so one absent header hid everything else
in the file. `windows.h` alone stopped 161 translation units and `comdef.h`
another 89.

Four headers now stand in for the Windows SDK and the MSVC C runtime on this
target, selected at each include site by `#if defined(__EMSCRIPTEN__)`. The
MSVC build's include set is unchanged: it still reaches the real headers
through the `#else` branch, and each substitute's body is inert outside the
WebAssembly target.

| Header | Stands in for | Contents |
| --- | --- | --- |
| `code/win32compat.h` | `windows.h` and the Win32 headers behind it, `comdef.h`, `unknwn.h`, `objidl.h`, `ole2.h`, `dsound.h`, `mmsystem.h`, `commctrl.h`, `shellapi.h`, `dbghelp.h`, `winioctl.h`, `shlwapi.h`, `tlhelp32.h`, `iphlpapi.h`, `rpc.h` | Types, constants, structures, the COM interface shapes, and roughly 300 stubbed entry points |
| `code/crtcompat.h` | `io.h`, `direct.h`, `conio.h`, `new.h`, `sal.h`, `sys/timeb.h`, and the MSVC spellings of the string and path helpers | Real implementations over POSIX; included by `always.h` |
| `code/winsockcompat.h` | `winsock.h` | The Windows spellings over the host's BSD sockets |
| `code/guidcompat.h` | `basetyps.h`, `initguid.h` | Whether `DEFINE_GUID` declares or defines |

`code/win32compat.cpp` holds the out-of-line half: the stub definitions, the
few real ones, and the layout assertions.

### Layout

wasm32 is ILP32 exactly as Win32 x86 is, which is why the engine's structures
survive the move, so every substituted type carries the width, signedness, and
alignment of its Win32 x86 original. `DWORD` is `unsigned long`, `LONG` is
`long`, `WPARAM` and `LPARAM` are pointer sized, `BOOL` is `int`, `RECT` is
four `LONG`s, `CRITICAL_SECTION` is twenty-four bytes.

This is checked rather than asserted. `win32compat.cpp` carries a block of
`static_assert`s over the sizes, alignments, and field offsets MSVC reports for
the same constructs on Win32 x86, including the two packed structures whose
size does not follow from their members: `WAVEFORMATEX` is eighteen bytes
because `mmsystem.h` packs the wave formats to one, and `BITMAPFILEHEADER` is
fourteen because `wingdi.h` packs it to two. A change that moves a field fails
the build instead of reshaping a saved game.

### Stubs

Nothing in the substitute implements Windows. A stub that quietly returned
success would be debugged for hours later, so every stubbed entry point names
itself at runtime and then returns what its Win32 original returns on failure.
Three forms, all in `win32compat.h`:

| Form | Used for |
| --- | --- |
| `WIN32_STUB(value)` | Reports the call and yields the Win32 failure value |
| `WIN32_STUB_VOID()` | Reports a call that returns nothing |
| `WIN32_STUB_ABORT()` | Reports and terminates, where no return value could carry the failure |

Reporting is once per entry point rather than once per call, so a stub inside a
frame loop names itself without burying the log.

A few routines are implemented instead of stubbed, and only where the answer is
genuinely available: the clocks (`GetTickCount`, `timeGetTime`,
`QueryPerformanceCounter`, `GetSystemTime`) over the host's monotonic and
realtime clocks, the rectangle arithmetic, `wsprintf` and the `lstr` string
helpers, the interlocked operations, which are exact on a single-threaded
target, and everything in `crtcompat.h`. Each says so where it is defined.

Two consequences are worth stating outright, because the engine depends on them
and a stub is not a substitute:

- **Locomotors are COM objects.** `ILocomotionPtr(CLSID_DriveLocomotion)` and
  its relatives activate an in-process COM object, and there is no COM runtime
  here to activate one with. `_com_ptr_t::CreateInstance` reports itself and
  yields a null pointer, so a unit that needs a locomotor gets none.
- **`__uuidof` is unavailable.** It needs `-fms-extensions` and
  `__declspec(uuid)`, which this target does not build with, so interface
  identity is not available. Conversion between the shim's interfaces uses
  `dynamic_cast` instead, which answers the same question for them, but a
  `QueryInterface` written against `__uuidof` fails.

## Still failing (4)

| Translation unit | Cause |
| --- | --- |
| `except.cpp` | Structured exception handling. `__try`, `__except`, and the `dbghelp` stack walk have no clang or WebAssembly equivalent; the crash handler needs a port, not a shim. |
| `wonline.cpp` | ATL. The WOL client derives its event sinks from `CComObjectRoot` and drives a `CComModule`; `atlbase.h` has no substitute here. |
| `xsurface.cpp` | An `_asm` block remains in `surface_quick_fill`. |
| `video.cpp` | A `Backend_Init` signature mismatch against `bgfxbackend.h`, which the renderer work owns. |

## Type and ABI differences from Win32 x86

Emscripten's wasm32 is ILP32, which is why the engine's layouts survive the
move. Not everything matches, and the build settles two of the differences:

| Property | MSVC Win32 x86 | Emscripten wasm32 | Settled by |
| --- | --- | --- | --- |
| `void *`, `long`, `size_t` | 4 bytes | 4 bytes | — |
| `char` signedness | signed | signed | — |
| `wchar_t` | 2 bytes | 4 bytes | `-fshort-wchar` |
| Strict aliasing | not assumed | assumed | `-fno-strict-aliasing` |
| `long double` | 8 bytes | 16 bytes | nothing yet |

`long double` is the outstanding one. It is not equivalent to `double` under
clang the way it is under MSVC, so anything that serializes or lays out a
`long double` differs between the two targets.

`size_t` is a second difference that costs nothing in layout and something in
source. It is four bytes on both, but it is `unsigned int` under MSVC and
`unsigned long` under Emscripten, so a `std::min(unsigned, size_t)` that
deduced one type under MSVC deduces two here. The fix is an explicit template
argument at the call, not a change of representation.

WebAssembly arithmetic is strict IEEE-754 with no excess precision and no
reassociation, which is what `/arch:SSE2` and `/fp:precise` buy under MSVC, so
the deterministic simulation's floating-point model carries over by default.
It carries over only as long as no fast-math option is introduced.

`__declspec` is resolved by `-fdeclspec`, which the build passes, including the
`__declspec(property)` accessors in `abstract.h`, `rect.h`, `trigger.h`, and
`trigtype.h`. The four calling-convention keywords are erased on the command
line; the single-underscore spellings `_cdecl`, `_stdcall`, and `_fastcall` are
erased in `crtcompat.h`.

## What still has no answer

One thing the build removes rather than replaces:

- **Version and icon resources.** `Sun.rc` and `except.rc` are resource-compiler
  input, so the WebAssembly target does not build them.

Localized strings are no longer among them. `code/language/` is still excluded,
because the DLL is built by the Visual Studio toolchain, but `code/peresource.cpp`
reads the shipped `Language.dll` as a data file and walks its resource directory,
so `Fetch_String` and `Fetch_Resource` answer from the same library the Win32
build loads.

The test harness under `tests/` is written against the Win32 API and is also
excluded.

Beyond those, the whole dialog layer -- `windlg.cpp`, `winfix.cpp`,
`winstub.cpp`, `ownrdraw.cpp`, and the option and lobby dialogs -- compiles
against the substitute but consists of Win32 dialogs end to end and does
nothing at runtime. So does the audio path, which is DirectSound, and network
play, whose sockets are WebSocket-backed under Emscripten and carry neither raw
IPX nor UDP.
