# Engine source agent instructions

These instructions apply to `code/` and supplement the repository-wide
instructions in [`../AGENTS.md`](../AGENTS.md).

## Read before changing source

- Use [`../docs/BUILDING.md`](../docs/BUILDING.md) for supported build paths.
- Use [`../docs/STYLE.md`](../docs/STYLE.md) for source conventions.
- Use [`../docs/HARNESS.md`](../docs/HARNESS.md) before running the WebAssembly
  build in a browser.
- Read the task-relevant implementation, callers, data paths, and tests before
  editing.

## Language and change discipline

- Target C++20 for new and substantially rewritten code. Modernize inherited
  code incrementally instead of mechanically converting the tree.
- Follow surrounding naming, layout, and ownership conventions unless the
  change deliberately establishes and validates a new local pattern.
- Keep mechanical cleanup separate from behavior changes. Format only code
  touched by the current change.
- Do not rename reconstruction placeholders such as `func_XXXXXX`,
  `field_XXX`, or `entry_XX` without evidence for the replacement name.
- Before moving state or replacing a subsystem, trace initialization,
  ownership, callers, teardown, persistence, and externally consumed effects.

## Compatibility boundaries

Treat game-data formats and defaults, saves and replays, network packets,
deterministic simulation, consumed COM interfaces, and layout-sensitive
structures as compatibility boundaries.

- Establish current behavior before changing it and classify what the change
  does to it: preserved, fixed, or intentionally changed.
- Preserve representations and defaults unless the contribution explicitly
  defines, tests, documents, and where necessary migrates the incompatibility.
- Use binary matching and the archived executable as historical evidence, not
  as the sole correctness criterion.

## Comments

Keep comments sparse; most edits need none.

- Comment only what the code cannot show: an invariant, a compatibility
  constraint, or genuinely surprising behavior. One concise sentence usually
  suffices. Do not annotate declarations, branches, or edits out of habit.
- A comment describes the code as it now stands, never the edit that produced
  it. `// retired; slot kept, save headers store these values` on a kept enum
  slot states a live constraint; `// removed the modem branch` narrates a
  diff, and that history belongs to git.
- Preserve accurate historical comments. Correct an inaccurate ordinary
  comment narrowly when current code or stronger evidence proves it wrong.

## Comment styles

The tree mixes distinct comment forms. Follow the surrounding file for
indentation, placement, and width, but not for the choice of form: inherited
usage records what Westwood wrote and does not authorize writing more of it.
Where a rule below and the neighboring lines disagree, the rule decides.

- **Historical file headers** — the Westwood banner at the top of an inherited
  file, including its `Functions:` table — are frozen verbatim. Do not edit
  them even when the functions they list change or disappear. Header
  maintenance requires an explicit legal or attribution change with
  repository-wide review.
- **Historical function banners** (`/*** Name -- purpose ... HISTORY ***/`)
  stay while they are accurate; correct a wrong detail narrowly. When a
  banner needs substantial rewriting, replace it with `///` XML documentation
  instead of re-authoring it in the historical form, and never write a new
  Westwood-style banner.
- **Ordinary historical comments** (Westwood `/* ** */` blocks and old `//`
  prose) keep their form through a narrow correction. A substantial amendment
  restates the comment as `//` OpenTS prose instead of extending the Westwood
  block decoration.
- **`///` marks XML documentation and nothing else** (`<summary>`, `<param>`,
  `<returns>`), the form for new or substantially rewritten function
  documentation. Much of the inherited tree trails ordinary prose after `///`
  instead. Read that as inherited noise rather than as the local convention: a
  new trailing comment takes `//` however its neighbors are written.
- **`//` and plain `/* */` blocks** (including the `//----` and `//....`
  separator lines) carry ordinary prose inside code. Where Westwood decorated
  blocks are prevalent, a plain `/* */` block is the way to sit alongside
  them; new code does not reproduce the `**` continuation prefix. Follow the
  local indentation and spacing, and introduce no new decoration styles.

Preserve SPDX identifiers, copyright notices, modification notices, and GPL
Section 7 notices. Comment syntax does not establish authorship.

## Documentation, validation, and handoff

- Update the owning user or developer documentation with every material source
  change. If no update is required, state why the existing documentation
  remains accurate.
- Run the narrowest relevant checks first, followed by the supported build or
  test suite when the change warrants it.
- Use `python3 tools/harness/harness.py` to run the WebAssembly build in a
  browser. Do not write a server, browser launcher, readiness poll, input
  dispatch, or screenshot comparison of your own; extend the harness instead.
  It reads the developer's discs, so what it produces is a runtime observation
  and never a test result.
- Report exact commands, configurations, environments, results, and relevant
  checks not run. Never turn an unverified result into a support claim.
- Behavior changes require focused evidence and corresponding documentation.
- Automated checks must not require proprietary game assets or original game
  executables.
