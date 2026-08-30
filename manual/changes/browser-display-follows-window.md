---
title: Fill and follow the browser window
category: feature
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

The WebAssembly build now draws the game across the whole browser window and changes
resolution with it: widening or heightening the window gives a wider playfield and more
sidebar rows rather than a larger picture of the same one. The resolution is the window
measured in CSS pixels, so the sidebar and the tab bar keep their size on a display that
carries several device pixels for each of them, and the frame is presented into a drawing
buffer of the full device resolution.

Display options in the browser build lists resolutions again. It offers the window's own
size, the resolution the game is running at, and the common 4:3 and widescreen sizes that
fit on the display; choosing the window's own size keeps the game following the window, and
choosing any other size pins the game to it and scales it up to fill. `?display=scaled`
and `?display=1024x768` on the page make that choice before the game starts.

A resolution change replaces every drawing surface, so it waits for the window to stop
moving and for the engine to reach a point that can take it: never while a movie is playing
or a scenario is loading. Until then the frame already in hand is scaled to fill the
window. An open dialog is no barrier. Resizing the window while the game is paused on one
moves the frame with it, and the dialog and the screen behind it are drawn again at the new
size.
