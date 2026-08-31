# The WebAssembly path to the main menu

> [!IMPORTANT]
> **This is a record of an investigation, dated August 29, 2026, and the
> question it asked has since been answered.** The menu was reached the
> following day, and the game behind it runs; [Building OpenTS](BUILDING.md)
> owns what has been built and run. The page is kept because most of its
> analysis is still the best map of this part of the engine, and because the
> few things it got wrong are worth knowing. Where a later observation settled a
> question, [8](#8--what-happened) says so; the body below is left as it was
> written, apart from three corrections marked in place.

[WebAssembly port design](WASM-PORT.md) sizes the whole port.
[WebAssembly target status](wasm-compile-status.md) records the substitute the
port stands on. This page answered a narrower question at the time it was
written: between where the engine stopped and the first frame of the graphical
main menu, what was actually missing? It is a snapshot of a tree three
workstreams were editing at once; regenerate the line references rather than
trusting them.

## Contents

- [What was measured](#what-was-measured)
- [1 — The path](#1--the-path)
- [2 — Subsystem state along the path](#2--subsystem-state-along-the-path)
- [3 — What the menu needs loaded](#3--what-the-menu-needs-loaded)
- [4 — How the menu reaches the canvas](#4--how-the-menu-reaches-the-canvas)
- [5 — Input](#5--input)
- [6 — The verdict](#6--the-verdict)
- [7 — Corrections to the port design](#7--corrections-to-the-port-design)
- [8 — What happened](#8--what-happened)

## What was measured

| | |
| --- | --- |
| Date | August 29, 2026, 21:34 UTC |
| Tree | `a2ca2d4` on `wasm-port` plus uncommitted in-flight work from three concurrent workstreams |
| Toolchain | Emscripten 6.0.8, CMake 4.4.2, Ninja, node v26.7.0, macOS 26.5.1 |
| Configuration | `Debug`, JSPI on (`OPENTS_WASM_JSPI` defaults to `ON`, `code/CMakeLists.txt:345`), `NODERAWFS` on |
| Data | The user's staged Tiberian Sun install, 263 MB, 23 files |

```bash
source ~/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-menu -G Ninja -DCMAKE_BUILD_TYPE=Debug
ninja -C build-menu OpenTS
cd Run && node ../build-menu/bin/GameD.js
```

The build configures, compiles and links; the run reaches twenty-four distinct
unimplemented Win32 entry points and stops at:

```
OpenTS message box [Tiberian Sun]: Error - Unable to set the video mode.
```

**That stop is a property of node, not of the engine.** `Browser_Init`
(`code/browser.cpp:607`) asks the page for the size of `#canvas`; under node
there is no document, so it reports "the page has no canvas"
(`code/browser.cpp:624`) and leaves `_CanvasWidth` at zero.
`Video_Init` reads that value back (`code/video.cpp:208`) and returns false
because it is zero, and `WinMain` treats a false return as fatal
(`code/startup.cpp:594`). A page that lays a canvas out passes this test
without any engine change. Nothing was learned from the node run about whether
`Backend_Init` succeeds on a real WebGL 2 context; that belongs to the renderer
workstream, and `wasm/demo.cpp` is its evidence.

The consequence for this document is that everything from `Main_Game` onward is
static reading. Where a claim is a reading rather than an observation, it says
so.

## 1 — The path

```
main                       code/startup.cpp:723
 └─ Browser_Init           code/startup.cpp:737
 └─ WinMain                code/startup.cpp:376
     ├─ Init_Language_Resources        (peresource.cpp reads the shipped DLL)
     ├─ Create_Main_Window             code/startup.cpp:588   — all stubs
     ├─ Audio.Init                     code/startup.cpp:592
     ├─ Video_Init                     code/startup.cpp:594   ← stops here under node
     ├─ DSurface::Create_Primary       code/startup.cpp:599
     ├─ Allocate_Surfaces              code/startup.cpp:621
     ├─ Browser_Create_Mouse           code/startup.cpp:633
     └─ Main_Game                      code/startup.cpp:663
         ├─ Init_Game                  code/init.cpp:297
         └─ while (Select_Game(fade))  code/conquer.cpp:366
             └─ Select_Game            code/init.cpp:1035
                 └─ New_Main_Menu      code/init.cpp:1147 → :5889
                     └─ Process_Game_Select    code/newmenu.cpp:167
                         └─ Game_Select_Loop   code/newmenu.cpp:96
                             └─ Display_Menu   code/newmenu.cpp:199
                                 ├─ Do_Graphic_Menu    code/grphmenu.cpp:39
                                 └─ GraphicMenu::Presentation  code/grphmenu.cpp:163
```

### `Init_Game` (`code/init.cpp:297`)

The ordered list, with what each step requires. A `return(-1)` from any of them
ends the run: `Main_Game` puts up a message box and returns
(`code/conquer.cpp:341`–`:356`), and the message box is itself a stub.

| Step | Line | Requires |
| --- | --- | --- |
| `Bootstrap` | `:339` | `GameInFocus`; `TIBSUN.MIX`; `CACHE.MIX` cached; `LOCAL.MIX`; six `.PAL` files; `voxels.vpl`; the six `.FNT` files behind `Init_Fonts` |
| `Init_Mouse` | `:352` | `MOUSE.SHP`; builds a cursor that cannot be built (see [4](#4--how-the-menu-reaches-the-canvas)) |
| `Init_Secondary_Mixfiles` | `:359` | `CONQUER.MIX`, `MAPS*.MIX`, `MULTI.MIX`, `SOUNDS.MIX`, `SCORES.MIX`, **`MOVIES*.MIX`** |
| `Init_Campaigns` | `:367` | `BATTLE*.INI` — a `FindFirstFile` scan, real on this target |
| `Init_Heaps`, `Init_Threads` | `:373`, `:376` | Nothing. `Init_Threads` is empty (`code/init.cpp:5333`) |
| `Options.Load_Settings` | `:383` | The config INI |
| `Anim_Init` | `:389` | Mixfile data |
| `Play_Movie("WWLOGO.VQA")` | `:400` | Skipped when the file is absent; otherwise blocks without yielding |
| `Draw_Menu_Background` | `:409` | `Title.PCX` or `Loading.PCX` |
| `Init_Bulk_Data` | `:442` | `CONQUER.MIX` cached, `SOUNDS*.MIX` cached when audio is up, `TUTORIAL.INI`, `DPOD.VXL`, `DPOD.HVA` |
| `SOUND.INI`, `THEME.INI` | `:455`, `:475` | Both mandatory; a missing one returns −1 |
| `Init_Rules` | `:505` | `RULES.INI`, `ART.INI`; opens a Win32 dialog if two or more loose `RULE*.INI` exist (`code/init.cpp:957`) |
| `Init_Commands` | `:544` | Mixfile data |

`Get_New_Menu()` (`code/newmenu.cpp:31`) is a function-local static, first
constructed at `code/init.cpp:401`, after the archives are mounted. Its
constructor decides the whole shape of what follows: it looks for `GMENU.MIX`
(`code/newmenu.cpp:61`) and leaves `MixFile` null if it is absent.

### `Select_Game` (`code/init.cpp:1035`)

```cpp
if (Get_New_Menu()->MixFile) {
    selection = New_Main_Menu();      // code/init.cpp:1147
} else {
    selection = Main_Menu(ATTRACT_MODE_TIMEOUT);   // :1149
}
```

**The fallback is not a fallback on this target.** `Main_Menu`
(`code/init.cpp:3324`) opens an owner-draw Win32 dialog with
`OwnerDraw::Begin_Dialog`, asserts it is non-null, and drives it with
`OwnerDraw::Dialog_Message_Handler`. On WebAssembly `Begin_Dialog` returns null,
the `assert` fires in a Debug build, and a Release build takes the `else` branch
and returns `SEL_EXIT`, ending the game. So `GMENU.MIX` is not a nicety: without
it there is no menu at all, only an exit.

The same gate exists one level down. `Display_Menu` returns `NSEL_OLD_MENU` when
`Do_Graphic_Menu` cannot read `NewMenu.INI` (`code/newmenu.cpp:203`), and
`New_Main_Menu` turns that into the same `Main_Menu` call
(`code/init.cpp:5898`).

### `GraphicMenu::Presentation` (`code/grphmenu.cpp:163`)

The menu loop itself, and it is short:

1. `Theme.Play_Song` (`:165`) — silent without audio, not blocking.
2. `OwnerDraw::Capture_Mouse` (`:167`) — releases the game's mouse; see
   [4](#4--how-the-menu-reaches-the-canvas).
3. `HiddenSurface->Fill(0)`, `AlternateSurface->Fill(0)` (`:169`–`:170`).
4. `Engine.Restore_Anims` / `Restore_And_Advance` (`:181`–`:182`).
5. A `while (!done)` loop that reads `Keyboard->Check()` / `Get()`, hit-tests
   the item under `Get_Mouse_X()` / `Get_Mouse_Y()`, and calls
   `Engine.Wait_Delay(1)` (`:220`).
6. `item->Action(&Engine)` and `Theme.Fade_Out()`.

`Engine.Wait_Delay` (`code/msengine.cpp:362`) is the frame: `Call_Back()`,
`Advance(HiddenSurface)`, `Blit_All(HiddenSurface)`,
`Windows_Message_Handler()`, `Sleep(0)`. Both `Call_Back`
(`code/conquer.cpp:516`–`:527`) and `Windows_Message_Handler`
(`code/msgloop.cpp:98`–`:113`) already hand the thread back on this target, and
`Sleep` is an inert stub. *(Corrected: `Sleep` moved to
`code/win32timer.cpp:217` and is a yielding wait when the scaffold is built in,
which strengthens rather than changes the conclusion.)* **The menu's own loop
needs no flattening.** It is already a per-frame loop that services the page.

## 2 — Subsystem state along the path

Every unimplemented entry point in `code/win32compat.cpp` reports itself once
(`code/win32compat.h:44`–`:62`). Cross-referencing the 269 stub definitions
against the files on this path gives the following. `if` matches are regex noise
and are excluded.

| File | Stubs it can reach | Assessment |
| --- | --- | --- |
| `newmenu.cpp`, `grphmenu.cpp`, `grphmitm.cpp`, `mschoice.cpp`, `msanim.cpp`, `movies.cpp`, `movie.cpp`, `theme.cpp` | none | The graphical menu is portable as written. |
| `mixfile.cpp`, `cdfile.cpp`, `rawfile.cpp` | none | The archive and file layer sits on the real POSIX-backed half of the shim. |
| `msengine.cpp` | `Sleep` ×2 | Inert. `Sleep(0)` at `:380` and `Sleep(500)` at `:409` are both inside loops that also call `Windows_Message_Handler`. |
| `dsurface.cpp` | GDI | The allocation path is already `#if defined(__EMSCRIPTEN__)` (`:114`, `:193`); what is left is the stretch path, [below](#4--how-the-menu-reaches-the-canvas). |
| `vqa.cpp` | `TranslateMessage`, `Sleep` | The raw pump at `:47`, called from `Play_VQA` at `:523`. Inert, and therefore silently non-yielding. |
| `ahandle.cpp` | `timeSetEvent`, 24 critical-section calls | Movie audio. Fails cleanly: `Open_Audio_Handler` returns `VQAERR_AUDIO` when `Audio_Available()` is false (`code/ahandle.cpp:198`), and the VQA falls back to the wall clock. |
| `wwmouse.cpp` | `ShowCursor`, `ClipCursor`, `GetCursorPos`, `GetClientRect`, `ClientToScreen` | `Browser_Create_Mouse` overrides only the three position accessors (`code/browser.cpp:558`–`:566`). |
| `wincursor.cpp` | `CreateDIBSection`, `CreateBitmap`, `CreateIconIndirect`, `SetCursor`, `DestroyCursor` | **Total.** The cursor cannot be built at all. *(Corrected: `code/wincursor.cpp:180` now routes the Emscripten build to `Win32_Window_Create_Cursor`, which encodes the frame as a PNG data URL for `canvas.style.cursor` (`code/win32window.cpp:541`). Whether a player sees it is a separate question — see [8](#8--what-happened).)* |
| `keyboard.cpp` | `GetKeyState`, `ToAscii`, `GetAsyncKeyState` | All three already have `__EMSCRIPTEN__` branches (`:230`, `:321`, `:399`). |
| `winstub.cpp`, `ownrdraw.cpp`, `loaddlg.cpp`, `options.cpp`, dialog procs in `init.cpp` | many | Win32 front end. Only two of `ownrdraw.cpp`'s functions are on the menu path, and both are portable. |
| `dsaudio.cpp` | `timeSetEvent`, `timeGetDevCaps`, critical sections | Owned by the audio backend workstream. |

Two things the stub census does not show, both confirmed by reading:

- **`OwnerDraw` is not a menu dependency.** `GraphicMenu::Presentation` calls
  `OwnerDraw::Capture_Mouse` (`code/ownrdraw.cpp:6681`) and `Release_Mouse`
  (`:6699`). Both touch only `MouseCursor` and a file-static counter; neither
  contains an `HWND`. The 7,001-line control toolkit is needed for everything
  *behind* the menu — campaign choice, skirmish setup, options, load — and for
  nothing on the way to it. The parent framing is correct.
- **The blocking-wait analysis does not apply to this path.** Of the sixty-odd
  load-bearing waits [Part A](WASM-PORT.md#a2-load-bearing-versus-incidental)
  counts, the menu path reaches exactly two kinds: `Engine.Wait_Delay`, which
  already services and yields, and the VQA player, which does not.

## 3 — What the menu needs loaded

### The hard requirement nobody expects

`Init_Secondary_Mixfiles` scans for `MOVIES*.MIX` (`code/init.cpp:2706`) and
then:

```cpp
if (MoviesMix == NULL) {
    return(false);
}
```

at `code/init.cpp:2727`. That propagates to `Init_Game` returning −1, and the
game never reaches `Select_Game`. **The movie archives are mandatory to reach
the main menu.**

Two facts make this worse than it first looks:

1. The scan is `CDFileClass::Find_First_File` (`code/cdfile.cpp:657`), which
   consults the filesystem and any mounted ISO image and **never the mixfile
   catalog**. A `MOVIES01.MIX` nested inside another archive cannot satisfy it.
   The same is true of the `MAPS*.MIX` scan at `code/init.cpp:2618`, whose null
   check at `:2641` is equally fatal.
2. The staged install this was measured against has no `MOVIES*.MIX`. Its
   twenty-three files are `TIBSUN.MIX`, `MULTI.MIX`, `MAPS01`–`03.MIX`,
   `SCORES.MIX`, `SCORES01.MIX`, `EXPAND01.MIX`, `E01SCD01`/`02`,
   `SIDECD01`/`02`, `WDT.MIX`, `WDTVOX.MIX`, `LANGUAGE.DLL`, four `.TLB` files
   and the build output. So even with video working, **today's node run cannot
   reach the menu with today's data.** This is a data problem, not a port
   problem, but it will be indistinguishable from one when it happens.

### The rest of the list

Required as loose files or image entries, because the code scans the filesystem
for them: `TIBSUN.MIX` (`code/init.cpp:2547`, in `Init_Bootstrap_Mixfiles`),
`MAPS*.MIX`, `MOVIES*.MIX`, `EXPAND%02d.MIX` (`code/init.cpp:2456`, a
`RawFileClass` probe), `PATCH.MIX` (`:2493`).

Required, but resolvable from inside an archive because they go through
`CCFileClass`: `CACHE.MIX`, `LOCAL.MIX`, `CONQUER.MIX`, `MULTI.MIX`,
`SOUNDS.MIX`, `SCORES.MIX`, plus the content `Bootstrap` and `Init_Fonts`
retrieve — `UNITSNO.PAL`, `TEMPERAT.PAL`, `WAYPOINT.PAL`, `ANIM.PAL`,
`PALETTE.PAL`, `CAMEO.PAL`, `MOUSEPAL.PAL`, `voxels.vpl`, `MOUSE.SHP`,
`12METFNT.FNT`, `KIA6PT.FNT`, `6POINT.FNT`, `EDITFNT.FNT`, `8POINT.FNT`,
`GRAD6FNT.FNT` — and `SOUND.INI`, `THEME.INI`, `RULES.INI`, `ART.INI`,
`TUTORIAL.INI`, `DPOD.VXL`, `DPOD.HVA`.

The menu's own data: `GMENU.MIX` (`code/newmenu.cpp:61`), `NewMenu.INI`
(`code/newmenu.cpp:201`), the `Background` PCX each section names, and the
backdrop the section's `Background` key selects — `_Graphic_Menu` appends
`.VQA` and prefers a movie, falling back to a PCX
(`code/grphmenu.cpp:76`–`:84`).

`NewMenu.INI` is present in this install: `EXPAND01.MIX` contains the literal
`TiberianSunMenu`, which is the section name `Display_Tiberian_Sun_Menu` asks
for (`code/newmenu.cpp:289`). Whether `GMENU.MIX` is reachable was **not
established** — the mixfile headers are Blowfish-encrypted, so the archives
could not be listed without running the engine, and the engine stops first. It
is the single cheapest thing to confirm once video works, because the answer
decides whether the menu exists at all.

### Caching and heap

`MFCD::Cache("CACHE.MIX")` (`code/init.cpp:2564`) and `ConquerMix->Cache()`
(`code/init.cpp:2989`) read whole archives into memory before the menu appears,
and `SOUNDS*.MIX` follows when audio is up. The engine links with
`-sALLOW_MEMORY_GROWTH=1` (`code/CMakeLists.txt:322`) so there is no fixed
ceiling to trip over, but the resident footprint at the menu has not been
measured and should be, before any `INITIAL_MEMORY` or `MAXIMUM_MEMORY` is
chosen.

## 4 — How the menu reaches the canvas

The chain is entirely portable and already ends at the right place:

```
MSAnim::Advance / Redraw   →  HiddenSurface        (code/msengine.cpp:177)
MSEngine::Blit_All         →  VisibleSurface       (code/msengine.cpp:321-331)
Video_Present_If_Dirty     →  Video_Present        (code/video.cpp:370, :342)
Backend_Present(565 pixels)                        (code/video.cpp:356)
```

`DSurface` no longer needs GDI to hold pixels: the WebAssembly branch allocates
the 565 buffer directly and reports `Is_GDI_Backed()` as false
(`code/dsurface.cpp:114`, `code/dsurface.h:122`). `Allocate_Surfaces`
(`code/init.cpp:5244`) builds all five surfaces from that constructor.

Three things in this chain are not right yet.

### 4.1 A scaled blit crops instead of stretching

`DSurface::Blit_From` (`code/dsurface.cpp:492`) sends a blit to the software
blitter when it is transparent, when the source is not GDI-backed, or when the
rectangles are the same size (`:502`); otherwise it uses GDI `StretchBlt`
(`:529`). On this target `Is_GDI_Backed()` is always false, so every blit goes
to the software path — and the software path does not scale. `Bit_Blit` copies
`std::min(srect.Height, drect.Height)` rows of `srect.Width` pixels
(`code/blit.cpp:342`, `:576`). A blit whose destination is larger than its
source therefore produces a 1:1 copy in the corner, not a stretch.

`DSurface::AllowStretchBlits` (`code/dsurface.h:133`) is still initialized to
`true` (`code/dsurface.cpp:79`), so `Play_Movie` will still compute a stretched
rectangle (`code/movie.cpp:104`–`:112`) that the blitter cannot honor.

> **Corrected.** The two paragraphs above were true when this was written and
> are not now. Scaling no longer depends on GDI or on a flag: `XSurface::Blit_From`
> dispatches to `XSurface::Blit_Scaled` whenever the destination rectangle
> differs in size from the source (`code/xsurface.cpp:1281`), so every surface
> scales. `AllowStretchBlits` and the `StretchBlt` path were removed, and the
> stretched rectangle `Play_Movie` computes (`code/movie.cpp:104`–`:117`) is
> honored.

This does **not** affect the menu's own drawing: `MSVQAnim` forces both its
rectangles to a centered 640×400 (`code/msanim.cpp:604`–`:605`),
`Load_Title_Screen` blits a PCX at its own size (`code/winstub.cpp:558`), and
`Update_Visible_Surface` clamps the source to the destination when the tactical
map is not zoomed (`code/gscreen.cpp:543`–`:548`). It affects stretched
fullscreen movies, and it is a latent trap for everything after the menu.

### 4.2 There is no cursor

> **Corrected.** The first paragraph below was true when this was written and is
> not now: `Build_Cursor` takes an Emscripten branch (`code/wincursor.cpp:180`)
> that builds a CSS cursor instead of an `HCURSOR`. Everything after it — the
> `Capture_Mouse` interaction, and the observation that a canvas has no OS
> pointer to hand over to — still describes the code, and the pointer a player
> sees is still the browser's arrow. See [8](#8--what-happened).

This is the finding most likely to be assumed away. OpenTS no longer draws the
mouse pointer into the frame — `code/wincursor.cpp` converts a `ShapeSet` frame
into a real `HCURSOR` and hands it to `SetCursor`. `Build_Cursor`
(`code/wincursor.cpp:81`) calls `CreateDIBSection`, `CreateBitmap` and
`CreateIconIndirect` (`:167`), all three stubs, so it returns null and every
cursor the game asks for is null.

Independently, `GraphicMenu::Presentation` calls `OwnerDraw::Capture_Mouse`
(`code/grphmenu.cpp:167`), which calls `MouseCursor->Release_Mouse()`
(`code/ownrdraw.cpp:6685`). After that `Is_Captured()` is false, and
`WWMouseClass::Show_Mouse` / `Hide_Mouse` take the branch that calls only
`ShowCursor` (`code/wwmouse.cpp:184`, `:211`) — never
`Win_Cursor_Set_Visible`. On Windows this is correct, because the OS pointer
takes over while a dialog owns the mouse. On a canvas there is no OS pointer to
take over.

So for the whole duration of the graphical main menu the engine draws no
pointer of any kind. The player is left with the browser's own arrow, which the
shipped page does not suppress (`wasm/game.html:34` sets no `cursor` property)
and which is at the right place, because `BrowserMouseClass` reads the position
from the DOM event (`code/browser.cpp:563`). **The menu is therefore usable
without a cursor fix, and looks wrong.** That distinction matters for
sequencing: it is not a blocker, and it is not cosmetic either — in game, the
pointer carries the command (attack, deploy, sell) and its absence is a
functional loss.

`ShowCursor` returning 0 rather than a negative number
(`code/win32compat.cpp:2241`) is load-bearing in a way worth recording: three
`while (Get_Mouse_State() < 0) Show_Mouse();` loops on the startup path
(`code/init.cpp:2955`, `:2963`) would otherwise never end.

### 4.3 Presentation is gated on the yield scaffold

`Video_Present_If_Dirty` presents at most once per `Browser_Frame_Serial()`
(`code/video.cpp:382`–`:385`), and `_FrameSerial` is only incremented inside
`Browser_Yield`, inside `#if defined(OPENTS_WASM_JSPI)`
(`code/browser.cpp:435`). With `OPENTS_WASM_JSPI=OFF` the serial never moves, so
after the first present nothing is ever put on the canvas again. That is
consistent with the option's stated meaning — a build without the scaffold
"stops responding the moment it reaches a wait" (`code/CMakeLists.txt:342`) —
but it makes the coupling total rather than partial, and it is worth knowing
before someone turns the option off to see what happens.

## 5 — Input

The menu reads input in three places, and all three already work:

- `Keyboard->Check()` (`code/grphmenu.cpp:191`) calls
  `Fill_Buffer_From_System` (`code/keyboard.cpp:562`), which calls
  `Windows_Message_Handler` (`:565`), which calls `Browser_Service`
  (`code/msgloop.cpp:110`). `Browser_Service` drains the page's event queue into
  `Post_Key_Event` / `Post_Mouse_Event` (`code/browser.cpp:406`–`:418`,
  `code/keyboard.cpp:838`, `:852`).
- `Keyboard->Get()` returns `KN_LMOUSE` for a click and the item hit-test runs
  against it (`code/grphmenu.cpp:193`).
- `Get_Mouse_X()` / `Get_Mouse_Y()` resolve through `BrowserMouseClass`
  (`code/browser.cpp:563`–`:565`), which reads the position the DOM event
  reported, already converted from CSS pixels to game coordinates
  (`code/browser.cpp:254`–`:267`).

So `Process_Game_Select` needs nothing new to respond to a click or a key. Two
caveats:

- Keyboard shortcuts on menu items go through `Is_Input_Key`, which compares
  against `KeyNumType` values built from the browser's `code` property
  (`code/browser.cpp:160`). Physical-key identity is preserved; the character a
  key produces is recorded separately from the event's `key` property
  (`code/browser.cpp:282`–`:288`). This is the substitution
  [Part B](WASM-PORT.md#b2-seams-that-do-not-exist-at-all) flagged as a real
  behavior boundary, and the menu is a mild test of it: menu accelerators are
  letters and `Escape`.
- `Bootstrap` opens with `do { Keyboard->Check(); } while (!GameInFocus);`
  (`code/init.cpp:2759`–`:2761`). It services and yields through `Check`, and
  `Browser_Service` sets `GameInFocus` from page visibility
  (`code/browser.cpp:404`), so a tab that starts hidden parks here until it is
  shown rather than hanging. That is correct behavior and worth not
  "fixing" away.

## 6 — The verdict

Ordered by what has to happen first. "Blocking" means the menu does not appear
until it is done.

| # | Item | Size | Blocking? | Confidence |
| --- | --- | --- | --- | --- |
| 1 | `Video_Init` on a real canvas — `Backend_Init` against WebGL 2 | Owned by the renderer workstream | **Yes** | High that it is required; medium that it already works, since only `wasm/demo.cpp` has been shown to render |
| 2 | `MOVIES*.MIX` present as a real file or image entry, or the hard `return(false)` at `code/init.cpp:2727` relaxed | Hours to relax; a data decision to keep | **Yes** | High — read directly, and reproduced by the absence in the staged install |
| 3 | `GMENU.MIX` reachable through the mounted archives | Unknown until checked | **Yes** | Low. Not established. If absent, the only path is `Main_Menu`, which is pure Win32, and the target moves from "one week" to "the whole `ownrdraw` shim" |
| 4 | The data mount answering directory scans — `MOVIES*`, `MAPS*`, `EXPAND%02d`, `BATTLE*.INI`, `RULE*.INI` | Owned by the data workstream | **Yes** | Medium-high. `CDFileClass::Find_First_File` already has an ISO-image branch (`code/cdfile.cpp:682`–`:704`); nothing else does |
| 5 | Audio far enough that `Audio_Available()` answers honestly | Owned by the audio workstream | No | High. Every audio call on this path is guarded; a false answer costs music and click sounds, nothing else |
| 6 | `VQAClass::Play_VQA` yielding — replace `VQA_Message_Handler` (`code/vqa.cpp:47`, called at `:523`) with `Windows_Message_Handler` | A day | No, unless a title movie is present | Medium-high. Without it the page freezes for the length of `WWLOGO.VQA`, `TS_Title.VQA` or `FS_Title.VQA` and shows nothing while it plays |
| 7 | A cursor — either a browser-side pointer that `Win_Cursor_Set` can drive (a data URL on `canvas.style.cursor` is the obvious form), or restoring a software-drawn one | A few days | No | High that it is missing; medium on which replacement is right |
| 8 | `DSurface::AllowStretchBlits` false on this target, or a scaling software blit | Hours for the flag; a week for real scaling | No | High. Reading only; no scaled blit was observed |
| 9 | Heap footprint at the menu measured | An hour | No | High that it should be done; unknown what it will say |

### Needed to see the menu

Items 1–4, plus the two inert-but-visible defects, 6 and 7, if the menu is to
look right rather than merely exist. Nothing on that list is a rewrite. The
graphical menu, its animation engine, its item factory, the PCX and VQA
decoders, the archive layer and the input path are all already portable, and the
menu's own loop already returns to the page every frame.

### Needed to play a game

Everything [Part C](WASM-PORT.md#part-c--remaining-subsystem-work) sizes, and in
particular `code/ownrdraw.cpp`'s 7,001-line control toolkit, because the first
click on any menu item lands in it:

- `NSEL_START_NEW_GAME` → `SEL_CAMPAIGN_GAME` → `Choose_Campaign`, a Win32
  dialog (`code/init.cpp:1182`).
- `NSEL_LOAD_MISSION` → `SEL_LOAD_GAME` → `LoadOptionsClass::Load`
  (`code/init.cpp:1235`), a Win32 dialog over an OLE compound file.
- `NSEL_SKIRMISH`, `NSEL_LAN`, `NSEL_INTERNET` → the lobby dialogs.
- Options, and the whole in-game dialog set behind `SpecialDialog`.

So the menu is reachable across a gap that the shim does not have to close, and
nothing behind it is. That is the shape of the remaining work, and it is why
reaching the menu is a genuine milestone rather than an arbitrary one.

### The biggest risk

**Item 3.** Everything in this document assumes the graphical menu is the menu.
If `GMENU.MIX` is not reachable in a given install, `Select_Game` falls through
to `Main_Menu` (`code/init.cpp:1149`), which is an owner-draw Win32 dialog end
to end, and there is no third path. That turns "the menu" from a week of
polishing an already-portable subsystem into the full
[C.1 phase 1](WASM-PORT.md#c1-the-win32-front-end) shim — the 2–4 month item.
It is also the cheapest thing on this list to settle: one `DebugString` after
`code/newmenu.cpp:61`, once video works.

## 7 — Corrections to the port design

Three statements in [WebAssembly port design](WASM-PORT.md) were contradicted by
the code as it stood, and were recorded here because that document was being
revised concurrently. They have since been carried into it — C.9's movie probe,
B.1's cursor seam, and B.3's third GDI site — so that document owns them now.

## 8 — What happened

The menu was reached on August 30, 2026, and the game behind it runs.
[Building OpenTS](BUILDING.md#what-has-been-run) records the observations. This
section closes out the verdict above rather than restating them.

| # | Item | Outcome |
| --- | --- | --- |
| 1 | `Video_Init` on a real canvas | Done. A page that lays a canvas out passes the test, as predicted. |
| 2 | `MOVIES*.MIX` present | Unchanged in the code. `code/init.cpp:2727` still returns false, so the archives remain mandatory; the install this was measured against has since acquired them. |
| 3 | `GMENU.MIX` reachable | Answered by observation: the graphical menu renders, so it is reachable and the `Main_Menu` fallback never runs. **This was the biggest risk in the document, and it did not happen.** |
| 4 | The data mount answering directory scans | Done, on two paths: the host filesystem under node, and an ISO volume, which in a page is served over HTTP range requests (`code/isohttp.cpp`). |
| 5 | Audio answering honestly | Superseded. `code/audiobackend.cpp` is a real OpenAL-over-Web-Audio backend rather than an honest refusal. Whether anything is audible is not established. |
| 6 | `Play_VQA` yielding | Done. `VQA_Message_Handler` takes an Emscripten branch that services the page and yields (`code/vqa.cpp:53`–`:69`), and movies have been observed playing. |
| 7 | A cursor | Attempted, not settled. The prediction that the answer was a data URL on `canvas.style.cursor` was right (`code/win32window.cpp:541`), but what a player sees is still the browser's arrow. |
| 8 | Scaled blits | Done, and not by either route predicted. Rather than clearing the flag or confining scaling to GDI, `XSurface::Blit_From` now dispatches to a nearest-neighbour `Blit_Scaled` whenever the rectangles differ in size (`code/xsurface.cpp:1281`), so every surface scales and `AllowStretchBlits` was removed. The estimate of a week for real scaling was too high. |
| 9 | Heap footprint at the menu | Still not measured. |

Two judgements in [6](#6--the-verdict) are worth marking as having held. The
graphical menu really was portable as written — nothing in `newmenu.cpp`,
`grphmenu.cpp`, `msanim.cpp`, or `theme.cpp` needed changing — and the menu's
own loop really did need no flattening.

One framing was too optimistic. "Needed to play a game: everything Part C sizes,
and in particular `code/ownrdraw.cpp`'s 7,001-line control toolkit, because the
first click on any menu item lands in it" is the right shape but the wrong
conclusion about the toolkit: campaign missions can be started without it,
because a command-line switch selects a scenario directly and skips the chooser.
What the toolkit still gates is everything reached by a dialog — options,
skirmish setup, the lobby, and save and load.
