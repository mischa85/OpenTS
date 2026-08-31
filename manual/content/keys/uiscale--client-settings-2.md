---
key: UIScale
scope: client-settings-2
label: Scale the interface is laid out at
see_also: [ScreenWidth, ScreenHeight, CursorScale]
when_omitted:
  kind: computed
  note: A scale of zero, which is also what an absent assignment leaves, is one step for every 540 rows of the screen.
---

This is the earlier of the two reads of the assignment, made before the main window exists,
because the drawing surfaces are sized around the answer and they are built before the rest
of the client settings are read.

The sidebar is drawn from artwork 168 pixels across, and the scale is how many screen pixels
each of those pixels becomes. At `1` the sidebar occupies its own 168 pixels of the screen
however large the screen is; at `2` it occupies 336 and every cameo, button and readout in it
is twice the size. The playfield is not magnified with it: it keeps the screen's own
resolution and gives up the width the sidebar takes, so raising the scale makes the interface
easier to read rather than making the world larger.

Whole numbers from `1` to `4` are accepted and anything outside that is brought into it. `0`,
which is what an absent assignment leaves, follows the screen instead: one step for every 540
rows, so a 1080-row screen lays the interface out at double size and a 2160-row one at
quadruple. A screen shorter than 1080 rows keeps the interface at the size its artwork was
drawn.

A scale the screen cannot carry is stepped back down until it can: the interface needs at
least 400 rows of its own to lay its artwork out in, and it may not take more than half the
screen's width. A 1080-row screen therefore accepts `2` and answers `3` and `4` with `2`, and
a 640 by 400 screen keeps the interface at its own size whatever is asked for.

How tall the sidebar's own surface is decides how many build cameos a strip can show, and
that surface is the screen height divided by this scale. A larger scale therefore trades rows
of the build list for readable ones, and by more than the scale alone: the radar pane and the
backdrop's end caps cost the same in either case, and what is left over is what the cameos
divide. The strip scrolls for whatever no longer fits.

This setting does not change the size of the graphic shell's menus, briefings or dialogs.

:::note[The tab bar keeps its size]
The bar along the top of the screen is drawn into the playfield and the sidebar alike, so it
is not magnified with the rest of the interface. At a scale above one the sidebar's credit
readout is drawn taller than the tab bar beside it.
:::
