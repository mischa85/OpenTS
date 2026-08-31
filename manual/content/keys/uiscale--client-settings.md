---
key: UIScale
scope: client-settings
label: Stored scale
see_also: [ScreenWidth, ScreenHeight, CursorScale]
when_omitted:
  kind: unchanged
  note: The read passes through the scale the surfaces were already built around, and nothing later overwrites it.
---

This is the later of the two reads of the assignment, made with the rest of the client
settings once the drawing surfaces already exist. It reads the same figure back into the same
setting, so on its own it changes nothing that is already on screen.

What it settles is the scale a later rebuild of the surfaces uses. Changing the resolution
lays the screen out again, and the interface is then sized from whatever this read left
behind. Leaving the options screen writes the value back to `sun.ini`, so the file carries
one once that screen has been used.
