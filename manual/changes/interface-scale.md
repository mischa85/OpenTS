---
title: Magnify the in-game interface on its own
category: feature
release: 0.2.0
targets:
- type: key
  id: UIScale
  effect: added
credit:
- Gunnar Beutner
---

The sidebar can now be drawn larger without shrinking the battlefield. `UIScale` under
`[Video]` in `sun.ini` is how many screen pixels each pixel of the interface artwork becomes,
from one to four; left out, it follows the screen, at one step for every 540 rows. The
playfield keeps the screen's own resolution either way and gives up the width the wider
sidebar takes, so a large display no longer has to be run at a low resolution to make the
interface readable.

Raising the scale gives the sidebar fewer, larger build rows, because the sidebar is laid
out in its own pixels and there are fewer of them to lay out in. A saved game does not carry
the scale, and one written at one scale loads at another: a build strip scrolled far down may
open on empty slots and scrolls back up to the cameos.

`ScreenWidth` and `ScreenHeight` are unchanged and still set the resolution the whole picture
is rendered at. Display options does not offer the interface scale yet, and the graphic
shell's menus and dialogs are not magnified by it.
