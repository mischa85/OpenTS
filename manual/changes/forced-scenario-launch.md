---
title: Start a scenario from the command line
category: feature
release: 0.2.0
targets:
- type: command
  id: launch:scenario
  effect: added
- type: command
  id: launch:campaign
  effect: added
credit:
- Gunnar Beutner
---

`-SCENARIO=<name>` starts the named scenario at once, in place of the main menu and the
campaign chooser. `-CAMPAIGN=<name>` names the battle it is played as part of, which
decides the closing movie and the campaign a save records; without it the scenario is
played on its own. Both names are spelled as the battle control file spells them, as in
`-SCENARIO=GDI1A.MAP -CAMPAIGN=GDI1`.

The engine already carried the forced-scenario state and honored it while reading the
scenario, but nothing set it and the campaign chooser was still put up for a choice that
had already been made. Menu selection without these options is unchanged.
