# Developer documentation

Each document owns one class of project fact:

- [Building OpenTS](BUILDING.md) — supported toolchain, build commands,
  outputs, build identity, and continuous integration.
- [Style](STYLE.md) — source formatting, naming, language, and comment
  conventions.
- [History](HISTORY.md) — source lineage, the reconstruction, and its
  methods.
- [Rationale](RATIONALE.md) — the reconstruction's tooling, structure
  recovery, and the reasoning behind choices a reader could mistake for
  accidents.
- [Project direction](DIRECTION.md) — the architectural direction behind the
  roadmap.
- [The browser harness](HARNESS.md) — the one way to run the WebAssembly build
  in a browser: serving it, driving it, observing it, and taking it down.

The WebAssembly port has three documents of its own. [Building
OpenTS](BUILDING.md) owns its build support and what has been run; these record
the rest:

- [WebAssembly target status](wasm-compile-status.md) — the Win32 substitute the
  target is built on, its layout and ABI constraints, and what is still missing
  behind it.
- [WebAssembly port design](WASM-PORT.md) — the design the port was started
  from, dated, with the parts of it that turned out to be wrong marked as such.
- [The WebAssembly path to the main menu](wasm-menu-path.md) — a dated
  investigation of what stood between the engine and its first menu frame, kept
  for the analysis and for the record of what it got wrong.

Contribution process and review expectations live in
[CONTRIBUTING.md](../CONTRIBUTING.md), and the public manual for players and
modders is authored under [manual/](../manual/README.md). Link to the owning
document instead of copying its facts into another guide.
