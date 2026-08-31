---
title: Fill the window with the menus and loading screens
category: fix
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

The graphic shell no longer draws itself at 640x400 in the middle of a larger frame. The
game selection page, the Tiberian Sun and Firestorm menus, and the loading and title screens
are magnified to the largest rectangle of their own shape the window holds, centered, with
black beside them. A window already the size the artwork was drawn at is unchanged, and the
menus answer the mouse where the choices now appear rather than where they used to be.

A menu takes its size from the backdrop it lays its choices out against, so artwork drawn
larger than 640x400 is magnified whole rather than cut down to that. The version and
copyright stamp moves with the menu it belongs to instead of sitting in the corner of the
frame. On a loading screen the caption moves with the artwork, but is still lettered at the
size it always was.

The score, objectives, map selection, mission choice, multiplayer score, dropship loadout,
progress bar, World Domination Tour and credits screens keep the size they were drawn at for
now, as does the plain menu the game falls back on when the graphic shell is not installed.
