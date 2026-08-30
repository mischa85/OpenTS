# WebAssembly port design

> [!IMPORTANT]
> **This document records the design the port was started from, dated August
> 2026. The port has since been built and run, and parts of this design were
> wrong.** It is kept, and corrected in place rather than rewritten, because a
> plan that was tried and did not work is more useful stated than quietly
> edited to match what happened. [Outcome](#outcome) is the summary;
> corrections appear against the sections they belong to and are marked as such.
> [Building OpenTS](BUILDING.md) remains the authority on what is supported and
> on what has been run. Every measurement below is a static count taken from the
> tree as it then stood; a count is not a runtime observation, and the counts
> have moved.

## Outcome

The engine builds, links, and runs in a browser: the main menu, a campaign
mission played from the player's own disc image, unit movement and selection,
building placement, terrain, radar, sidebar, and movies.
[Building OpenTS](BUILDING.md#what-has-been-run) records what was observed and,
just as importantly, what was not.

Four things in this design turned out differently.

- **[A.5](#a5-recommendation)'s recommendation could not be implemented as
  written.** It proposed standing up `emscripten_set_main_loop` *and* enabling
  the JSPI scaffold. Those two are mutually exclusive, and that was established
  by experiment rather than by reading. What was built instead keeps the
  engine's own stack and yields from inside it. See the correction in
  [A.5](#a5-recommendation) and [A.6](#a6-migration-sequence).
- **The premise of [C.3](#c3-com-and-what-it-means-for-the-save-format) — that
  there is no COM runtime to activate an object with — is no longer true.** An
  in-process class registry answers `CoCreateInstance` out of the same 67
  factories the Win32 build registers, which is what made units constructible.
  The save *container* remains exactly as this section describes it.
- **[C.9](#c9-assets-and-licensing)'s asset proposal was overtaken.** It
  proposed an IDBFS import with a manifest. What was built reads an ISO 9660
  image over HTTP range requests, which needs no import step and no manifest,
  because the volume answers directory scans directly. Its licensing analysis is
  unchanged and still governs.
- **The Win32 front end was not the first thing to go.** [C.1](#c1-the-win32-front-end)
  called it the longest pole, and it still is, but a window manager landed
  underneath it (`code/win32user.cpp`) while the dialog-template step did not,
  so the toolkit compiles and runs and no dialog opens. A campaign mission is
  reachable anyway, through a command-line switch that skips the chooser.

Three judgements held and are worth recording as such: the platform seam in
[Part B](#part-b--the-platform-seam) was the right shape and `bgfxbackend.h` was
the right model for it; the threading conclusion in [C.7](#c7-threading) held
exactly — no pthreads, no `SharedArrayBuffer`, no cross-origin isolation; and
the floating-point reasoning in [C.6](#c6-determinism) has not been contradicted,
though it has not been tested either.

OpenTS is a 385,199-line Win32 engine (379 `.cpp` files under `code/`) built
today only as a 32-bit MSVC target. Porting it to WebAssembly through
Emscripten is tractable because Emscripten's wasm32 is ILP32: `int`, `long`,
and pointers are all 32 bits, exactly as on Win32 x86. `code/CMakeLists.txt:6`
already asserts that with a hard `FATAL_ERROR` on any other pointer width, and
wasm32 satisfies it. The layout-sensitive structures, the `uintptr_t` swizzle
identities the save system writes, and the packet structs therefore keep their
current widths, and the 64-bit migration stays a separate, later problem.

Two workstreams are running in parallel with this design and are not covered
here: replacing the x86 assembly with C++, and adding the Emscripten CMake
path. Both are landing as this is written — `code/CMakeLists.txt:6` and
`CMakeLists.txt:16` already branch on `EMSCRIPTEN`, and only
`code/winasm.asm` remains of the assembly — so treat any statement here about
their state as a snapshot. Both have since landed: no `.asm` file remains under
`code/`, and [Building OpenTS](BUILDING.md) owns the Emscripten build. This document owns the two pieces that cannot be
parallelized blindly, the main-loop restructure and the platform seam, and
sizes the remaining subsystem work behind them.

## Contents

- [Outcome](#outcome)
- [Part A — the main-loop restructure](#part-a--the-main-loop-restructure)
- [Part B — the platform seam](#part-b--the-platform-seam)
- [Part C — remaining subsystem work](#part-c--remaining-subsystem-work)
- [Part D — sequencing and cost](#part-d--sequencing-and-cost)

---

# Part A — the main-loop restructure

A browser tab runs the engine on the same thread that services the event loop,
paints, and delivers input. Code that does not return starves all three. This
engine does not return: it blocks in dozens of places, and several of those
places are nested inside each other.

## A.1 What actually blocks

Three constructs stall the engine. A scan of `code/*.cpp` for loop bodies
containing `Sleep`, `Call_Back`, `Windows_Message_Handler`, or
`OwnerDraw::Dialog_Message_Handler` finds **93 loops across 33 files**. The
scan is deliberately wide: it catches bounded iterations that merely service
the callback while doing finite work, as well as real waits, so treat 93 as the
candidate set rather than the blocking set.
[A.2](#a2-load-bearing-versus-incidental) separates them. The constructs are:

| Construct | Count | Definition |
| --- | --- | --- |
| `Sleep()` calls | 28, in 12 files | Yield or wall-clock delay |
| Loops servicing `Call_Back()` / `Windows_Message_Handler()` | 93 loop bodies, 33 files | Poll a condition while keeping audio, network, and Windows messages alive. This is the superset; the two rows below are subsets of it. |
| `OwnerDraw::Dialog_Message_Handler()` pumps | 19 call sites, 13 files | Modal dialog loops; the handler is at `code/ownrdraw.cpp:6870` |
| `WS_Wait_Dialog()` pumps | 18 call sites | `code/windlg.cpp:291`, a second modal pump with its own `PeekMessage` loop |

`Sleep()` sites, by file: `code/mainloop.cpp` (8), `code/wonline.cpp` (5),
`code/conquer.cpp` (3), `code/msengine.cpp` (2), `code/ownrdraw.cpp` (2),
`code/score.cpp` (2), and one each in `code/except.cpp`, `code/netdlg2.cpp`,
`code/session.cpp`, `code/taction.cpp`, `code/vqa.cpp`, `code/windlg.cpp`.

Only **six** sites in the whole tree run a raw Windows message pump
(`PeekMessage`/`GetMessage` + `TranslateMessage` + `DispatchMessage`):
`code/msgloop.cpp:106`–`:159`, `code/windlg.cpp:313`–`:315`,
`code/netdlg2.cpp:687`–`:689`, `code/wonline.cpp:1101`–`:1103`,
`code/vqa.cpp:51`–`:54`, and a commented-out one at
`code/keyboard.cpp:539`–`:544`.
Everything else routes through `Windows_Message_Handler()`
(`code/msgloop.cpp:97`), which drains the queue and returns. That
concentration is the single most encouraging fact in this document: the
blocking is spread wide, but the machinery underneath it is narrow.

## A.2 Load-bearing versus incidental

The recommendation turns on this ratio. A site is **incidental** when the loop
exists only to pass time or to keep the process responsive, and a callback loop
that returns to the browser each frame reproduces its effect for free. A site
is **load-bearing** when the loop is a real wait on state that only arrives
from outside — a network peer, a user click, an audio stream reaching its end.

### Incidental

| Site | What it does |
| --- | --- |
| `code/mainloop.cpp:597` and `:617` | `Sync_Delay` — the fixed-rate frame pacer, `while (FrameTimer) { Call_Back(); … Sleep(0); }`. The whole function exists to burn the remainder of a frame. |
| `code/mainloop.cpp:162`, `:210`; `code/startup.cpp:588`; `code/msengine.cpp:407`; `code/score.cpp:1119` | `while (!GameInFocus)` — parks the game while another application holds focus. A browser expresses this with Page Visibility and by simply not scheduling the callback. |
| `code/ownrdraw.cpp:1778`–`:1893` | The dialog reveal wipe, paced by `Sleep(wait)` at `:1875` and yielded with `Sleep(0)` at `:1890`, running entirely inside one `WM_PAINT`. Already half a state machine — `data->animState` is set to `2` at `:1897`. |
| `code/conquer.cpp:932`, `:961`, `:974` | Map-editor paths (`_DEBUG` only). |
| `code/taction.cpp:2006` | `Sleep(1000)` in a trigger action, after a render. A cosmetic pause that is also a latent multiplayer hazard. |
| `code/except.cpp:1442` | Crash-handler path, inside the half of the file the wasm build compiles out (see [C.8](#c8-structured-exception-handling)). |

Nine of these are loops in the 93-loop set; the `conquer.cpp` and
`taction.cpp` entries are bare `Sleep()` calls with no loop around them. Nine
is a small share — but it includes the two loops that run in every frame of
every game.

### Load-bearing

| Site | Waits on |
| --- | --- |
| `code/queue.cpp:1212` (`Wait_For_Players`) | Lockstep: other players' command packets. This loop calls `Map.Input`, `Keyboard_Process`, and `Map.Render` at `:1439`–`:1443` — it is a **complete nested frame loop**, not a sleep. |
| `code/queue.cpp:1017` (`Wait_For_End_Of_Queue`) | Outstanding packets to drain at game end. |
| `code/netdlg2.cpp:679` | The file-static `_netresponse` (`code/netdlg2.cpp:67`), written only by a dialog proc that this loop's own `DispatchMessage` invokes. |
| `code/netshare.cpp:1362`, `:1368`, `:1464`, `:1470`, `:1581`, `:1594` | Network handshake and player-response counts. |
| `code/sendfile.cpp` (9 loops) | Map transfer between peers. |
| `code/session.cpp:1233`, `code/wonline.cpp` (8 loops) | Send-queue drain and Westwood Online. |
| 19 `Dialog_Message_Handler` pumps + 18 `WS_Wait_Dialog` pumps | A user clicking a button. |
| `code/init.cpp:1185`, `:1464`; `code/house.cpp:1260`, `:1276`; `code/scenario.cpp:1199`, `:1316` | `Theme.Still_Playing()` / `Is_Speaking()` — an audio stream finishing. |
| `code/vqa.cpp:515` | A whole movie playing, frame by frame, inside one call. |
| `code/dropship.cpp:588`, `code/egos.cpp:525`, `:654`, `code/scenario.cpp:506`, `:1283` | Front-end and interlude screens waiting on input or elapsed time. |

**The ratio, and it decides Part A.** Of the 93 candidate loops, about nine are
incidental waits, about eighteen are bounded iterations that service the
callback while doing finite work and need no attention at all, and the
remaining **sixty-odd are genuine waits on external state**. A pure
`emscripten_set_main_loop` restructure would have to flatten every one of those
sixty into an explicit state machine before the game was playable. The cheap
sites are cheap — but they are also the ones on the hot path, which is why a
partial restructure still buys most of the performance.

## A.3 The nesting, which is the actual problem

The waits are not siblings. They nest, and the nesting crosses subsystem
boundaries.

The outer shape is straightforward. `WinMain` (`code/startup.cpp:371`) calls
`Main_Game` (`code/startup.cpp:635`, defined at `code/conquer.cpp:317`), which
runs `while (Select_Game(fade))` at `code/conquer.cpp:350` and, inside it, the
per-frame loop at `code/conquer.cpp:417`–`:431`:

```cpp
for (;;) {
    if (Main_Loop()) {
        break;
    }
    Ingame_Menu_Dialog();
}
```

Underneath that, four independent paths re-enter:

1. **A dialog runs the game.** `OwnerDraw::Dialog_Message_Handler`
   (`code/ownrdraw.cpp:6870`) calls `Main_Loop()` at `:6879` so a multiplayer
   game keeps running while a dialog is open. A `static bool inmainloop` guard
   at `:6872` caps the recursion at one level. So `Main_Loop` → dialog →
   `Main_Loop` is a supported, deliberate control flow.

2. **A stall runs a frame.** `Main_Loop` calls `Queue_AI`
   (`code/queue.cpp:488`) → `Queue_AI_Multiplayer` (`:662`) →
   `Wait_For_Players` (`:1156`), whose `while (1)` at `:1212` renders and
   accepts input. A frame's worth of work runs inside the wait for that frame's
   input.

3. **A desync opens a dialog from inside the simulation.**
   `Execute_DoList` compares the peer's CRC at `code/queue.cpp:3926` and, on a
   mismatch, calls `WWMessageBox().Process(TXT_OUT_OF_SYNC, …)` at `:3929` —
   which is the modal pump at `code/msgbox.cpp:129`, entered from deep inside
   `Queue_AI` inside `Main_Loop`.

4. **A modal pump dispatches a message that opens another dialog.** Both
   `WS_Wait_Dialog` (`code/windlg.cpp:300`) and the `_netresponse` loop
   (`code/netdlg2.cpp:679`) run `DispatchMessage` themselves, and the procs they
   dispatch to create further dialogs — `code/netdlg2.cpp` has five
   `WS_Create_Dialog` sites (`:661`, `:730`, `:786`, `:878`, `:2543`).

A worst-case stack is therefore something like `WinMain` → `Main_Game` →
`Select_Game` → `Net2Remote_Connect` pump → `DispatchMessage` → dialog proc →
`WS_Wait_Dialog` → `Dialog_Message_Handler` → `Main_Loop` → `Queue_AI` →
`Wait_For_Players` → `Execute_DoList` → `WWMessageBox` pump. Six or seven
distinct wait conditions, in one C++ stack, each with different resume
semantics.

There is one very useful precedent, and it is the model for the fix.
`SpecialDialog` (`code/globals.cpp:476`) is a global enum that in-game code
sets as a *request*, and `Ingame_Menu_Dialog` (`code/conquer.cpp:181`) services
it at one well-defined point between frames, running an explicit
`while (SpecialDialog != SDLG_NONE)` transition table at `:202`–`:280`. The
dialogs it dispatches to still block, so this is not a finished example — but
the request-plus-transition-table half of the pattern is exactly what the port
needs, and it was written by the engine's own authors rather than imposed by
the port.

## A.4 The three options

| | Asyncify | JSPI | `emscripten_set_main_loop` |
| --- | --- | --- | --- |
| Mechanism | Binaryen rewrites the module so an instrumented function can unwind its locals to a side stack and rewind later | The VM suspends and resumes a real wasm stack around a JS `Promise` | The engine returns to the browser every frame; state lives in explicit variables |
| Nested modal pumps | Work unchanged | Work unchanged | Must be flattened, one at a time |
| Code size | Emscripten's own documentation puts Asyncify overhead, size and speed together, at "something like 50% or so"; unoptimized builds are much worse | Unchanged | Unchanged |
| Speed | Instrumented functions pay a permanent tax, including on the simulation hot path | No instrumentation tax | No tax |
| Browser support | Universal — it is plain wasm | Chrome/Edge shipped; Firefox 153 enabled by default; Safari 27 beta. Part of Interop 2026 | Universal |
| Blast radius in this tree | The transitive closure of anything that can be on the stack at a yield — because `Call_Back()` is reached from render, logic, and queue code, that is effectively the whole engine | Bounded: declare the async imports and exports | Every load-bearing wait |
| Leaves behind | Permanent size and speed cost, and the reentrancy stays | A dependency on a young VM feature, and the reentrancy stays | Explicit state, no hidden control flow |

Two Asyncify constraints matter specifically here. First, `ASYNCIFY_ONLY` is
the mechanism for containing the instrumentation, and it needs a list of the
only functions that may unwind — with `Call_Back()` and
`Windows_Message_Handler()` called from `code/mainloop.cpp`, `code/queue.cpp`,
`code/netshare.cpp`, `code/sendfile.cpp`, `code/scenario.cpp`,
`code/ownrdraw.cpp` and a dozen more, that list is not small and is not stable
under refactoring. Second, an Asyncify unwind cannot cross a JavaScript frame,
so any browser event that re-enters wasm and then blocks is a fault.

The comparison also has to be read against a fact the tree already establishes:
the two loops that run every frame — `Sync_Delay` at `code/mainloop.cpp:597`
and `:617` — are **incidental**. `Video_Present_If_Dirty`
(`code/video.cpp:312`) already documents that "the game loop is not paced by
presentation." Converting the outer loop to a callback is therefore cheap and
removes the two hottest blocking sites outright. Neither Asyncify nor JSPI is
needed for the part of the port that runs in the inner loop.

## A.5 Recommendation

> [!WARNING]
> **Corrected. This recommendation cannot be implemented as written, and the two
> halves of it are mutually exclusive.**
>
> A callback registered with `emscripten_set_main_loop` is invoked by the
> runtime, not from underneath a promising export, so a JSPI suspend taken
> inside it throws `SuspendError`. That was established with a standalone test
> rather than inferred, and it is the reason the sentence below cannot stand:
> the destination and the scaffold cannot coexist, so the scaffold has to carry
> the whole engine or none of it.
>
> **What was built instead.** `main` (`code/startup.cpp:723`) calls
> `Browser_Init` and then `WinMain` (`:739`), and never returns while the engine
> is running — the return is a promise the page holds. The engine keeps its own
> C++ stack, and `Browser_Yield` (`code/browser.cpp:439`) hands the thread back
> from wherever it happens to be by awaiting `requestAnimationFrame`
> (`code/browser.cpp:111`), falling back to a timer in a hidden tab. Every one
> of the sixty-odd waits is carried that way, and
> `Browser_Blocking_Wait_Count` (`code/browser.cpp:489`) is the number the page
> displays and the number that has to reach zero.
>
> This is a deeper commitment to the scaffold than A.5 intended, and the
> time-boxing below matters more, not less, because of it. The reasoning that
> follows — why not Asyncify, why flattening is the destination — is unaffected;
> only the claim that the two can be run side by side is wrong. The migration
> order in [A.6](#a6-migration-sequence) is affected, at step 4.
>
> `emscripten_set_main_loop` survives in the tree in exactly one place:
> `wasm/demo.cpp:488`, the standalone renderer demo, which takes no JSPI suspend
> and therefore has no conflict.

**Restructure to `emscripten_set_main_loop`. Use JSPI as an explicitly
temporary scaffold for the not-yet-flattened waits, behind a build flag, and
plan to remove it. Do not use Asyncify.**

The reasoning:

- **Asyncify is the wrong price for this codebase.** A ~50% size and speed cost
  paid across a 385k-line engine, most of it on simulation code that never
  needed to yield, buys only the ability to defer the restructure. The
  containment mechanism that would make it cheap requires enumerating a closure
  that this tree does not have.

- **`emscripten_set_main_loop` is the destination because the project already
  wants what it forces.** [Project direction](DIRECTION.md) commits to
  modernizing incrementally toward an entity-component architecture, preferring
  composition, keeping new state separable from behavior, and avoiding new hard
  couplings to the class tree. Flattening a modal pump into a state machine *is*
  that work: it converts control flow hidden in the C++ stack into named state
  that can be inspected, serialized, and eventually owned by a component. The
  `SpecialDialog` dispatch at `code/conquer.cpp:181` is the half-built version
  of that pattern already in the tree.

- **JSPI is the right scaffold and the wrong destination.** It costs nothing at
  runtime, it makes nested pumps work unchanged, and it is now shipped or
  shipping in all three engines. But it makes every entry point return a
  `Promise`, it preserves exactly the reentrancy the project is trying to
  remove, and it pins a shipping browser build to a feature Safari has only in
  beta. As a scaffold it is excellent: it makes the game playable end to end
  early, which is what gives the front-end rewrite (see
  [C.1](#c1-the-win32-front-end)) a working reference to check itself against.

- **The ratio in [A.2](#a2-load-bearing-versus-incidental) says the scaffold is
  needed.** Flattening sixty real waits before the
  first frame renders in a browser would mean months with nothing runnable, and
  most of that work is inside the front end, which is going to be rewritten
  anyway. Doing it in that order would mean flattening code that is then thrown
  away.

The scaffold must be time-boxed in the plan, not just in intention. Two
concrete guards: the JSPI path is a CMake option that is off by default once
the in-game loop is flattened, and the count of remaining
`emscripten_sleep`-equivalent yield points is tracked as a number that only
goes down.

> A caution to carry into implementation: the browser-support statements above
> were checked in August 2026 and are external facts with a short shelf life.
> Re-verify them against the Emscripten documentation and current browser
> release notes before committing to the scaffold, and record the result in
> [Building OpenTS](BUILDING.md) rather than here.

## A.6 Migration sequence

Each step leaves the Win32 build working and, from step 4 onward, leaves a
browser build running.

1. **Make the outer loop reentrant-safe on Win32 first.** Extract the body of
   `code/conquer.cpp:417`–`:431` into a `Game_Frame()` that performs exactly one
   pass and returns a status. `Main_Game` keeps calling it in a loop. No
   behavior change, no browser involvement, and it is verifiable against the
   supported target.

2. **Split `Sync_Delay` into pacing and servicing.** `code/mainloop.cpp:589`
   currently conflates "wait out the frame" with "keep audio, network, and
   messages alive." The servicing half becomes an unconditional per-pass call;
   the waiting half becomes a predicate — `Frame_Is_Due()` — that the caller
   consults. On Win32 the loop still sleeps; on wasm the callback simply returns
   when the frame is not due. This deletes the two hottest blocking sites.

3. **Convert `while (!GameInFocus)` to a state check.** The five focus spins
   become a single `Is_Suspended()` predicate that `Game_Frame` honors by
   returning early. On the browser this reads Page Visibility. Note that the
   multiplayer branch at `code/mainloop.cpp:167` already breaks out
   unconditionally, so the behavior a network game sees is unchanged.

4. ~~**Stand up `emscripten_set_main_loop` around `Game_Frame` and turn on the
   JSPI scaffold.**~~ **Not achievable as written; see the correction in
   [A.5](#a5-recommendation).** A callback the runtime invokes cannot suspend.
   What was done instead: `main` calls `WinMain` and does not return, and a
   yield point inside the engine's own stack awaits an animation frame. The
   engine reaches a browser frame at this step either way, and everything still
   nested still blocks, carried by the scaffold. Steps 1 to 3 were not
   prerequisites for it and were not done first; they remain the right work, and
   they are now what step 9 needs rather than what step 4 needed.

5. **Flatten the in-game waits, highest value first.** `Wait_For_Players`
   (`code/queue.cpp:1212`) is the one that matters: it already renders and takes
   input, so converting it into a `Game_Frame` state — "advancing" versus
   "stalled on peers" — is close to a relabeling, and it is the wait that
   decides whether multiplayer feels alive. `Wait_For_End_Of_Queue`
   (`code/queue.cpp:1017`) follows the same shape.

6. **Flatten the audio waits.** The six `Theme.Still_Playing()` /
   `Is_Speaking()` loops become a deferred continuation on the same
   `SpecialDialog`-style dispatch. These are small, independent, and a good
   place to establish the pattern the front-end work will copy.

7. **Replace the modal pumps as the front end is rewritten.** Each dialog that
   moves off Win32 (see [C.1](#c1-the-win32-front-end)) takes its pump with it.
   The `SpecialDialog` transition table is the template: a request enum, a
   service point between frames, and an explicit result.

8. **Convert the VQA player.** `code/vqa.cpp:515` plays an entire movie inside
   one call. It becomes a per-frame `Advance()` driven from `Game_Frame`, which
   also removes the second message pump at `code/vqa.cpp:51`–`:54`.

9. **Remove the scaffold.** Turn the JSPI option off, fix what breaks, delete
   the option.

Steps 1 through 3 are pure Win32 refactors and should be validated on the
supported target before any wasm work depends on them. Steps 5, 6, and 8 are
independent of each other and of step 7.

---

# Part B — the platform seam

`code/bgfxbackend.h` is the model. Six functions, one `HWND`, and a comment at
`code/bgfxbackend.h:9`–`:11` stating the point: only `bgfxbackend.cpp`
includes bgfx, so no bgfx type appears in the header and no other translation
unit needs the library's headers or build settings. `code/video.cpp` is the only
caller. Every new seam should look like that: a header of free functions with no
backend type in it, one implementation file per backend, one caller.

The renderer side is already done and is better than it looks.
`Backend_Present` (`code/bgfxbackend.h:43`) takes raw 16-bit 565 pixels that
stay owned by the caller. `Video_Present` (`code/video.cpp:284`) hands it
`surface->Get_Buffer()` directly. `DSurface` surfaces are always 565
(`code/dsurface.h:131`), and DirectDraw is already gone — the only residue is a
comment at `code/dsurface.cpp:262`. A browser backend is a WebGL texture upload
of a 565 buffer.

## B.1 Seams that exist and should be kept

| Seam | Where | State |
| --- | --- | --- |
| Renderer | `code/bgfxbackend.h` | Complete. Add an Emscripten backend implementation; change nothing above it. |
| Present policy | `code/video.cpp:284`, `:312` | Complete and already non-blocking: `Video_Present_If_Dirty` documents that it never waits. |
| File I/O | `code/wwfile.h:52` — `FileClass`, 14 pure virtuals, specialized by `RawFileClass` → `BufferIOFileClass` → `CDFileClass` → `CCFileClass` | Nearly complete. `RawFileClass` is the only Win32 dependency: `CreateFile` at `code/rawfile.cpp:273`, `:278`, `:285`, `:358`; `ReadFile` `:471`; `WriteFile` `:529`; `SetFilePointer` `:942`. `code/rawfile.h:47` already abstracts the handle behind `HANDLE_TYPE`. |
| Archive access | `code/mixfile.h:26` | Portable already; sits on `FileClass`. |
| Surface backing | `code/dsurface.h` | Partial. The pixel buffer is allocated by `CreateDIBSection` (`code/dsurface.cpp:146`) and the class exposes a GDI device context (`code/dsurface.h:66`). See [B.3](#b3-the-gdi-text-residue). |
| In-game GUI | `GadgetClass` and its 18 file pairs, 10,750 lines | Complete and portable. Zero `HWND`, zero `WM_`; it draws through `LogicalSurface` (`code/dialog.cpp:107`–`:127`, `code/textbtn.cpp:280`–`:338`). The sidebar, radar, HUD, and chat all sit on it. Nothing to do. |
| Mouse cursor | `code/wwmouse.cpp`, 403 lines | **Corrected.** This row was wrong: it read the wrong file. The pointer is no longer drawn into the frame, and `code/wincursor.cpp` turns a `ShapeSet` frame into a real `HCURSOR` through `CreateDIBSection` and `CreateIconIndirect`. Seaming `wwmouse.cpp` alone produces a game with no pointer. The Emscripten branch at `code/wincursor.cpp:180` encodes the frame as a PNG data URL for `canvas.style.cursor` instead; what a player sees is still the browser's arrow. |
| Scaled input routing | `code/msgroute.cpp:155` | Already converts window coordinates to game coordinates because the engine renders scaled. Keep it; a browser needs the same conversion. |

## B.2 Seams that do not exist at all

| Subsystem | Today | Under Emscripten |
| --- | --- | --- |
| **Window** | `Create_Main_Window` (`code/winstub.cpp:429`), a `RegisterClass` at `:453`, a `WndProc` handling 16 messages, and `MainWindow` as a global `HWND` named in 37 `.cpp` files. There is no interface. | An abstraction over "the surface being presented to" plus a resize/visibility/focus event feed. Emscripten's HTML5 API (`emscripten_set_resize_callback`, `emscripten_set_visibilitychange_callback`) supplies the events; the canvas supplies the target. |
| **Input** | `WWKeyboardClass::Message_Handler` (`code/keyboard.cpp:605`) switches on `WM_KEYDOWN`/`WM_KEYUP` and the six mouse-button messages at `:630`–`:730`, and uses `GetKeyState` (`:230`–`:239`), `MapVirtualKey` (`:335`), `ToAscii` (`:336`), and `GetAsyncKeyState` (`:385`). No interface. | A push interface: the platform layer calls `Post_Key_Event` / `Post_Mouse_Event` with engine-native codes. Emscripten's keyboard and mouse callbacks feed it. The `ToAscii`/`MapVirtualKey` pair must be replaced by the browser's own `key` value; this is a real behavior boundary for text entry and hotkeys, not a mechanical substitution. |
| **Audio** | See [C.2](#c2-directsound). Two independent DirectSound clients, both driven by `timeSetEvent`. No interface. | Done. `code/audiobackend.h` is an `Audio_Backend_*` header in the shape of `bgfxbackend.h`: open device, open/close stream, start, stop, seek, play cursor, gain, service. OpenAL over Web Audio behind it. |
| **Time** | `timeGetTime` in 8 files, `timeBeginPeriod`/`timeSetEvent` in `code/dsaudio.cpp:464`, `:470`, `code/ahandle.cpp:285`, `:288`, `code/mstimer.cpp:24`, `code/milsectmr.cpp:42`, `code/except.cpp:1970`. `QueryPerformanceCounter` in `code/mpu.cpp:223`. | A single `Platform_Milliseconds()` and a per-frame service call. `emscripten_get_now()` supplies the clock; the periodic timers become main-loop polls. |
| **Threading** | See [C.7](#c7-threading). Two `CreateThread` in `code/except.cpp`, two `timeSetEvent` callback threads, 121 synchronization calls across 8 files. No interface. | Nothing. The concurrency exists only to keep audio fed while the main thread blocks; once the main thread stops blocking, the timers become main-loop calls and the locks become no-ops. |
| **Resources** | `Fetch_String` (`code/data.cpp:200`) and `Fetch_Resource` (`code/data.cpp:270`), both against `Language.dll`. See [C.4](#c4-languagedll). | Done. `code/peresource.cpp` reads the shipped library's resource directory out of the file, behind the same two functions. |
| **Sockets** | See [C.5](#c5-networking). | A `Send_To`/`Receive_From` transport interface; `UDPInterfaceClass` already has one internally. |

## B.3 The GDI text residue

> **Corrected: the residue is not all text.** `DSurface::Blit_From`
> (`code/dsurface.cpp:492`) is a third site and a functional one. It sends a
> blit to the software blitter when the source is not GDI-backed (`:502`) and
> otherwise stretches it with `StretchBlt` (`:529`). On this target
> `Is_GDI_Backed` is always false (`code/dsurface.h:122`), so every blit takes
> the software path — and that path does not scale: `Bit_Blit` copies
> `std::min(srect.Height, drect.Height)` rows (`code/blit.cpp:342`). A
> destination larger than its source gets a 1:1 copy in the corner.
> `DSurface::AllowStretchBlits` is still initialized to true
> (`code/dsurface.cpp:79`), and the comment beside its declaration claiming
> surfaces stretch in software does not match `Bit_Blit`. No scaled blit has
> been observed either way.

`DSurface` hands out a Windows device context (`code/dsurface.h:66`), and four
places draw text through it: `code/ownrdraw.cpp:4940`, `:5687`, `:6152`, and
`code/tactical.cpp:1341`. The in-game one is already guarded —
`Tactical::Draw_Screen_Text` checks `CompositeSurface->Is_GDI_Backed()` at
`code/tactical.cpp:1338` and draws nothing when the surface cannot supply a DC.
That guard is exactly the seam the port wants, and it means the in-game path
degrades correctly on day one for free.

The `ownrdraw` uses are harder, but bounded: eight `GetDC`, eight
`SelectObject`, ten `GetTextExtentPoint32`, two `TextOut`, two `DrawText` in
the whole 7,001-line file. Almost all of it is font *metrics*, not painting —
the actual pixels go to `AlternateSurface` (98 references) and
`VisibleSurface` (29 references) through the engine's own blitter, with zero
`BitBlt` and zero `BeginPaint` in the file. A metrics-and-glyph provider is
therefore a small, well-defined replacement, not a rendering rewrite.

---

# Part C — remaining subsystem work

Cost figures below are judgment, expressed in engineer-months for one engineer
familiar with the tree. They are estimates, not measurements, and they exclude
the validation a compatibility-boundary change needs under
[Contributing](../CONTRIBUTING.md).

## C.1 The Win32 front end

> **Where this stands.** Phase 1 is half built and the half that is missing is
> the one that matters. `code/win32user.cpp` is the portable shim this section
> proposes — handles, a message queue, `SendMessage`/`DispatchMessage`, the
> dialog-item protocol — and `Fetch_Resource` already hands back the 72 dialog
> templates in their shipped layout, as [C.4](#c4-languagedll) predicted it
> would. What does not exist is the step between them: `CreateDialogParam`,
> `CreateDialogIndirectParam`, and `DialogBoxParam` are still stubs
> (`code/win32compat.cpp:2613`–`:2615`), so `OwnerDraw::Begin_Dialog` returns
> null (`code/ownrdraw.cpp:6737`) and every dialog is dead. The toolkit compiles
> and runs; nothing ever asks it to draw.
>
> One consequence the section did not anticipate: the graphical main menu does
> not depend on the toolkit, so it renders anyway, and a campaign mission can be
> started by a command-line switch that skips the chooser
> (`code/init.cpp:1854`). The front end is still the longest pole; it is no
> longer the thing standing between the port and a played mission.

**This is the longest pole.** The entire out-of-game experience — main menu,
options, skirmish setup, multiplayer lobby, save/load, message boxes — is real
Win32 windows.

| File | Lines | Role |
| --- | --- | --- |
| `code/ownrdraw.cpp` | 7,001 | Owner-draw control toolkit |
| `code/netdlg2.cpp` | 3,460 | Multiplayer lobby, three dialog procs |
| `code/loaddlg.cpp` | 939 | Save/load/delete, three dialog procs |
| `code/windlg.cpp` | 759 | Dialog stack and the `WS_Wait_Dialog` pump |
| `code/tooltip.cpp` | 402 | Tooltip manager |
| `code/msgbox.cpp` | 270 | `WWMessageBox` |

`ownrdraw.cpp` subclasses stock Win32 controls and repaints them entirely with
the game's own blitter. The dispatch table at `code/ownrdraw.cpp:835`–`:863`
maps class names to per-control window procedures covering button
(`code/ownrdraw.cpp:2009`), edit (`:2506`), static (`:2709`), checkbox
(`:2899`), combo (`:3015`), listbox (`:3289`), scrollbar (`:4110`), progress
(`:4530`), trackbar (`:4597`), groupbox (`:4930`), hotkey (`:5005`), and a tab
control used as a text box (`:2210`), behind a universal dispatcher at `:1108`
that is 858 lines on its own. Exactly one class is registered —
`"ComboDropWin"` at `code/ownrdraw.cpp:245`. It talks to the rest of the game
through 27 custom messages declared at `code/ownrdraw.h:372`–`:508`, and 30
files include that header.

Counting `WM_[A-Z0-9_]*` tokens across `code/*.cpp` and `code/*.h` gives **649
occurrences in 36 files**, with `code/ownrdraw.cpp` (167),
`code/wonline.cpp` (161), `code/netdlg2.cpp` (36), `code/winstub.cpp` (35), and
`code/ownrdraw.h` (28) accounting for two thirds.

The encouraging half of the inventory is how few *structural* Win32 calls there
are behind all that volume:

| Construct | Count |
| --- | --- |
| `SendDlgItemMessage` / `GetDlgItem` / `SendMessage` | 1,179 |
| `InvalidateRect` | 67 |
| `SetWindowLong*` | 29 |
| `CallWindowProc` | 22, all in `code/ownrdraw.cpp` |
| `CreateWindow*` | 5 |
| `CreateDialog*` | 4 |
| `RegisterClass*` | 2 real |
| `DialogBox*` (true OS modal loops) | 2 — `code/except.cpp:1374` and `code/init.cpp:929` |

The 1,179 message calls are a *protocol* against control handles, mostly
carrying the engine's own `OD_*` messages. They do not need to change if
something answers them.

### Strategy

**A portable HWND shim first, then dialog-by-dialog replacement.**

*Phase 1 — the shim.* Reimplement handles, a message queue, `SendMessage`,
`DispatchMessage`, `IsDialogMessage`, `TranslateAccelerator`,
`SetTimer`/`KillTimer`, and the ~60 window-manipulation calls in portable C++,
plus a reader for the 72 dialog templates `CreateDialogIndirectParam`'s two
callers (`code/windlg.cpp:105`, `code/ownrdraw.cpp:6723`) fetch out of the
language library, plus a font-metrics provider for
[B.3](#b3-the-gdi-text-residue). Every dialog file, `ownrdraw.cpp` included,
compiles unchanged. Estimated 4,000–8,000 lines of new code, **2–4 months**.

The shim is the right first move for four reasons: it is mechanical and
therefore reviewable; it is checkable line by line against the Win32 build,
which is the only reference that exists; it unblocks everything downstream at
once; and it converts a 12,000-line porting problem into a 5,000-line writing
problem.

It does *not* fix blocking. The pumps still block; JSPI carries them until
phase 2 removes them.

*Phase 2 — replacement.* Rewrite each dialog on the existing `GadgetClass`
system, which is already portable, already `Surface`-based, already used
in-game, and already free of `HWND`. Each dialog that moves takes its modal
pump with it, so this is also the bulk of [A.6](#a6-migration-sequence) step 7.
Estimated **6–12 months**, spread, and parallelizable across contributors once
the pattern is set by the first two or three dialogs.

### Interaction with the entity-component direction

[Project direction](DIRECTION.md) is explicit that there is no big-bang rewrite
and that new work should prefer composition, keep state separable from
behavior, and avoid new couplings to the class tree. Both phases are consistent
with that, but only if phase 1 is understood as scaffolding:

- The shim adds a *new* hard coupling to the Win32 model. It must therefore be
  written as a leaf that nothing new depends on, and phase 2 must actually
  happen. A shim that becomes permanent is a step away from the direction, not
  toward it.
- Phase 2 is a step toward it. Flattening a pump replaces control flow encoded
  in the C++ stack with state that a component could own, and moving a dialog to
  `GadgetClass` moves it onto composition instead of a `WndProc` inheritance
  chain.
- Neither phase should try to build the entity-component system. The obligation
  is only that the migration stays possible.

## C.2 DirectSound

`code/dsaudio.cpp` is 2,682 lines, and it is not the only DirectSound client:
`code/ahandle.cpp` (774 lines) is a second, independent one for movie audio,
with its own buffer, its own timer, and its own locking. Both must be ported.

The model is unusually friendly. **The engine does not mix.** It creates one
primary buffer to pin the output format (`code/dsaudio.cpp:347`, played looping
and silent at `:437`) and `MAX_SFX` — five, from `code/sound.h:41` — secondary
buffers, one per voice (`code/dsaudio.cpp:493`), and lets DirectSound combine
them. Per-voice volume goes through `IDirectSoundBuffer::SetVolume`
(`code/dsaudio.cpp:1216` and six more). The engine's only per-buffer work is
decompress-and-copy into a locked region: `code/dsaudio.cpp:1174` and `:1553`,
each followed by `Unlock` at `:1205` and `:1598`.

That maps onto OpenAL almost directly: five sources, `alSourceQueueBuffers` /
`alSourceUnqueueBuffers` in place of the lock-write-unlock cycle,
`alSourcef(AL_GAIN)` in place of `SetVolume`, and `Sample_Copy`
(`code/dsaudio.cpp:2592`) unchanged. Emscripten ships an OpenAL implementation
over Web Audio; SDL2 audio is the alternative if a single mixed callback is
preferred.

**What landed.** `code/audiobackend.h` and `code/audiobackend.cpp`. The seam
keeps the looping ring rather than a submit-a-buffer call, because the ring and
its play cursor are what the engine's own refill logic is written against; the
queue is an implementation detail underneath it. `audiobackend.cpp` also carries
the DirectSound shaped object that both clients consume, declared in
`dsaudio.h`, so `dsaudio.cpp` changes in two places — the object it starts from,
and the service pass — and `ahandle.cpp` not at all. A page that will not start
audio, and a host with no Web Audio at all, both leave the stream running on the
wall clock, so the driver retires its samples on time either way.

Three details to plan for:

- **The service tick.** `timeSetEvent` at `code/dsaudio.cpp:470` runs
  `Sound_Timer_Callback` (`:1431`) 40 times a second on a multimedia-timer
  thread. Under a callback main loop it becomes a call from `Game_Frame`. The
  existing main-thread pass, `DSAudio::Sound_Callback` (`:1961`), is already the
  right entry point.
- **The codec was assembly.** `SCOMP_SOS` ADPCM decoding calls
  `sosCODECDecompressData` (`code/dsaudio.cpp:2668`) and
  `General_sosCODECDecompressData` (`:2670`), which had no C implementation. The
  assembly-removal workstream has since supplied one in `code/soscodec.cpp`
  (`:193`, `:202`, `:241`, `:254`), so this is no longer on the critical path —
  but the audio work depends on it, and the two must be validated together
  rather than separately.
- **Delete the dead thread.** `code/dsaudio.cpp:472` is a commented-out
  `_beginthread`, and roughly two dozen commented-out critical-section calls
  scatter through the file. Westwood abandoned that design; nothing needs
  porting.

Estimated **1–2 months**, plus the codec rewrite.

A related subsystem rides along: the VQA movie player under `code/vqalib/`
(11,164 lines) is the client of `code/ahandle.cpp`, and it also owns the second
raw message pump at `code/vqa.cpp:51`–`:54` and the whole-movie blocking loop at
`code/vqa.cpp:515`. It is otherwise portable — the decoders are the only Win32
or x86 dependency, and the assembly workstream has already replaced them
(`code/vqalib/vqa_sos.cpp`, `code/vqalib/cmp.cpp`). Budget it with the
main-loop work
([A.6](#a6-migration-sequence) step 8) rather than with audio, and note that
cutscenes are a candidate to defer entirely on a first browser build, since
[C.9](#c9-assets-and-licensing) makes the movie archives optional anyway.

## C.3 COM, and what it means for the save format

It is tempting to conclude that COM is vestigial here and can simply be
deleted, because the modernized save path makes it look that way. That
conclusion is wrong in one specific and expensive respect.

It is true that the *payload* is modern. `SaveStreamClass`
(`code/savestream.h:68`) drives one member-wise `Serialize` description in both
directions, 95 classes implement it, and pointers are swizzled by `uintptr_t`
identity (`code/savestream.h:134`–`:143`, announced at
`code/abstract.cpp:255`). `SaveStreamClass` touches `IStream` in exactly two
places, the `Write` and `Read` inside `Serialize_Bytes` at
`code/savestream.cpp:69` and `:71`.

It is also true that reference counting is nearly vestigial.
`AbstractClass::AddRef` and `::Release` (`code/abstract.cpp:155`, `:166`) both
`return(1)`, as `code/abstract.h:86`–`:89` states. Genuine refcounting exists
only in `BulletClass` (`code/bullet.cpp:1467`–`:1487`), `LocomotionClass`
(`code/loco.cpp:192`–`:215`), `CStreamClass`, and the class factory template.

But **COM is the save system's object factory and type tag**, and that is
load-bearing:

- Objects are written with `OleSaveToStream` (`code/saveload.cpp:202`, `:356`)
  and read with `OleLoadFromStream` (`:174`, `:714`). `OleSaveToStream` writes
  the object's CLSID and then calls `IPersistStream::Save`;
  `OleLoadFromStream` reads the CLSID, `CoCreateInstance`s it, and calls
  `IPersistStream::Load`. The 16-byte CLSIDs are therefore **in every save
  file**, as the polymorphic type discriminator.
- To make `CoCreateInstance` find its own classes, the engine registers **67
  in-process class factories** at `code/startup.cpp:278`–`:344` through the
  `REGISTER_CLASS` macro at `:271`.
- The container is an **OLE compound file**, not a flat file:
  `StgCreateDocfile` (`code/saveload.cpp:937`), a `CONTENTS` stream
  (`:978`), an LZO-compressing `IStream` obtained by
  `CreateInstance(CLSID_CompressStream, …)` (`:985`), and metadata written
  through `IStorage` in `code/savever.cpp` (`:328`, `:492`, `:649`, `:725`,
  `:792`, `:868`, `:935`, `:1005`).

None of `ole32` exists under Emscripten. So the replacement has to answer a
compatibility question, not just a portability one.

> **Corrected, in one half.** "None of `ole32` exists" is no longer true of
> activation. `code/win32compat.cpp:2203`–`:2344` is an in-process class
> registry: `CoRegisterClassObject` publishes a factory and `CoCreateInstance`
> is a table lookup followed by `IClassFactory::CreateInstance`, with no
> registry, no marshalling, and no second process. `RegisterClasses`
> (`code/startup.cpp:241`) publishes the same 67 factories it publishes on
> Windows, so a locomotor activates and a unit is constructible. In effect the
> first half of proposal 1 below was answered by keeping the COM shape rather
> than by replacing it, which is cheaper and leaves the argument for replacing
> it intact.
>
> The container half is untouched. `StgCreateDocfile` and `StgOpenStorage`
> report `E_NOTIMPL` (`code/win32compat.cpp:2676`, `:2677`), so `Save_Game`
> writes nothing (`code/saveload.cpp:937`) and `Load_Game` fails before it opens
> a stream (`:1184`). Saving and loading do not work, and a saved file would in
> any case land on a filesystem the tab discards. Proposal 2 is therefore still
> the live decision, unchanged.

**Proposal, in two independent pieces:**

1. **Replace the COM interfaces with plain C++.** `IPersistStream` becomes an
   abstract `PersistentClass` with `Load`/`Save`/`Get_Class_ID`. `IStream`
   becomes an abstract byte sink with `Read`/`Write`/`Seek` — the engine already
   implements it itself (`CStreamClass`, `code/cstream.h:17`), so nothing
   depends on the system interface. `QueryInterface`/`AddRef`/`Release` vanish
   from `AbstractClass`; `BulletClass` and `LocomotionClass` keep explicit
   reference counts as ordinary members. The 67 class factories become a plain
   `map<ClassID, factory function>` registry — the same table, without
   `CoRegisterClassObject`. Keeping the existing 16-byte class identifiers as
   the on-disk tag preserves the discriminator exactly.

2. **Decide, deliberately and separately, what happens to the container.** Two
   options, and this is a compatibility-boundary decision that
   [Contributing](../CONTRIBUTING.md) requires be made explicitly:

   - *Reproduce it.* Write a portable compound-file reader/writer and reproduce
     `OleSaveToStream`'s framing. Existing saves keep loading. Fiddly, and it
     carries a dead Microsoft format forever.
   - *Replace it.* Define a straightforward container — a header, a stream
     directory, the same LZO-compressed `CONTENTS` payload. Simpler, portable,
     and a documented save-format break needing migration guidance. Note that
     [Contributing](../CONTRIBUTING.md) already records that saves carry the
     project version and that different versions refuse each other's saves, so
     a break at a version boundary is a supported outcome rather than a novel
     one.

   The recommendation is *replace*, done as its own change on the Win32 target
   with its own change record, before the wasm build depends on it. Reproducing
   a format nobody wants in order to port away from the platform that defined it
   is work spent in the wrong direction.

Estimated **1–3 months** for piece 1, **1–2 months** for piece 2.

One further COM user is out of scope entirely: `code/wonline.cpp` (10,514
lines) drives the Westwood Online SDK through ATL (`CComObjectRoot`,
`AtlAdvise`) and `CoCreateInstance` on `CLSID_Chat`, `CLSID_NetUtil`, and
`CLSID_Download` (`:4637`, `:4650`, `:4663`) — in-process DLLs for a dead
service, pointing at dead servers. It should be excluded from the wasm target
outright. See [D.4](#d4-genuine-risks).

## C.4 Language.dll

The dependency is `LoadLibrary("Language.dll")` at `code/data.cpp:346`, and the
entire read surface is **two functions**:

- `Fetch_String` (`code/data.cpp:200`) — a 128-entry LRU cache in front of one
  `LoadString` at `code/data.cpp:253`.
- `Fetch_Resource` (`code/data.cpp:270`) — `FindResource` (`:273`),
  `LoadResource` (`:278`), `LockResource` (`:283`), returning a pointer that is
  never released.

`Fetch_Resource` has exactly **two callers**, `code/windlg.cpp:105` and
`code/ownrdraw.cpp:6723`, and both fetch `RT_DIALOG` templates. Version
reporting adds `GetModuleFileName` / `GetFileVersionInfo` / `VerQueryValue` at
`code/data.cpp:396`–`:404`.

The payload is `code/language/language.rc`, 3,036 lines: **72 dialog
templates**, 49 `STRINGTABLE` blocks holding **654 localized strings**, and a
version resource. The `Language` target is already excluded from the wasm build
(`CMakeLists.txt:77`), so `Fetch_String` is the first thing that will fail at
runtime.

**Resolved.** `code/peresource.cpp` reads the resource directory of the shipped
`Language.dll` straight out of the file: DOS header, optional header, section
table, and the three level type/name/language tree. No new container format
exists and the library is not rebuilt, so the file the player installed and the
file `code/language/` builds are both read as they are.

`Fetch_String`'s cache stays exactly as it is; only the `LoadString` line
changes, and the string table is read in its shipped form — bundles of sixteen
UTF-16 strings, narrowed to Windows-1252 bytes, which is what `LoadStringA`
hands the engine on Windows. `Fetch_Resource` returns a pointer into the held
image, the lifetime `LockResource` gives it. Version reporting reads the
`VS_VERSIONINFO` resource through the same reader.

Under Emscripten the library is opened with `RawFileClass` rather than
`CCFileClass`: a global constructor reaches `Fetch_String`, so the load runs
before the mixfile and search path objects are constructed and may not touch
them. That also matches the Windows build, where the module loader reads the
installation directory and never the game data.

The dialog templates need no converter. `Fetch_Resource` already returns
`RT_DIALOG` bytes in their shipped layout on both targets, so what
[C.1](#c1-the-win32-front-end) consumes is the same structure the Win32 build
consumes.

## C.5 Networking

The Winsock surface is remarkably small. **The only `socket()` call in the tree**
is `code/wspudp.cpp:302` — `socket(AF_INET, SOCK_DGRAM, 0)` — and there is no
TCP path anywhere: `SOCK_STREAM` and `IPPROTO_TCP` do not appear in `code/` at
all. `code/wsproto.h:75` declares the whole protocol enumeration as
`{ PROTOCOL_NONE, PROTOCOL_UDP }`. Despite its name, `code/ipxmgr.cpp` has no
IPX left; all four modes construct a `UDPInterfaceClass`
(`code/ipxmgr.cpp:206`, `:229`, `:246`, `:267`).

The complete API: `WSAStartup` (`code/wsproto.cpp:360`), `socket`
(`code/wspudp.cpp:302`), `bind` (`:315`), two `sendto` (`:145`, `:165`), two
`recvfrom` (`:182`, `:186`), five `setsockopt`, `closesocket`, `gethostbyname`,
`inet_addr`, `GetAdaptersInfo`, and `WSAAsyncSelect`
(`code/wsproto.cpp:211`), which posts `WM_UDPASYNCEVENT`
(`code/wsproto.h:69`) to the main window; `UDPInterfaceClass::Message_Handler`
(`code/wspudp.cpp:520`) drains on `FD_READ` at `:540`.

Browsers have no UDP, and `WSAAsyncSelect` has no equivalent. But the tunnel
support is the foothold, and it is a strong one. `code/wspudp.h:117`–`:120`:

```cpp
		// A tunnel is in use when TunnelPort is non-zero.
		unsigned short TunnelID;
		unsigned long TunnelIP;
		unsigned short TunnelPort;
```

`Configure_Tunnel` (`code/wspudp.cpp:128`) makes every datagram carry a
four-byte header naming sender and recipient by tunnel ID, sent to a single
relay address: `Send_To` at `code/wspudp.cpp:142` (header written at `:156`–
`:157`) and `Receive_From` at `:179` (rejected unless `header[1] == TunnelID`
at `:193`). `code/wspudp.h:66`–`:72` documents this as CnCNet-compatible.

**Proposal.** Make `Send_To` and `Receive_From` the transport seam — they
already are, internally — and add a WebSocket implementation behind them. In
tunnel mode every packet goes to one address, so a browser client is one
WebSocket to the relay, with each message carrying one tunnelled datagram.
Delivery becomes a poll drained from `Game_Frame`, replacing the
`WSAAsyncSelect` message; the queue shape above it is unchanged. LAN discovery
(`SO_BROADCAST` at `code/wspudp.cpp:325`) and direct-peer mode simply do not
exist in a browser, and the wasm build should offer tunnel mode only.

**Cross-play with native peers is achievable, and the wire format is not the
obstacle.** The bytes on the tunnel side are identical: the same four-byte
header, the same payload, the same lockstep protocol. What is required is a
relay that accepts both UDP and WebSocket clients and forwards between them —
work outside this repository, in the tunnel server. The obstacle to cross-play
is determinism, not transport. See [C.6](#c6-determinism).

Estimated **1–2 months** engine-side. The relay is a separate project.

## C.6 Determinism

The engine is lockstep with per-frame CRC comparison. `Compute_Game_CRC`
(`code/queue.cpp:4220`) accumulates, through the rotate-and-add helper
`Add_CRC` (`:4302`):

- every infantry, unit, and building's `PositionCoord.As_Int()` plus its
  facings (`:4233`–`:4252`);
- every map- and logic-layer object's position plus `RTTI` (`:4256`–`:4275`);
- `Scen->RandomNumber` (`:4281`).

The value rides in each frame's `FRAMEINFO` header (`code/queue.cpp:2949`), is
ring-buffered into `CRC[256]` (`:725`), and is compared at
`code/queue.cpp:3926`. On mismatch it dumps diagnostics (`Print_CRCs`, `:3927`)
and shows a modal Continue/Stop prompt (`:3929`); Continue destroys every
connection (`:3932`–`:3935`).

Note what is hashed: **integers only**. Float state is not in the sync check
directly. That is not reassurance — it means drift is invisible until it
crosses an integer quantization boundary, and then it appears as a desync with
no proximate cause.

### Where floats reach simulation state

`/arch:SSE2 /fp:precise` is already the build setting, documented at
`code/CMakeLists.txt:104`–`:107` as "IEEE-754 single precision, no x87 excess
precision" and "no reassociation of the engine's accumulations." This matters
enormously: it means the basic arithmetic operations are already
IEEE-754-conformant and unreassociated. Wasm's `f32`/`f64` add, subtract,
multiply, divide, and `sqrt` are likewise IEEE-754 correctly-rounded
instructions. **Those operations should therefore already agree bit for bit**,
including the 58 `sqrt` call sites — `sqrt` is correctly rounded by the
standard and is a wasm instruction, not a library call. That is a reasoned
expectation, not a measurement; the replay harness below is how it gets
confirmed rather than assumed. The Emscripten build must not enable fast-math,
which would forfeit all of it.

The risk is confined to the transcendental functions, which IEEE-754 does not
require to be correctly rounded and where Emscripten's musl libm and the MSVC
CRT will differ in the last bits. Across `code/*.cpp` there are about 110 such
calls — 39 `std::sin`, 28 `std::cos`, 11 `std::atan2`, plus `tan`, `asin`,
`acos`, `atan`, `pow`, and `log` — spread over 24 files, and a further 20 in
`code/velocity.h`, which is the projectile velocity primitive itself
(`code/velocity.h:41`–`:42`, `:59`, `:127`–`:132`, `:163`, `:206`). Fourteen of
those files are in the simulation path: `bounce`, `bullet`, `building`,
`combat`, `drive`, `droppod`, `fly`, `hover`, `ionblast`, `jumpjet`,
`levitate`, `techno`, `vein`, `wave`.

The sites that convert a transcendental result into integer simulation state
are the ones that decide a desync:

| Site | What it produces |
| --- | --- |
| `code/combat.cpp:853` | `angle = std::acos(std::sqrt(value) / v)` → a `DirType` launch pitch |
| `code/building.cpp:1253` | `(int)((std::asin(dr) * 16384.0) * M_2_PI)` → a `DirType` |
| `code/techno.cpp:3901` | `std::atan2(-displacement.Y, displacement.X)` → launch heading |
| `code/techno.cpp:3947` | `std::atan2f((double)abs_z - y, planar)` → barrel pitch (note the `f` variant on `double` arguments) |
| `code/techno.cpp:8069`–`:8070` | `coord.Y -= std::sin(dir) * travel; coord.X += std::cos(dir) * travel;` → the predicted aim point for a moving target |
| `code/bullet.cpp:453` | `std::sin(...) * rotvar` → a weaving missile's integer turn rate |

By contrast, `code/combat.cpp:870` —
`(MPHType)(int)std::sqrt(range * gravity * 1.2)`, the muzzle velocity of every
arcing weapon — reaches integer simulation state through `sqrt` alone, and so
is expected to be safe. It is a useful reference case for the harness: if it
diverges, the assumption above about the basic operations is wrong and the plan
needs rethinking.

`code/techno.h:213`–`:222` also holds four `float` rocking angles that are
integrated every frame (`Rocking_AI`, `code/techno.cpp:7945`) **and
serialized** (`code/techno.cpp:8145`–`:8148`), so they are save state, not view
state.

### Assessment and proposal

- **wasm against wasm is safe** with no change, given `/fp:precise`-equivalent
  settings on the Emscripten side (do not enable fast-math) and one libm for
  both peers, which two wasm builds have by construction.
- **wasm against native is not safe** without pinning a shared libm.

**Pin a libm inside the simulation.** Add a header — call it
`code/simmath.h` — exposing `Sim_Sin`, `Sim_Cos`, `Sim_Atan2`, `Sim_Asin`,
`Sim_Acos`, `Sim_Tan`, `Sim_Pow`, and route the sim-path call sites — the
fourteen files above plus `code/velocity.h`, on the order of eighty calls —
through it. The implementation is one small, vendored, deterministic set of
polynomial approximations compiled identically on both targets. Leave `sqrt`
and basic arithmetic alone — they are already conformant — and leave
`code/matrix3d.cpp` (26 calls) and `code/quat.cpp` (13 calls) alone as well
unless a specific voxel pose is shown to feed back into simulation state;
they are pose interpolation.

The engine-side cost is **1–2 months**: mechanical substitution, plus a replay
harness that runs a recorded game to a fixed frame on both targets and compares
the CRC ring. That harness is worth building regardless and satisfies the
"automated checks must not require proprietary assets" rule only if it ships
with a synthetic scenario rather than a real recording — a constraint to design
for rather than discover.

The honest catch: **pinning the libm changes the native build's simulation
too.** It is a deliberate behavior change at a compatibility boundary — it
alters replays, cross-version network compatibility, and possibly gameplay in
edge cases — and needs its own change, evidence, and change record on the Win32
target before the wasm build depends on it. It cannot be smuggled in as a
porting detail.

There is also a **dormant asset** worth knowing about: 61 files implement
`Compute_CRC(CRCEngine &)`, and `CRCEngine` hashes floats and doubles by raw
bit pattern (`code/crc.cpp:113`, `:131`, with the consequence stated in the
comment at `:109`–`:110`). `Object_CRCs` (`code/saveload.cpp:222`) aggregates
them and currently has **no callers**. Wiring it into the replay harness would
give the port a float-exact divergence detector that pinpoints the first
diverging object, rather than a positional checksum that reports the symptom
many frames later. That is probably the single highest-leverage day of work in
this entire section.

## C.7 Threading

The port needs no threads. But the reason is not that the engine is
single-threaded today — it is not — and getting the reason right matters,
because it is what makes the conclusion survive contact with the audio work.

There are exactly two `CreateThread` sites, both in
`code/except.cpp` — the minidump writer at `:1821` and a self-test at `:1949`.
`std::thread`: zero. `pthread_*`: zero. `_beginthread`: one site, commented out
(`code/dsaudio.cpp:472`).

But two **multimedia-timer callbacks** run on OS timer threads:
`code/dsaudio.cpp:470` (`Sound_Timer_Callback`, 40 Hz) and
`code/ahandle.cpp:288` (VQA movie audio, 60 Hz). They are guarded by real
synchronization — 121 calls to `CreateMutex`, `InitializeCriticalSection`,
`EnterCriticalSection`, `WaitForSingleObject`, `ReleaseMutex`, and
`Interlocked*` across eight files, concentrated in `code/dsaudio.cpp` (51),
`code/ahandle.cpp` (28), `code/wonline.cpp` (20), and `code/except.cpp` (13).

**Why the conclusion still holds.** That concurrency exists
for exactly one reason: to keep audio fed while the main thread is blocked in
one of the 93 loops from [A.1](#a1-what-actually-blocks). Once the main loop
returns to the browser every frame, both timers become ordinary calls from
`Game_Frame`, and both already have main-thread entry points —
`DSAudio::Sound_Callback` (`code/dsaudio.cpp:1961`) is precisely that. The
`Interlocked*` uses are COM reference counts on objects never touched across
threads and degrade to plain increments. `AppMutex` and `AutoPlayMutex`
(`code/startup.cpp:399`, `:449`) are single-instance and CD-autoplay guards,
both meaningless in a browser tab. `code/except.cpp`'s `DbgHelpLock` goes away
with the file.

So: **the wasm build needs no pthreads, no `SharedArrayBuffer`, and no
COOP/COEP cross-origin isolation.** What that buys, concretely:

- The build can be served from any static host, including GitHub Pages, with no
  special response headers. Cross-origin isolation is the single most common
  reason a WebAssembly game will not run on the host a project already has.
- No `SharedArrayBuffer` means no dependency on Spectre-mitigation policy
  differences between browsers and embedded webviews.
- Single-threaded Emscripten output is smaller and simpler, and the whole
  category of data races the port could introduce does not exist.
- Testing and debugging stay deterministic, which matters directly for
  [C.6](#c6-determinism).

This is the cheapest good news in the port, and it should be protected: adding
a worker thread later would forfeit all four benefits at once.

Estimated **2–4 weeks**, most of it in deleting rather than writing.

## C.8 Structured exception handling

`code/except.cpp` is 1,981 lines and holds **every** `__try`/`__except` in the
codebase — 15 pairs, all in the uniform `__except (EXCEPTION_EXECUTE_HANDLER)`
form, each wrapping a call that might fault while already handling a crash.
There are no `__finally` blocks anywhere and no vectored handlers. The only SEH
outside the file is one `RaiseException` at `code/jshell.cpp:64`, matching the
one at `code/except.cpp:1518`.

It is a complete post-mortem crash reporter: `SetUnhandledExceptionFilter`
(`:1773`), `StackWalk64` with a hard-coded `IMAGE_FILE_MACHINE_I386` (`:425`),
`MiniDumpWriteDump` (`:436`), DbgHelp symbol resolution behind a lock (`:405`,
`:415`, `:1712`), an x87 and SSE register dump (`:627`, `:653`), a handoff to a
dedicated dumper thread (`:1821`), the full CRT handler chain
(`_set_purecall_handler`, `_set_invalid_parameter_handler`,
`std::set_terminate`, `SIGABRT`, `SetThreadStackGuarantee` at `:1774`–`:1796`),
and a dialog at `:1374`.

None of it has a WebAssembly analogue. There is no `CONTEXT`, no
`EXCEPTION_POINTERS`, no DbgHelp, no minidump, and no SEH.

**Proposal.** Exclude the translation unit from the wasm target and provide a
small portable replacement for the only two things the rest of the tree depends
on: `Fatal()` (`code/except.h:58`) and the custom exception codes raised at
`code/except.cpp:1518` and `code/jshell.cpp:64`. On wasm these become a message
to the console, a JavaScript stack capture, and `abort()`. Emscripten's own
`-sASSERTIONS` and demangled stack traces do the rest.

> **Adjusted.** The shape is what was done; the packaging is not. The file is
> compiled rather than excluded, and the split is inside it: everything from
> `code/except.cpp:79` to `:1996` is `#if !defined(__EMSCRIPTEN__)`, and a
> WebAssembly half at `:1997` keeps the entry points and announces its own
> absence. A fault reaches the host as a wasm trap and is reported with the
> browser's or node's own JavaScript stack. Keeping one file avoids a second
> declaration of the same entry points, which is the whole of the difference.

Worth noting separately: the engine uses **no C++ exceptions at all** — zero
`catch` blocks across `code/*.cpp`. `-fno-exceptions` is available for engine
code, subject to what bgfx and bx require.

> **Corrected.** The build passes `-fwasm-exceptions`, not `-fno-exceptions`,
> and the reason is the yield scaffold rather than the engine's own use of
> exceptions: `-fexceptions` routes every call that can unwind through an
> `invoke_` import, and Emscripten declares every `invoke_` import suspending
> whenever JSPI is on, so a static initializer would try to suspend outside a
> promising boundary and trap (`code/CMakeLists.txt:157`–`:165`).

Estimated **1–2 weeks**.

## C.9 Assets and licensing

[README](../README.md) is explicit: OpenTS "supplies the engine, not the game
data," and is "not a distribution of the original game assets." Installation
means extracting the release into a Tiberian Sun directory obtained from Steam
or the EA App. That statement is not a policy detail here — **it decides
whether a browser build is usable at all**, so take it first.

### The licensing shape

The engine is GPLv3-or-later (`LICENSE.md`) with EA's Section 7 additional
terms on derived material. The engine can be served from anywhere. The data
cannot: it is EA's, and the project neither has nor claims a right to
distribute it. So a web build cannot be a link that a stranger clicks and
plays. It must be a page that asks the visitor for the Tiberian Sun files they
already own.

That is a real cap on the value of the port and should be stated plainly in
whatever ships, not discovered by users. It also rules out two otherwise
obvious designs: a preloaded `.data` bundle, and lazy fetching from a
project-hosted origin. Both would mean the project serving EA's assets.

### What the engine actually needs

The startup path opens a fixed set of archives, and also **enumerates
directories**: `FindFirstFile("ECACHE*.MIX")` at `code/init.cpp:2159`,
`"ELOCAL*.MIX"` at `:2182`, `"MAPS*.MIX"` at `:2555`, `"MOVIES*.MIX"` at
`:2643`, plus `EXPAND%02d.MIX` and `ECACHE%02d.MIX` probing at `:2393` and
`:2406`, and the named archives `TIBSUN.MIX` (`:2484`), `CACHE.MIX` (`:2498`),
`LOCAL.MIX` (`:2507`), `CONQUER.MIX` (`:2543`), `MULTI.MIX` (`:2582`),
`SOUNDS.MIX` (`:2609`), `SCORES.MIX` (`:2623`), and theater archives at
`:5451`–`:5453`. There are 29 `FindFirstFile`/`FindNextFile` calls across seven
files, and 44 Win32 filesystem calls in total once directory and module-path
queries are counted. So the virtual filesystem must support directory listing,
not just open-by-name.

`MFCD::Cache("CACHE.MIX")` (`code/init.cpp:2501`) also reads whole archives
into memory, which sets a floor on the wasm heap that should be measured before
committing to an `INITIAL_MEMORY`.

Scale matters here. A full Tiberian Sun plus Firestorm install runs to roughly a
gigabyte, and the bulk of that is the `MOVIES*.MIX` archives — a figure to
confirm against a real install rather than take from this document. That is
enough to run into browser storage quota, which is per-origin, heuristic, and
evictable. The useful consequence is that the archives are *separable*: the
`MOVIES*.MIX` probe at `code/init.cpp:2643` is a wildcard scan that finds
nothing gracefully, and `SCORES.MIX` (`:2622`), `SCORES01.MIX` (`:2633`),
`SOUNDS01.MIX` (`:2596`), and `PATCH.MIX` (`:2430`) are all guarded by
availability checks. So the import can be tiered — required archives first,
movies and alternate score sets optional — which keeps the common case well
inside quota and makes the first run fast.

> **Corrected: the movie probe is not graceful.** `Init_Secondary_Mixfiles`
> returns false when no `MOVIES*.MIX` was found (`code/init.cpp:2727`), which
> makes `Init_Game` return −1 and ends the run before the menu. `SCORES.MIX` is
> fatal in the same way (`:2692`), and so is the `MAPS*.MIX` scan (`:2641`). The
> tiering above therefore needs either those checks relaxed or the movie
> archives moved into the required tier, and that choice should be made
> deliberately: relaxing the check changes behavior on the Win32 target too.

### Proposal

> **Superseded.** The proposal below was not what was built, and the reason is
> that the engine scans directories. What was built reads an **ISO 9660 image**,
> mounted lazily on the first name the host cannot answer for
> (`code/win32compat.cpp:535`) and, in a page, served over HTTP range requests
> (`code/isohttp.cpp`). The volume answers `FindFirstFile` directly, so
> requirement 2 below — a manifest built at import time — does not arise, and
> neither does the import UX that most of the estimate was for. Requirement 3
> held exactly: nothing above `FileClass` changed.
>
> The transport that answers a read is a synchronous `XMLHttpRequest`, and that
> is forced rather than chosen: the engine's first file open happens in a static
> constructor, before `main`, where a JSPI suspend is illegal, so an
> asynchronous fetch there would end the run (`code/isohttp.cpp:13`–`:21`). It
> is the one place in the port where the scaffold could not be used. Reads that
> settle into a forward run are also fetched ahead of, and that fetch is an
> ordinary asynchronous one — starting it is not a wait, and the read that wants
> it copies it off the heap.
>
> The licensing analysis above is unchanged and still governs: the image is the
> player's own, named by the page or the host, and the project serves none.
> [Building OpenTS](BUILDING.md#where-the-webassembly-target-finds-game-data)
> owns how it is configured.

**User-supplied files, persisted in IDBFS, with no project-hosted data path.**

1. On first run the page asks the visitor to point at their Tiberian Sun
   directory — a directory picker or a drag-and-drop of the folder. Files are
   written into an IDBFS mount and `FS.syncfs`'d, so subsequent runs start
   immediately from IndexedDB.
2. A manifest is built at import time so the `FindFirstFile` paths can be
   answered from an index rather than by scanning.
3. `RawFileClass` (see [B.1](#b1-seams-that-exist-and-should-be-kept)) is the
   only thing that changes; everything above `FileClass` is untouched.
4. Provide a re-import path, because IndexedDB is evictable and browsers do
   clear it.

Deliberately not proposed: `--preload-file` bundling (distributes the data),
lazy `createLazyFile` fetching from a project origin (same problem), and
synchronous XHR (deprecated, and unavailable in the contexts that matter).

A self-hosted deployment — a modder serving their own build alongside data they
have the right to serve — is a separate, legitimate configuration that falls
out of the same design at no extra cost. It should be documented as such and
never as the default.

Estimated **1–2 months**, most of it in the import UX and the manifest.

---

# Part D — sequencing and cost

## D.1 Dependency order

```
  asm → C++  ─────────┐                     (separate workstream)
  Emscripten CMake ───┤                     (separate workstream)
                      │
  A.6 steps 1-3  ─────┼──→  A.6 step 4  ──→  first browser frame
  (Win32 refactor)    │     (main loop
                      │      + JSPI)
  B: platform seam ───┘            │
                                   ├──→ C.2 audio ────────────┐
                                   ├──→ C.9 assets ───────────┤
                                   ├──→ C.4 resources ────────┤──→ playable
                                   ├──→ C.1 phase 1 (shim) ───┤
                                   ├──→ C.7, C.8 (deletions) ─┘
                                   │
  C.3 COM/save ──(own change on Win32 first)──→ saves work
  C.6 determinism ──(own change on Win32 first)──→ cross-play
                                   │
                                   └──→ C.1 phase 2 + A.6 steps 5-9
                                        (flatten, then drop JSPI)
```

Three things are genuinely serial and cannot be worked around:

1. **The assembly must go before anything compiles.** Largely done as this is
   written: only `code/winasm.asm` is left, and the SOS ADPCM codec that blocks
   audio now has a C implementation in `code/soscodec.cpp`.
2. **The platform seam must exist before its consumers are ported.** Porting
   audio before there is an audio backend header means porting it twice.
3. **`C.3` and `C.6` must land on the Win32 target first.** Both are
   compatibility-boundary changes with their own evidence and change-record
   obligations under [Contributing](../CONTRIBUTING.md). Doing them inside the
   port would hide a save-format change and a simulation change inside a
   platform change, which is exactly what the repository rules forbid.

## D.2 What parallelizes

Freely parallel once step 4 lands: audio (C.2), assets (C.9), resources (C.4),
networking (C.5), the deletions (C.7, C.8), and the input seam. Different
files, no shared state.

Parallel with care: C.1 phase 1 is one person's job — it is a coherent shim and
splitting it produces seams that do not meet. C.1 phase 2 parallelizes well
across contributors *after* the first two or three dialogs establish the
pattern.

Not parallel with anything: A.6 steps 1–3, because everything else builds on
the shape they establish.

## D.3 Estimate

| Work | Estimate | Confidence |
| --- | --- | --- |
| A.6 steps 1–4 — main-loop restructure and first browser frame | 1–2 months | Medium-high; the code is read and the shape is clear |
| B — platform seam extraction | 1–2 months | Medium-high; `bgfxbackend.h` sets the pattern |
| C.1 phase 1 — HWND shim, dialog converter, font metrics | 2–4 months | Medium; the API census is solid, the unknown is behavioral fidelity |
| C.2 — audio | 1–2 months | Medium-high; excludes the codec rewrite |
| C.3 — COM removal and save container | 2–5 months | Medium; the container decision dominates |
| C.4 — resource container | 3–6 weeks | High |
| C.5 — networking, engine side | 1–2 months | Medium-high; excludes the relay |
| C.6 — determinism, engine side | 1–2 months | Low-medium; validation is open-ended |
| C.7 + C.8 — threading and SEH | 1 month | High; mostly deletion |
| C.9 — assets | 1–2 months | Medium |
| **Playable browser build, with the JSPI scaffold** | **12–20 engineer-months** | **Medium** |
| C.1 phase 2 + A.6 steps 5–9 — flatten and drop the scaffold | 6–12 months | Low-medium |
| **Scaffold-free browser build** | **20–32 engineer-months** | **Low-medium** |

These assume one engineer familiar with the tree and exclude the two workstreams
already running. They also exclude the tunnel relay, which is a separate
project. And they should be read against the fact that the automated safety net
is thin: `tests/` currently contains a single test
(`tests/logstress/logstress.cpp`), so almost all verification is manual
play-through against the Win32 build.

> **Corrected.** The safety net is no longer that thin, though it is still thin
> for what this section estimates. Eleven tests are registered under Emscripten
> and eleven under MSVC, covering the decoders, the lighting and voxel paths,
> the ISO reader, the Win32 file layer, the PE resource reader, and the audio
> backend; [Building OpenTS](BUILDING.md#tests) lists them. None of them
> exercises the simulation, so the replay harness [C.6](#c6-determinism) calls
> for is still unbuilt and manual play-through is still the only check on the
> parts these estimates are about.

## D.4 Genuine risks

Four things could fail permanently rather than merely take longer.

1. **Cross-play with native peers.** wasm-against-wasm is solvable. Native
   cross-play requires the *native* build to adopt the same pinned libm
   ([C.6](#c6-determinism)) — a deliberate change to the shipped simulation, at
   a compatibility boundary, affecting replays and network compatibility with
   every existing build. That is a project decision, not a porting task, and it
   could reasonably be declined. If it is declined, the honest position is that
   browser and native players do not share games. **Assessment: solvable, but
   only by changing the native build; plan for the possibility that it is not
   worth it.**

2. **Asset delivery and licensing.** A browser build cannot ship the data
   ([C.9](#c9-assets-and-licensing)), so it will always be "bring your own
   install." That removes the main thing a web build is usually for — a link a
   stranger clicks — and leaves a narrower audience: existing owners who want no
   installation, modders demonstrating work, and contributors testing.
   **Assessment: not a technical risk; a value risk, and the one that should be
   settled before the work starts rather than after.**

3. **Westwood Online.** `code/wonline.cpp` (10,514 lines) uses ATL and
   in-process COM DLLs for a service that no longer exists, talking to servers
   that no longer answer. It will never work in a browser, and the README's
   plans point at CnCNet instead. **Assessment: will not work; exclude it from
   the wasm target outright and treat any browser multiplayer as tunnel-only.**

4. **The nested-pump flattening completing at all.** Sixty load-bearing waits,
   most of them inside a front end that is simultaneously being rewritten, is a
   long tail with a strong pull toward "the scaffold works, leave it." If JSPI
   stays, the port stays pinned to a young VM feature and the reentrancy the
   project wants to remove stays with it. **Assessment: the most likely way this
   port ends up permanently half-finished. The mitigation is procedural, not
   technical: count the remaining yield points, publish the number, and do not
   let it rise.**
