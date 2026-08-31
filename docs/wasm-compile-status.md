# WebAssembly target status

> [!IMPORTANT]
> The WebAssembly target is unsupported. [Building OpenTS](BUILDING.md) owns
> build support and records what has been built and run; nothing here is a
> support claim. This page is a snapshot of a moving tree and goes stale —
> regenerate the counts and line references rather than trusting them.

This page owns two things: the Win32 substitute the target is built on, and the
state of the subsystems standing on it. It was written as a compile-blocker
inventory, when the question was how many translation units would compile. That
question is closed — the target compiles, links, and runs — so what remains is
the shape of what was substituted and what is still missing behind it.

## What was measured

| | |
| --- | --- |
| Date measured | August 30, 2026 |
| Tree | `732f984` |
| Toolchain | Emscripten 6.0.8, CMake 4.4.2, Ninja 1.13.2, macOS host |
| Configuration | `Debug`, both WebAssembly options at their defaults |

Every translation unit except `code/wonline.cpp` compiles, and the engine links.
The earlier per-file measurement — 404 of 408 units clean, with `except.cpp`,
`wonline.cpp`, `xsurface.cpp`, and `video.cpp` failing — is superseded: all four
were resolved, and `wonline.cpp` is now excluded from the target by choice
rather than by failure (`code/CMakeLists.txt:53`). For reference, before the
Win32 substitute existed the same sweep read 81 clean out of 395.

## The Win32 substitute

Almost every failure in the first measurement was a missing Windows SDK header,
and the failures were fatal, so one absent header hid everything else in the
file. `windows.h` alone stopped 161 translation units and `comdef.h` another 89.

Four headers stand in for the Windows SDK and the MSVC C runtime on this target,
selected at each include site by `#if defined(__EMSCRIPTEN__)`. The MSVC build's
include set is unchanged: it still reaches the real headers through the `#else`
branch, and each substitute's body is inert outside the WebAssembly target.

| Header | Stands in for | Contents |
| --- | --- | --- |
| `code/win32compat.h` | `windows.h` and the Win32 headers behind it, `comdef.h`, `unknwn.h`, `objidl.h`, `ole2.h`, `dsound.h`, `mmsystem.h`, `commctrl.h`, `shellapi.h`, `dbghelp.h`, `winioctl.h`, `shlwapi.h`, `tlhelp32.h`, `iphlpapi.h`, `rpc.h` | Types, constants, structures, the COM interface shapes, and the entry points |
| `code/crtcompat.h` | `io.h`, `direct.h`, `conio.h`, `new.h`, `sal.h`, `sys/timeb.h`, and the MSVC spellings of the string and path helpers | Real implementations over POSIX; included by `always.h` |
| `code/winsockcompat.h` | `winsock.h` | The Windows spellings over the host's BSD sockets |
| `code/guidcompat.h` | `basetyps.h`, `initguid.h` | Whether `DEFINE_GUID` declares or defines |

Only two of the four carry an out-of-line half, and neither has a `.cpp` of its
own: what `crtcompat.h` and `winsockcompat.h` declare out of line is defined in
`code/win32compat.cpp`, under its C runtime and Winsock sections.

The out-of-line half started as one file and has since split by subject. Every
entry point below is declared in `code/win32compat.h` wherever it is defined, so
which file holds one is an arrangement of the substitute rather than anything an
include site or the MSVC build can see:

| File | What it implements |
| --- | --- |
| `code/win32compat.cpp` | The filesystem and its disc-image overlay, the layout assertions, the in-process COM runtime, the clocks, the mutexes and events, the heap, resources and version information, OLE serialization and the structured-storage bridge onto `docfile.cpp`, locale formatting, the MSVC C runtime, the Windows-only half of Winsock, and the registry, profile and console entry points nothing has been written for |
| `code/win32user.cpp` | An in-process window manager: window classes, handles, the message queue, `SendMessage`/`DispatchMessage`, the dialog-item protocol, dialog templates, and a message box drawn into the page; also the keyboard-state, menu, window-scroll-bar and context-help entry points, none of which a page answers |
| `code/win32ctrl.cpp` | The stock controls the window manager registers -- button, static, edit, list box, combo box, scroll bar, track bar, progress bar and hot key -- drawn and driven in process; the image list is stubbed beside them |
| `code/win32gdi.cpp` | The synthetic GDI: device contexts, objects, font measurement and text drawing onto the engine's own surfaces. The raster half -- the blits, the DIB sections and the device capabilities -- is stubbed |
| `code/win32window.cpp` | The canvas: its size, the pointer position, the cursor, the code page, and the display-mode enumeration a page stands in for a driver's |
| `code/win32process.cpp` | Who the process is: the module name and handle, the command line and its splitter, the locks and critical sections a one-threaded target answers at once, and the module, thread, process and DbgHelp entry points that have nothing underneath them |
| `code/win32timer.cpp` | `Sleep` and the multimedia timers as main-loop polls, over the millisecond clock `code/win32compat.cpp` supplies |
| `code/win32disk.cpp` | Free-space reporting, from `navigator.storage.estimate` in a page |
| `code/docfile.cpp` | The compound file the storage entry points answer out of; built on every target, so its bytes can be held against OLE's |

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
because `mmsystem.h` packs the wave formats to one (`code/win32compat.cpp:66`),
and `BITMAPFILEHEADER` is fourteen because `wingdi.h` packs it to two (`:67`).
A change that moves a field fails the build instead of reshaping a saved game.

### Stubs

Nothing in the substitute implements Windows for its own sake. A stub that
quietly returned success would be debugged for hours later, so every stubbed
entry point names itself at runtime and then returns what its Win32 original
returns on failure. Four forms, all declared in `code/win32compat.h:44`–`:76`:

| Form | Used for |
| --- | --- |
| `WIN32_STUB(value)` | Reports the call and yields the Win32 failure value |
| `WIN32_STUB_VOID()` | Reports a call that returns nothing |
| `WIN32_STUB_ABORT()` | Reports and terminates, where no return value could carry the failure |
| `WIN32_UNSUPPORTED(description, value)` | An entry point that is real for the cases the engine uses, naming a request it has no honest answer for |

Reporting is once per entry point, or once per description, rather than once per
call, so a stub inside a frame loop names itself without burying the log.

An entry point returns the Win32 failure value silently only where Windows would
return it too. `GetMenu` (`code/win32user.cpp:3108`) answers null because no
window here has a menu, not because nothing was written for it, and a page's
`LoadCursor` (`code/win32window.cpp:606`) answers null for a resource identifier
that names no system cursor, which is what a null module handle gets on Windows.
The test is whether Windows on the same input would answer the same way; where
it would not, the call keeps reporting, because a stub that stops saying so is
worse than no stub at all.

On the date above, 179 stub sites remained in `code/win32compat.cpp` and one in
`code/win32timer.cpp`, against 24 `WIN32_UNSUPPORTED` sites spread across the
implementation files -- there are eight of those, and six of them carry one. The
direction of both numbers is the measurement that matters, not their value on a
given day, and the split by subject has since moved stubs between the files
without changing how many there are.

### What is implemented rather than stubbed

Each of these says so where it is defined.

- **Dialog templates.** `CreateDialogIndirectParamA` (`code/win32user.cpp:2432`)
  builds a dialog and its controls out of the template, converting dialog units
  to pixels at `:2463` and `:2485`, over the window manager the rest of
  `code/win32user.cpp` supplies. `CreateDialogParam` and `DialogBoxParam` reach
  it. What each individual dialog then does is recorded in
  [Building OpenTS](BUILDING.md#what-has-been-run).
- **Message boxes.** `MessageBoxA` (`code/win32user.cpp:2940`) lays the question
  out in the page and waits on the engine's own yield, rather than calling
  `confirm()` and stopping the page; `MessageBoxIndirectA` (`:3069`) is the same
  box from a parameter block, which is how `Main_Game` reports a failed
  `Init_Game` (`code/conquer.cpp:408`). Naming a disc image the server does not
  carry, as in `?image=TS3.iso`, mounts nothing and is how that report is
  reached deliberately. Without the yield scaffold there is nothing to wait on,
  and both report the box cancelled instead.
- **The filesystem.** The Win32 file API over POSIX, with a case-insensitive
  fallback when the exact spelling is missing, and directory enumeration, so
  `FindFirstFile` answers. It also mounts an ISO 9660 volume, lazily, on the
  first name the host cannot answer for (`code/win32compat.cpp:691`); in a page
  that volume is served over HTTP range requests by `code/isohttp.cpp`.
- **COM activation.** `code/win32compat.cpp:2479`–`:2612` is an in-process class
  registry: `CoRegisterClassObject` publishes a factory, `CoCreateInstance` is a
  table lookup followed by `IClassFactory::CreateInstance`, and an activation
  that names an unregistered class prints the CLSID rather than handing back a
  null pointer. `RegisterClasses` (`code/startup.cpp:241`) publishes the same 67
  factories it publishes on Windows, the ten locomotors among them
  (`code/startup.cpp:328`–`:337`). This is what makes a unit constructible.
- **Resources.** `code/peresource.cpp` reads the shipped `Language.dll` as a
  data file and walks its PE resource directory, so `Fetch_String`,
  `Fetch_Resource`, and version reporting answer out of the same library the
  Win32 build loads (`code/data.cpp:404`).
- **The clocks and `Sleep`.** `GetTickCount`, `timeGetTime`,
  `QueryPerformanceCounter`, and `GetSystemTime` over the host's monotonic and
  realtime clocks, in `code/win32compat.cpp`; `Sleep` is a yielding wait when the
  scaffold is built in (`code/win32timer.cpp:217`). The clock is deliberately not
  in `code/win32timer.cpp` beside the timers that read it: `tests/timer`
  substitutes a `timeGetTime` of its own to step the timer wheel, which it can
  only do while the two are in different translation units.
- **Audio.** `code/audiobackend.cpp` carries a looping ring out to Web Audio
  through OpenAL's streaming queue and dresses it in the DirectSound secondary
  buffer contract, so `dsaudio.cpp` and `ahandle.cpp` consume it unchanged.
- **The page.** `code/browser.cpp` sizes the canvas, feeds keyboard and mouse
  events into the engine's own queues, reports page visibility, and hands the
  thread back to the page.
- Plus the rectangle arithmetic, `wsprintf` and the `lstr` helpers, the
  interlocked operations, which are exact on a single-threaded target, and
  everything in `crtcompat.h`.

### `__uuidof` and interface identity

`__uuidof` needs `-fms-extensions` and `__declspec(uuid)`, which this target does
not build with, so the identity `MIDL_INTERFACE` attaches to a type on Windows is
not available. Every interface the tree activates or queries for also declares
its identifier as `IID_<interface>` in the matching MIDL `_i.c` file, so
`code/win32compat.h:991` defines `__uuidof(type)` as that constant. An interface
without one fails to compile rather than resolving to the wrong identity.

Conversion between two interfaces the shim already holds pointers to uses
`dynamic_cast` (`code/win32compat.h:1138`), which answers the same question for
them. Activation still goes through the real virtual `QueryInterface`, which
`TClassFactory<T>::CreateInstance` calls on the object it just built
(`code/classfactory.h:103`).

## Type and ABI differences from Win32 x86

Emscripten's wasm32 is ILP32, which is why the engine's layouts survive the
move. Not everything matches, and the build settles the differences that matter:

| Property | MSVC Win32 x86 | Emscripten wasm32 | Settled by |
| --- | --- | --- | --- |
| `void *`, `long`, `size_t` | 4 bytes | 4 bytes | — |
| `char` signedness | signed | signed | — |
| `wchar_t` | 2 bytes | 4 bytes | `-fshort-wchar` (`code/CMakeLists.txt:155`) |
| Strict aliasing | not assumed | assumed | `-fno-strict-aliasing` (`:151`) |
| `long double` | 8 bytes | 16 bytes | Nothing. Nothing under `code/` declares one, so the difference is currently unreachable |

`size_t` is a difference that costs nothing in layout and something in source.
It is four bytes on both, but it is `unsigned int` under MSVC and
`unsigned long` under Emscripten, so a `std::min(unsigned, size_t)` that
deduced one type under MSVC deduces two here. The fix is an explicit template
argument at the call, not a change of representation.

WebAssembly arithmetic is strict IEEE-754 with no excess precision and no
reassociation, which is what `/arch:SSE2` and `/fp:precise` buy under MSVC, so
the deterministic simulation's floating-point model carries over by default. It
carries over only as long as no fast-math option is introduced, which is why
`-fno-fast-math` is passed explicitly (`code/CMakeLists.txt:147`).

`__declspec` is resolved by `-fdeclspec`, which the build passes, including the
`__declspec(property)` accessors in `abstract.h`, `rect.h`, `trigger.h`, and
`trigtype.h`. The four calling-convention keywords are erased on the command
line; the single-underscore spellings `_cdecl`, `_stdcall`, and `_fastcall` are
erased in `crtcompat.h`.

Exception handling is `-fwasm-exceptions` rather than `-fexceptions`, and the
reason is the yield scaffold rather than a preference: `-fexceptions` routes
every call that can unwind through an `invoke_` import, and Emscripten declares
every `invoke_` import suspending whenever JSPI is on, so a static initializer
would try to suspend outside the promising boundary and trap
(`code/CMakeLists.txt:157`–`:165`).

## What is still missing

- **Version and icon resources.** `Sun.rc` and `except.rc` are resource-compiler
  input, so the WebAssembly target does not build them
  (`code/CMakeLists.txt:72`). Nothing replaces them.
- **Saving and loading through the engine.** Neither half is missing any more.
  `StgCreateDocfile`, `StgOpenStorage`, and `StgIsStorageFile` answer out of
  `code/docfile.cpp` (`code/win32compat.cpp:3060`, `:3072`, `:3084`), and
  `tests/save` holds its writer and its reader to the layout Microsoft publishes
  as MS-CFB. [C.3](WASM-PORT.md#c3-com-and-what-it-means-for-the-save-format)
  sizes the decision behind it. A save also has somewhere to live: `/save` is
  mounted from IndexedDB and read back before the engine starts
  (`wasm/game.html:357`), and it stands in front of the game directory for any
  bare name, on an open (`code/win32compat.cpp:438`) and on a scan
  (`code/win32compat.cpp:1950`), with writes flushed by `FS.syncfs`
  (`code/win32compat.cpp:469`). What is missing is evidence: no save has been
  written or loaded through the engine in a page.
- **The mouse cursor.** `code/wincursor.cpp:180` routes the Emscripten build to
  `Win32_Window_Create_Cursor`, which encodes the frame as a PNG data URL for
  `canvas.style.cursor` (`code/win32window.cpp:526`). The pointer a player sees
  is still the browser's arrow, and `SetCursorPos` remains a stub
  (`code/win32window.cpp:719`), so the edge-scroll warp at `code/scroll.cpp:768`
  does nothing.
- **Networking.** Sockets are WebSocket-backed under Emscripten and carry
  neither raw IPX nor UDP, so network play does not work.
  [C.5](WASM-PORT.md#c5-networking) sizes it.
- **Westwood Online.** `code/wonline.cpp` is excluded from the target
  (`code/CMakeLists.txt:53`); `wonlinestub.cpp` supplies what the rest of the
  engine references.
- **The crash report.** `code/except.cpp` builds here, but its whole
  structured-exception, DbgHelp, and minidump half is compiled out
  (`code/except.cpp:79`) and a WebAssembly half keeps the entry points
  (`:1997`). A fault reaches the host as a wasm trap, and what reports it is the
  browser's or node's own JavaScript stack. The engine no longer writes a crash
  report on this target.
