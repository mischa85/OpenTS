# OpenTS for macOS and iOS

A native shell around the WebAssembly engine. The engine, its page and its modules are
built by CMake and copied into the app bundle; the app supplies a window, a menu, and one
thing a browser cannot: a scheme handler that answers the engine's ranged reads out of a
disc image on this device.

OpenTS supplies the engine and not the game data. A player points the app at their own
disc images; until they do, it falls back to the addresses `DiscLibrary.archiveDiscs()`
holds.

A first launch on those addresses reads its working set over the network before the menu
can appear, so the window carries a readout of what has been read and how fast while it
does. What arrives is kept on this device, so only the first launch pays for it.

## What it needs

- Xcode 15 or later (built and run against Xcode 26).
- [XcodeGen](https://github.com/yonaskolb/XcodeGen) to regenerate the project after
  editing `project.yml`. `brew install xcodegen`.
- A WebAssembly build of the engine — see [Building OpenTS](../docs/BUILDING.md) for the
  Emscripten toolchain.

## Building

The engine is built twice, because the two suspension strategies are separate
configurations and the page chooses between them at load time. WKWebView has no JavaScript
Promise Integration, so on Apple platforms the page loads the Asyncify module; the JSPI
module is bundled alongside it so the same app switches to the faster one if WebKit ever
ships the feature.

```sh
source ~/emsdk/emsdk_env.sh

emcmake cmake -S . -B build-wasm -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DOPENTS_WASM_NODERAWFS=OFF \
  -DCMAKE_EXE_LINKER_FLAGS="-sEXPORTED_RUNTIME_METHODS=FS,callMain"
ninja -C build-wasm OpenTS

emcmake cmake -S . -B build-wasm-asyncify -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DOPENTS_WASM_NODERAWFS=OFF -DOPENTS_WASM_SUSPEND=ASYNCIFY \
  -DCMAKE_EXE_LINKER_FLAGS="-sEXPORTED_RUNTIME_METHODS=FS,callMain"
ninja -C build-wasm-asyncify OpenTS
```

Then generate the Xcode project and build:

```sh
cd apple
xcodegen generate
open OpenTS.xcodeproj
```

`OPENTS_WEB_DIRS` is where the bundling script takes the artifacts from — a colon separated
list of `bin` directories, defaulting to the two above. Point it elsewhere on the command
line if your build directories are named differently:

```sh
xcodebuild -project OpenTS.xcodeproj -scheme OpenTS-macOS -configuration Release \
  OPENTS_WEB_DIRS="$PWD/../build-wasm/bin:$PWD/../build-wasm-asyncify/bin" build
```

The build fails rather than producing an app with nothing to run if it finds no page and no
module.

## Layout

| Path | What it is |
| --- | --- |
| `project.yml` | The XcodeGen spec the project is generated from. |
| `Shared/DiscLibrary.swift` | The configured discs and their persistence as bookmarks. |
| `Shared/DiscScheme.swift` | The `disc:` scheme: the bundled engine, and ranged reads of an image from a file or a server. |
| `Shared/DiscCache.swift` | The copy of a served image this device keeps, and the fetching that fills it. |
| `Shared/GameSession.swift` | The webview, the run's status, and the browser side storage. |
| `macOS/` | The Mac window, menu and settings panel. |
| `iOS/` | The iPhone and iPad window and setup screen. |
| `Support/copy-web.sh` | Copies the page and modules into the bundle at build time. |
| `Support/make-icons.swift` | Renders the app icons from the project's mark. Run it when the mark changes. |

## Discs

A player points the app at their own images. On macOS that is an open panel; on iOS a
document picker, or a file dropped into the app's own Documents folder over file sharing.
Either way what is recorded is a bookmark, so the same files reopen on a later launch
without asking again.

Images can also be read from a server that answers ranged requests, entered by address in
the settings panel. Until a player configures anything, `DiscLibrary` falls back to the
addresses in `archiveDiscs()`.

A ranged read a server does not answer usably is tried up to three times before it counts
as a failure, and the address is resolved afresh in between: a run makes hundreds of these
reads and a public mirror will occasionally answer one of them badly. A read that recovers
goes to the log and nowhere else. Only a read that stays failed raises the alert, and the
alert says what the server answered rather than what that would imply about the server. A
speculative fetch that fails is not reported at all: its blocks simply stay absent, and
whoever wants them asks for them.

Local images are read straight off the device and go nowhere near the cache: the engine is
told not to keep blocks for them and neither does the shell, because there is nothing a
copy could make faster.

### The served images

The engine's transport is synchronous, and WebKit answers a synchronous request on a custom
scheme by blocking the web content process on one message to this app. So a read that costs
a round trip costs the whole run that round trip, and nothing the engine does overlaps two of
them. Against a public mirror a ranged request waits one to three seconds for its first byte
however little is asked for, and the run to the menu touches about 124 blocks across three
images.

`DiscCache` is what makes that affordable, and it does it without knowing anything about what
a disc holds — no offset, file or region is named anywhere in it, so it behaves the same for
an image of any language or edition:

- The run's images are opened side by side before the page is loaded, rather than one at a
  time through the engine's own probes.
- A read that cannot be answered from this device is fetched on its own and answered as soon
  as it lands, while the megabyte it sits in and the three after it are fetched beside it. A
  megabyte costs what a block costs, and eight requests beside each other cost what one
  costs; both are measured, not assumed.
- A read one of those megabytes is already fetching waits for that request instead of asking
  for the same bytes a second time, but only when the request has been out long enough that
  what is left of it is shorter than a fresh one — both times taken from what this link has
  been answering rather than from a constant. It joins only a request that covers the whole
  read, since one covering part of it would leave the rest to be asked for anyway. A join
  that has waited longer than it projected asks for its own bytes after all, so a stalled
  megabyte cannot hold up the read the engine is blocked on.
- Everything fetched is written into a sparse file as long as the image, with a bitmap in its
  header recording which blocks are there. A block is recorded only after its bytes have been
  flushed, so a crash costs a refetch and never a block that reads as present and is zeroes.

The files live in the app's `Caches` directory, one per image, named for the identity the
page reads it through. They can hold two gigabytes between them, which is about what three
disc images read from end to end come to; over that, whole images are dropped, least
recently used first, and the system may reclaim the directory itself if the disk fills. The
settings panel reports what they are holding, and **Clear Cached Discs…** empties them along
with the engine's own block store in browser storage. Saved games are stored separately on
the same origin and are not touched.

An image is checked against the server once per run, when it is opened: the length and
validator in that answer decide whether the kept copy still describes the file the server
has, and a copy that does not is dropped rather than served.

## What a run leaves behind

The shell writes to the unified log under the subsystem `org.opents.shell`, in four
categories: `disc` for the reads the engine asked for, `cache` for what reached the network,
`page` for what the page reports about the run, and `stall` for the engine's record of the
reads it was blocked on. The first three are at info and debug level, which live in the
memory buffer, so they are read with `log stream` while a run is going:

```sh
log stream --predicate 'subsystem == "org.opents.shell" and process == "OpenTS"' --level debug
```

`stall` is at notice level instead, because it is meant to be read once the session is over:

```sh
log show --predicate 'process == "OpenTS" and subsystem == "org.opents.shell"
    and category == "stall"' --last 1h
```

One line per stall, carrying whatever the engine recorded for it — a sequence number, a
monotonic timestamp, the image, the offset and length, the milliseconds it was blocked, and
whether it was a read the engine waited on or a look-ahead that had not landed. The same
lines go to `stalls-<when>.txt` in the app's Documents folder, one file per run, so a session
can be handed over as one artefact; a run with no stalls leaves no file. The running total of
seconds blocked is logged every ten seconds, so a session that ends unexpectedly still leaves
a headline number.

The record comes from the engine, and the shell reads it two ways: from
`window.OpenTS_State.stalls` with `window.OpenTS_State.stallSeconds` beside it, or from
`Module._OpenTS_Iso_Stalls(after)` with `Module._OpenTS_Iso_Stall_Seconds()`. An engine that
offers neither is noted once and not asked again.

## Signing

The macOS target is ad hoc signed and runs outside the App Sandbox by default. A sandboxed
build cannot remember the discs a player picked unless it is signed with a real identity:
macOS refuses a persistent security scoped bookmark to an ad hoc signature, and the app
then falls back to a plain bookmark, which the sandbox will not reopen.

For a sandboxed build, set a team and the entitlements:

```sh
xcodebuild -project OpenTS.xcodeproj -scheme OpenTS-macOS \
  DEVELOPMENT_TEAM=XXXXXXXXXX CODE_SIGN_STYLE=Automatic \
  OPENTS_ENTITLEMENTS=macOS/Sandbox.entitlements build
```

The iOS target builds and runs in the Simulator without signing. A device build needs a
development team and a provisioning profile for `org.opents.shell`.

## Icons

`Support/make-icons.swift` renders both asset catalogs from
`manual/site/public/favicon.svg`, the project's own mark. The rendered PNGs are checked in
so a build needs nothing but Xcode. macOS insets the mark inside the platform's rounded
shape; iOS fills the square and drops the alpha channel, which iOS icons may not have.
