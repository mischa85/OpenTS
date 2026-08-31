# Porting OpenTS to macOS and Linux

This document owns the native macOS/Linux port: the strategy, the staged
plan, and the current status. [Building OpenTS](BUILDING.md) remains the
authority for what is supported today; nothing here is a support claim until
BUILDING.md says so.

## Strategy

The port replaces Windows subsystems behind the seams the tree already has,
stage by stage, keeping the Visual Studio Win32 build working throughout.
Three findings shape the plan:

- **Save compatibility costs nothing.** Saves are stamped with the packed
  project version and every earlier stamp is refused outright, so the OLE
  structured-storage save backend can be replaced with a portable container
  instead of emulated. A 64-bit build refusing 32-bit saves is the existing
  policy, not a new break.
- **The renderer is already portable.** bgfx builds natively on macOS
  (Metal) and Linux (Vulkan/OpenGL); the engine touches it only through
  `code/bgfxbackend.cpp`, whose interface leaks a single `HWND`.
- **The port is de-Windowsing, not de-32-bitting.** Targets travel as IDs,
  not pointers; the network wire format and the per-member save serializer
  are pointer-width-clean apart from a short, known list of LP64 breaks.

Cross-platform multiplayer and replay compatibility additionally require
bit-identical floating point across compilers and libms (transcendentals in
the sim paths); that is deliberately out of scope until the single-platform
port is playable, and same-platform play is unaffected.

## Stages

1. **Build scaffolding.** CMake configures and builds on non-Windows
   toolchains: thirdparty bgfx, the test suite, and a growing
   `OpenTSPortable` source set. The engine executable stays Windows-only
   until its dependencies are ported. MASM and MSVC flags become
   conditional.
2. **Foundation.** Debug logging, raw file I/O, the game-directory layer,
   timing, and the MSVC CRT-isms (`always.h` name macros, `%I` format
   lengths, `stricmp` family) get POSIX equivalents; the existing CTest
   suite runs green on macOS/Linux.
3. **Codecs.** C++ replacements for the six x86 MASM files and the
   MSVC-only inline-assembly `LCW_Comp`: the SOS ADPCM decoder (one
   algorithm, three signatures), the remaining `UnVQ` block decoders,
   `AudioUnzap`, and the palette adjust/brighten tables. The voxel
   rasterizer table flips to the existing C rows off-MSVC; the blitter
   template specializations already fall back to their generic bodies.
4. **Object model and saves.** The COM dependency (`IPersistStream` on
   `AbstractClass`, `CoRegisterClassObject`, `OleSave/LoadFromStream`,
   structured storage, property sets) is replaced by an in-tree CLSID
   factory registry, a plain stream interface, and a portable save
   container. This intentionally changes the save container format; the
   version stamp already gates loading.
5. **Platform backend.** SDL window/event/audio backend behind the existing
   seams (`Create_Main_Window`, the message pump, `WWKeyboardClass`
   translation, `dsaudio`/`ahandle`), BSD-socket networking behind
   `WinsockInterfaceClass` (the `WSAAsyncSelect` path becomes a poll
   drain), a POSIX crash reporter replacing `except.cpp`, and the
   `Language.dll` string/dialog resources transcoded to a data file behind
   `Fetch_String`/`Fetch_Resource`.
6. **Dialog layer.** The Win32 resource dialogs (`ownrdraw.cpp`,
   `windlg.cpp`, the shell and network screens) move onto a portable
   dialog host: a `DLGTEMPLATE` interpreter over the owner-draw path, or a
   rebuild on the in-game gadget system. Largest single item; last because
   everything else can be proven under the Windows build first.

## Adopted work

Stage 3 came from tinix0's translate-assembly-to-cpp and win64-port branches, with
golden parity data captured against the original assembly. Stage 4 and the
win32compat/crtcompat/docfile/peresource/iso9660 layers came from gunnarbeutner's
wasm-port branch, widened from the WebAssembly target to every non-Windows target and
made LP64-clean. The merge history records the authorship.

## Native host

`code/sdlhost.cpp` answers the same host contract `code/browser.h` states for the
WebAssembly target -- window, input drain, yield trio, cursor -- over SDL2, and supplies
`main`. `code/hostyield.cpp` is the windowless default the test harnesses link. The
`OpenTSNative` target (`bin/opents`) builds when SDL2 and Python are found; it is
experimental and unsupported. Run it from a directory holding the game data.

Runtime fixes the native boot surfaced, all latent in the inherited code: the straw
chain in the mixfile header reader destroyed its segments in an order that read a dead
neighbor, base64 decoding trusted the BSD `BIG_ENDIAN` macro that is defined on
little-endian hosts too, the SHA-1 digest was declared as five `long` words, and the
mixfile name hash uppercased caller strings in place, string literals included.

## Known LP64 breaks

The short list of genuine 64-bit breaks found by audit, fixed as their
subsystems are touched:

- `zbuffer.h`/`abuffer.h` hold heap addresses in `unsigned int`
  (ring-buffer wrap arithmetic, ~151 call sites).
- `radio.h` `Transmit_Message` smuggles pointers through `int & param`
  (~10 sites).
- `taction.h` serializes a union containing `long Value` as its raw image.
- `vqalib`/`soscomp.h` on-disk headers declare fields `long`.
- `isotype.cpp` `IsoBlitState` holds addresses in `int` (transient only).

## Status

| Stage | State |
| --- | --- |
| 1. Build scaffolding | done — non-Windows configures; bgfx and the tests build |
| 2. Foundation | done — CTest green on macOS arm64 and x86_64; Linux expected but unverified |
| 3. Codecs | done — merged from tinix0/translate-assembly-to-cpp and win64-port; golden parity tests green on macOS |
| 4. Object model and saves | done — gunnarbeutner/wasm-port's COM registry, CFBF docfile, and PE resource layers adopted; com/save/resources tests green natively |
| 5. Platform backend | in progress — the SDL host runs the engine natively on macOS arm64: it boots from freeware game data into a campaign mission, with mouse positions mapped through the frame scaling, rendering through bgfx/Metal, and sound through the shared OpenAL backend over openal-soft; Linux is still open |
| 6. Dialog layer | in progress — the Win32 substitute's window manager and dialog templates carry the shell and the owner-draw dialogs natively; the campaign flow runs through them into the game proper |
