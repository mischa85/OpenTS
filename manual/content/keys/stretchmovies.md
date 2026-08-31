---
key: StretchMovies
summary: Stretches full screen movies to fill the display instead of playing them at their own size.
see_also: [ScreenWidth, ScreenHeight]
when_omitted:
  kind: value
  value: "no"
---

The flag reaches the full screen movie player alone, and only where that player's caller also asks for stretching. A movie played into a fixed rectangle — the sidebar's, and the one a graphical menu plays its backdrop into — never consults the flag and keeps that rectangle either way, because the artwork drawn over such a movie is placed against that rectangle at its own size.

A stretched movie grows by whichever of the two ratios it fits inside and is centered, so a display wider or taller than the movie's own shape leaves a margin rather than pushing the picture off the edge. The flag takes effect on every display; there is no display that refuses it.

The browser build omits it as `yes` rather than `no`, because the frame there is the browser window rather than a resolution anyone picked. Writing the key still decides it there as it does anywhere.

The display options screen carries the same switch and stores it as the screen is accepted; leaving the options screen behind it writes the setting back to `sun.ini`.
