---
title: Save a game under the name the save dialog picked for it
category: fix
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

Saving into an empty slot now writes the file under the `SAVE####.SAV` name the dialog
chose. The name lived in a buffer that had already gone out of scope by the time the save
ran, so the game could be written under whatever happened to be left there. A file named
that way is not found by the search for saved games, which is why Load Game stayed
unavailable after a successful save.

The description, house, scenario and version a save carries are also written at their true
length again. They were measured in characters of the wrong width, so each one was stored
short, unterminated, and followed by whatever the previous value had left behind. A save
written that way still loads, and its description now reads back as far as it survives.
Saves written by an unaffected build are unchanged, and the file format is the same in
either direction.
